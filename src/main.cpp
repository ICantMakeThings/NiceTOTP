// Hello! This is a nRF52840 based TOTP with a SSD... something display.
// Has a pin to lock your stuff, and more soon
//
// u = up button
// d = down button
// commands include:
// setunixtime
// add <username> <base32secret>
// factoryreset (power cycle the device)
// list
// del <GetTheIDFromListCommand>
// clear
// pinsetup (im traumatised from this, i need to fix it or remove it)
// lock
#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <SPI.h>
#include "TOTP++.h"
#include <Fonts/FreeSansBold9pt7b.h>
#include <nrf.h>
#include <nrf_gpio.h>
#include <bluefruit.h>
#include <Adafruit_TinyUSB.h>
#include <hal/nrf_power.h> // For NRF_POWER->GPREGRET
#include "nrf_nvic.h"      // For NVIC_SystemReset()

// Constants

#define DFU_MAGIC_UF2_RESET 0x57

using namespace Adafruit_LittleFS_Namespace;

BLEUart bleuart;
BLEDis bledis;

RTC_DS3231 rtc;

const uint8_t lockIcon8x8[] PROGMEM = {
    0b00111100, 0b01100110, 0b01000010, 0b01111110,
    0b01111110, 0b01111110, 0b01111110, 0b00111100};

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define BUTTON_UP_PIN PIN_031
#define BUTTON_DOWN_PIN PIN_029

const unsigned long INACTIVITY_TIMEOUT = 60000;
const unsigned long PIN_SETUP_TIMEOUT = 10000;
const int MAX_PIN_LENGTH = 20;
const int MAX_KEYS = 50;
const int MAX_FAILED_ATTEMPTS = 10;

const char *PIN_FILENAME = "/pin.dat";
const char *KEYS_FILENAME = "/keys.dat";

bool pinError = false;
unsigned long pinErrorTime = 0;
int failedAttempts = 0;

unsigned long lastActivityTime = 0;
unsigned long lastPinInputTime = 0;

char devicePin[MAX_PIN_LENGTH + 1] = {0};
int devicePinLength = 0;

char inputBuffer[MAX_PIN_LENGTH + 1] = {0};
int inputIndex = 0;

bool locked = true;
bool pinSet = false;
bool inPinSetup = false;

struct KeyEntry
{
  char username[16];
  char base32secret[33];
};

KeyEntry keys[MAX_KEYS];
int keysCount = 0;

bool buttonUpPressed = false;
bool buttonDownPressed = false;
int selectedKeyIndex = 0;

TOTP totp;

/*  https://github.com/ICantMakeThings/Nicenano-NRF52-Supermini-PlatformIO-Support/blob/main/Platformio%20Example%20code/Read%20Batt%20voltage/main.cpp  */
float readBatteryVoltage()
{
  // The volatile keyword is a type qualifier in C/C++ that tells the compiler a variable's value might change in ways that the compiler cannot detect from the code alone.
  // Essentially, it says: "Don't optimize access to this variable because its value might change unexpectedly."
  volatile uint32_t raw_value = 0;
  // Configure SAADC
  NRF_SAADC->ENABLE = 1;
  NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_12bit;

  NRF_SAADC->CH[0].CONFIG =
      (SAADC_CH_CONFIG_GAIN_Gain1_4 << SAADC_CH_CONFIG_GAIN_Pos) |
      (SAADC_CH_CONFIG_MODE_SE << SAADC_CH_CONFIG_MODE_Pos) |
      (SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos);

  NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_VDDHDIV5;
  NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELN_PSELN_NC;

  // Sample
  NRF_SAADC->RESULT.PTR = (uint32_t)&raw_value;
  NRF_SAADC->RESULT.MAXCNT = 1;
  NRF_SAADC->TASKS_START = 1;
  while (!NRF_SAADC->EVENTS_STARTED)
    ;
  NRF_SAADC->EVENTS_STARTED = 0;
  NRF_SAADC->TASKS_SAMPLE = 1;
  while (!NRF_SAADC->EVENTS_END)
    ;
  NRF_SAADC->EVENTS_END = 0;
  NRF_SAADC->TASKS_STOP = 1;
  while (!NRF_SAADC->EVENTS_STOPPED)
    ;
  NRF_SAADC->EVENTS_STOPPED = 0;
  NRF_SAADC->ENABLE = 0;

  // Force explicit double-precision calculations
  double raw_double = (double)raw_value;
  double step1 = raw_double * 2.4;
  double step2 = step1 / 4095.0;
  double vddh = 5.0 * step2;

  return (float)vddh;
}

// Batt status

void drawBatteryIcon(float voltage)
{
  const int iconX = SCREEN_WIDTH - 18;
  const int iconY = 0;
  const int iconWidth = 16;
  const int iconHeight = 6;
  const int terminalWidth = 2;

  display.drawRect(iconX, iconY, iconWidth, iconHeight, SSD1306_WHITE);
  display.fillRect(iconX + iconWidth, iconY + 2, terminalWidth, iconHeight - 4, SSD1306_WHITE);

  float minV = 3.0;
  float maxV = 4.2;
  bool isCharging = (voltage >= 4.3);

  if (isCharging)
  {
    const int fillMax = iconWidth - 4;
    int animPhase = (millis() / 150) % (fillMax + 1);

    display.fillRect(iconX + 2, iconY + 2, animPhase, iconHeight - 4, SSD1306_WHITE);
  }
  else
  {
    int fillWidth = (int)((voltage - minV) / (maxV - minV) * (iconWidth - 4));
    if (fillWidth < 0)
      fillWidth = 0;
    if (fillWidth > iconWidth - 4)
      fillWidth = iconWidth - 4;

    if (fillWidth > 0)
    {
      display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4, SSD1306_WHITE);
    }
  }
}

// Files

void savePin()
{
  if (!InternalFS.begin())
    return;
  if (devicePinLength == 0)
  {
    if (InternalFS.exists(PIN_FILENAME))
      InternalFS.remove(PIN_FILENAME);
    return;
  }
  File f = InternalFS.open(PIN_FILENAME, FILE_O_WRITE);
  if (!f)
    return;
  f.write((uint8_t *)&devicePinLength, 1);
  f.write((uint8_t *)devicePin, devicePinLength);
  f.close();
}

bool loadPin()
{
  if (!InternalFS.begin())
    return false;
  if (!InternalFS.exists(PIN_FILENAME))
    return false;
  File f = InternalFS.open(PIN_FILENAME, FILE_O_READ);
  if (!f)
    return false;
  if (f.size() < 1)
  {
    f.close();
    return false;
  }
  uint8_t len;
  f.read(&len, 1);
  if (len < 1 || len > MAX_PIN_LENGTH || f.size() != len + 1)
  {
    f.close();
    return false;
  }
  f.read((uint8_t *)devicePin, len);
  devicePin[len] = '\0';
  devicePinLength = len;
  pinSet = true;
  f.close();
  return true;
}

void loadKeys()
{
  keysCount = 0;
  memset(keys, 0, sizeof(keys));

  if (!InternalFS.begin())
  {
    Serial.println("FS mount failed");
    return;
  }

  if (!InternalFS.exists(KEYS_FILENAME))
  {
    Serial.println("No keys file found");
    return;
  }

  File file = InternalFS.open(KEYS_FILENAME, FILE_O_READ);
  if (!file)
  {
    Serial.println("Failed to open keys file");
    return;
  }

  while (file.available() && keysCount < MAX_KEYS)
  {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      continue;

    int sepIndex = line.indexOf(' ');
    if (sepIndex > 0)
    {
      String username = line.substring(0, sepIndex);
      String secret = line.substring(sepIndex + 1);
      username.toCharArray(keys[keysCount].username, sizeof(keys[keysCount].username));
      secret.toCharArray(keys[keysCount].base32secret, sizeof(keys[keysCount].base32secret));
      keysCount++;
    }
  }

  file.close();
  Serial.printf("Loaded %d keys\n", keysCount);
}

void saveKeys()
{
  if (!InternalFS.begin())
  {
    Serial.println("FS mount failed");
    return;
  }

  if (InternalFS.exists(KEYS_FILENAME))
  {
    InternalFS.remove(KEYS_FILENAME);
  }

  File file = InternalFS.open(KEYS_FILENAME, FILE_O_WRITE);
  if (!file)
  {
    Serial.println("Failed to open keys file for writing");
    return;
  }

  for (int i = 0; i < keysCount; i++)
  {
    file.printf("%s %s\n", keys[i].username, keys[i].base32secret);
  }

  file.flush();
  file.close();

  Serial.println("Keys saved");
}


bool isDuplicateKey(const char *username, const char *secret)
{
  for (int i = 0; i < keysCount; i++)
  {
    if (strcmp(keys[i].username, username) == 0 && strcmp(keys[i].base32secret, secret) == 0)
    {
      return true;
    }
  }
  return false;
}

// Display

void displayCode()
{
  display.clearDisplay();

  if (!pinSet)
  {
    display.setFont(nullptr);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Set PIN:");
    for (int i = 0; i < inputIndex; i++)
      display.print("*");
  }
  else if (locked)
  {
    display.drawBitmap(0, 0, lockIcon8x8, 8, 8, SSD1306_WHITE);
    char masked[inputIndex + 1];
    for (int i = 0; i < inputIndex; i++)
      masked[i] = '*';
    masked[inputIndex] = '\0';

    display.setFont(&FreeSansBold9pt7b);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(masked, 0, 0, &x1, &y1, &w, &h);
    int x = (SCREEN_WIDTH - w) / 2;
    int y = (SCREEN_HEIGHT + h) / 2 - 1;
    display.setCursor(x, y);
    display.setTextColor(SSD1306_WHITE);
    display.print(masked);
  }
  else
  {
    if (keysCount == 0)
    {
      display.setFont(nullptr);
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("No keys saved");
    }
    else
    {
      uint64_t now = rtc.now().unixtime();
      char *rawCode = totp.getCode(keys[selectedKeyIndex].base32secret, 30, now);
      char formattedCode[8] = "      ";
      if (rawCode && strlen(rawCode) == 6)
      {
        memcpy(formattedCode, rawCode, 3);
        formattedCode[3] = ' ';
        memcpy(&formattedCode[4], &rawCode[3], 3);
      }
      else
      {
        strcpy(formattedCode, "------");
      }

      display.setFont(&FreeSansBold9pt7b);
      display.setTextColor(SSD1306_WHITE);
      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds(formattedCode, 0, 0, &x1, &y1, &w, &h);
      int16_t x = (SCREEN_WIDTH - w) / 2;
      int16_t y = (SCREEN_HEIGHT + h) / 2 - 1;
      display.setCursor(x, y);
      display.print(formattedCode);

      display.setFont(nullptr);
      display.setTextSize(1);
      char displayUser[18];
      strncpy(displayUser, keys[selectedKeyIndex].username, 17);
      displayUser[17] = '\0';
      uint16_t uw;
      int16_t ux, uy;
      uint16_t uh;
      display.getTextBounds(displayUser, 0, 0, &ux, &uy, &uw, &uh);
      if (uw > SCREEN_WIDTH)
      {
        strncpy(displayUser, keys[selectedKeyIndex].username, 13);
        strcpy(&displayUser[13], "...");
      }
      display.setCursor(0, 0);
      display.print(displayUser);

      const uint8_t barHeight = 5;
      const uint8_t barY = SCREEN_HEIGHT - barHeight - 2;
      uint8_t elapsed = now % 30;
      uint8_t fillWidth = map(elapsed, 0, 30, 0, SCREEN_WIDTH - 2);
      display.drawRoundRect(0, barY, SCREEN_WIDTH, barHeight, 2, SSD1306_WHITE);
      if (fillWidth > 0)
        display.fillRoundRect(1, barY + 1, fillWidth, barHeight - 2, 1, SSD1306_WHITE);
    }
  }
  float batteryVoltage = readBatteryVoltage();
  drawBatteryIcon(batteryVoltage);

  // Last 2 digits of unix time under battery icon (Only in TOTP code menu)
  if (pinSet && !locked && keysCount > 0)
  {
    uint64_t now = rtc.now().unixtime();
    int lastTwoDigits = now % 100;
    char buf[3];
    snprintf(buf, sizeof(buf), "%02d", lastTwoDigits);
    display.setFont(nullptr);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(SCREEN_WIDTH - 18, 10);
    display.print(buf);
  }

  display.display();
}

// Buttons

bool debounceButton(bool currentState, bool &pressedFlag, uint32_t &lastDebounceTime, uint32_t debounceDelay, uint32_t currentMillis)
{
  if (currentState && !pressedFlag && (currentMillis - lastDebounceTime > debounceDelay))
  {
    pressedFlag = true;
    lastDebounceTime = currentMillis;
    return true;
  }
  else if (!currentState)
  {
    pressedFlag = false;
  }
  return false;
}

void appendInput(char c)
{
  if (inputIndex < MAX_PIN_LENGTH)
  {
    inputBuffer[inputIndex++] = c;
    inputBuffer[inputIndex] = '\0';
    lastPinInputTime = millis();
  }
  lastActivityTime = millis();
}

void resetInputBuffer()
{
  inputIndex = 0;
  memset(inputBuffer, 0, sizeof(inputBuffer));
}

void handleButtons()
{
  static uint32_t lastDebounceTimeUp = 0;
  static uint32_t lastDebounceTimeDown = 0;
  const uint32_t debounceDelay = 50;

  bool upState = digitalRead(BUTTON_UP_PIN) == LOW;
  bool downState = digitalRead(BUTTON_DOWN_PIN) == LOW;
  uint32_t currentMillis = millis();

  if (inPinSetup)
  {
    if (debounceButton(upState, buttonUpPressed, lastDebounceTimeUp, debounceDelay, currentMillis))
      appendInput('u');
    if (debounceButton(downState, buttonDownPressed, lastDebounceTimeDown, debounceDelay, currentMillis))
      appendInput('d');

    if (inputIndex > 0 && (currentMillis - lastPinInputTime) > PIN_SETUP_TIMEOUT)
    {
      strcpy(devicePin, inputBuffer);
      devicePinLength = inputIndex;
      pinSet = true;
      savePin();
      locked = true;
      inPinSetup = false;
      resetInputBuffer();
    }
  }
  else if (locked)
  {
    auto tryUnlock = [&](char c)
    {
      if (inputIndex < devicePinLength)
        inputBuffer[inputIndex++] = c;
      if (inputIndex == devicePinLength)
      {
        inputBuffer[inputIndex] = '\0';
        if (strncmp(inputBuffer, devicePin, devicePinLength) == 0)
        {
          locked = false;
          failedAttempts = 0;
        }
        else
        {
          pinError = true;
          pinErrorTime = currentMillis;
          failedAttempts++;
          if (failedAttempts >= MAX_FAILED_ATTEMPTS)
          {
            keysCount = 0;
            saveKeys();
            memset(devicePin, 0, sizeof(devicePin));
            devicePinLength = 0;
            pinSet = false;
            if (InternalFS.begin() && InternalFS.exists(PIN_FILENAME))
              InternalFS.remove(PIN_FILENAME);
            locked = false;
            inPinSetup = true;
          }
        }
        resetInputBuffer();
      }
      lastActivityTime = currentMillis;
    };

    if (debounceButton(upState, buttonUpPressed, lastDebounceTimeUp, debounceDelay, currentMillis))
      tryUnlock('u');
    if (debounceButton(downState, buttonDownPressed, lastDebounceTimeDown, debounceDelay, currentMillis))
      tryUnlock('d');
  }
  else
  {
    if (debounceButton(upState, buttonUpPressed, lastDebounceTimeUp, debounceDelay, currentMillis))
    {
      if (keysCount > 0)
      {
        selectedKeyIndex = (selectedKeyIndex + keysCount - 1) % keysCount;
      }
      lastActivityTime = currentMillis;
    }
    if (debounceButton(downState, buttonDownPressed, lastDebounceTimeDown, debounceDelay, currentMillis))
    {
      if (keysCount > 0)
      {
        selectedKeyIndex = (selectedKeyIndex + 1) % keysCount;
      }
      lastActivityTime = currentMillis;
    }
  }
}

// Serial

void processSerialInput()
{
  static String serialLine;
  while (Serial.available())
  {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r')
    {
      if (serialLine.length() > 0)
      {
        serialLine.trim();
        if (serialLine.startsWith("add "))
        {
          String cmd = serialLine.substring(4);
          int spaceIndex = cmd.indexOf(' ');
          if (spaceIndex > 0)
          {
            String username = cmd.substring(0, spaceIndex);
            String secret = cmd.substring(spaceIndex + 1);
            username.trim();
            secret.trim();
            if (username.length() > 15 || secret.length() > 32)
            {
              Serial.println("Error: username or secret too long");
            }
            else if (keysCount >= MAX_KEYS)
            {
              Serial.println("Max keys reached");
            }
            else
            {
              if (isDuplicateKey(username.c_str(), secret.c_str()))
              {
                Serial.println("Duplicate key, not added.");
              }
              else
              {
                username.toCharArray(keys[keysCount].username, sizeof(keys[keysCount].username));
                secret.toCharArray(keys[keysCount].base32secret, sizeof(keys[keysCount].base32secret));
                keysCount++;
                saveKeys();
                Serial.print("Added key: ");
                Serial.println(username);
              }
            }
          }
          else
          {
            Serial.println("Invalid add command format");
          }
        }
        else if (serialLine == "factoryreset")
        {
          if (InternalFS.begin())
          {
            if (InternalFS.exists(PIN_FILENAME))
              InternalFS.remove(PIN_FILENAME);
            if (InternalFS.exists(KEYS_FILENAME))
              InternalFS.remove(KEYS_FILENAME);
            pinSet = false;
            locked = true;
            keysCount = 0;
            memset(devicePin, 0, sizeof(devicePin));
            memset(keys, 0, sizeof(keys));
            Serial.println("Factory reset done, rebooting...");
            delay(100);
            NVIC_SystemReset();
          }
          else
          {
            Serial.println("FS mount failed");
          }
        }
        else if (serialLine == "dfu")
        {
          NRF_POWER->GPREGRET = DFU_MAGIC_UF2_RESET;
          NVIC_SystemReset();
        }
        else if (serialLine.startsWith("del "))
        {
          int index = serialLine.substring(4).toInt();
          if (index >= 0 && index < keysCount)
          {
            Serial.printf("Deleting key %d: %s\n", index, keys[index].username);
            for (int i = index; i < keysCount - 1; i++)
              keys[i] = keys[i + 1];
            keysCount--;
            saveKeys();
            Serial.println("Key deleted");
          }
          else
          {
            Serial.println("Invalid key index");
          }
        }
        else if (serialLine == "list")
        {
          if (locked)
          {
            Serial.println("Device locked. Unlock first.");
          }
          else
          {
            Serial.printf("Keys (%d):\n", keysCount);
            for (int i = 0; i < keysCount; i++)
            {
              Serial.printf("%d: %s\n", i, keys[i].username);
            }
          }
        }
        else if (serialLine == "clear")
        {
          keysCount = 0;
          saveKeys();
          Serial.println("Keys cleared");
        }
        else if (serialLine == "pinsetup")
        {
          inPinSetup = true;
          resetInputBuffer();
          lastPinInputTime = millis();
          Serial.println("Enter new PIN by buttons (timeout 10s)");
        }
        else if (serialLine == "lock")
        {
          locked = true;
          Serial.println("Device locked");
        }
        else
        {
          Serial.println("Unknown command");
        }
        serialLine = "";
      }
    }
    else
    {
      serialLine += c;
    }
  }
}

void processBleInput()
{
  while (Serial.available())
  {
    char c = Serial.read();
    bleuart.write(c);
  }

  while (bleuart.available())
  {
    char c = bleuart.read();
    Serial.write(c);
  }
}

void BLE(void)
{
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  bledis.setManufacturer("ICantMakeThings");
  bledis.setModel("NiceTOTP");
  bledis.begin();
  bleuart.begin();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

// Sleep n' stuf

void configureWakeupButtons()
{
  nrf_gpio_cfg_sense_input(BUTTON_UP_PIN, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  nrf_gpio_cfg_sense_input(BUTTON_DOWN_PIN, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
}

void fadeOutDisplay() {
  for (int contrast = 200; contrast >= 0; contrast -= 15) {
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(contrast);
    delay(60);
  }
  display.ssd1306_command(SSD1306_DISPLAYOFF);
}

void enterUltraSleep()
{
  Serial.println("Entering ultra sleep...");
  fadeOutDisplay();
  display.clearDisplay();
  display.display();
  delay(100);
  NRF_POWER->SYSTEMOFF = 1;
  while (true)
  {
  }
}

void setup()
{

  pinMode(PIN_013, OUTPUT);
  digitalWrite(PIN_013, HIGH);
  delay(1000);

  Serial.begin(115200);

  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println("SSD1306 init failed");
    while (1) {}
  }
  display.display();

  if (!rtc.begin())
  {
    Serial.println("RTC init failed");
    while (1) {}
  }

  configureWakeupButtons();

  loadPin();
  loadKeys();

  if (!pinSet)
  {
    inPinSetup = true;
    resetInputBuffer();
    lastPinInputTime = millis();
  }

  // BLE();

  locked = pinSet;
  lastActivityTime = millis();
}

void loop()
{
  handleButtons();
  processSerialInput();

  /*
  // Took part from: https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/libraries/Bluefruit52Lib/examples/Peripheral/bleuart/bleuart.ino
  while (Serial.available())
  {
    // Delay to wait for enough input, since we have a limited transmission buffer
    delay(2);

    uint8_t buf[64];
    int count = Serial.readBytes(buf, sizeof(buf));
    bleuart.write(buf, count);
  }

  // Forward from BLEUART to HW Serial
  while (bleuart.available())
  {
    uint8_t ch;
    ch = (uint8_t)bleuart.read();
    Serial.write(ch);
  }
*/
  displayCode();

  if ((millis() - lastActivityTime) > INACTIVITY_TIMEOUT && !inPinSetup)
  {
    enterUltraSleep();
  }

  delay(50);
}