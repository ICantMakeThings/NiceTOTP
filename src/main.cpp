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

using namespace Adafruit_LittleFS_Namespace;

RTC_DS3231 rtc;

const uint8_t lockIcon8x8[] PROGMEM = {
    0b00111100, //   ████
    0b01100110, //  ██  ██
    0b01000010, //  █    █
    0b01111110, //  ██████
    0b01111110, //  ██████
    0b01111110, //  ██████
    0b01111110, //  ██████
    0b00111100  //   ████
};

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define BUTTON_UP_PIN PIN_011
#define BUTTON_DOWN_PIN PIN_100

const int MAX_PIN_LENGTH = 20;
const char *PIN_FILENAME = "/pin.dat";
const char *KEYS_FILENAME = "/keys.dat";

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

#define MAX_KEYS 50
KeyEntry keys[MAX_KEYS];
int keysCount = 0;

bool buttonUpPressed = false;
bool buttonDownPressed = false;
int selectedKeyIndex = 0;

TOTP totp;

unsigned long lastPinInputTime = 0;
const unsigned long pinSetupTimeout = 10000;

void savePin()
{
  if (!InternalFS.begin())
  {
    Serial.println("Failed to mount FS");
    return;
  }
  File f = InternalFS.open(PIN_FILENAME, FILE_O_WRITE);
  if (!f)
  {
    Serial.println("Failed to open pin file for writing");
    return;
  }
  uint8_t len = (uint8_t)devicePinLength;
  f.write(&len, 1);
  f.write((uint8_t *)devicePin, len);
  f.close();
  Serial.print("PIN saved, length=");
  Serial.println(devicePinLength);
}

bool loadPin()
{
  if (!InternalFS.begin())
  {
    Serial.println("Failed to mount FS");
    return false;
  }
  if (!InternalFS.exists(PIN_FILENAME))
    return false;

  File f = InternalFS.open(PIN_FILENAME, FILE_O_READ);
  if (!f)
  {
    Serial.println("Failed to open pin file");
    return false;
  }
  if (f.size() < 1)
  {
    f.close();
    return false;
  }
  uint8_t len;
  f.read(&len, 1);
  if (len < 1 || len > MAX_PIN_LENGTH)
  {
    f.close();
    return false;
  }
  if (f.size() != (len + 1))
  {
    f.close();
    return false;
  }

  f.read((uint8_t *)devicePin, len);
  f.close();

  devicePin[len] = 0;
  devicePinLength = len;
  pinSet = true;

  Serial.print("PIN length= ");
  Serial.println(devicePinLength);
  return true;
}

void loadKeys()
{
  if (!InternalFS.begin())
  {
    Serial.println("Failed to mount FS");
    return;
  }
  if (!InternalFS.exists(KEYS_FILENAME))
    return;

  File f = InternalFS.open(KEYS_FILENAME, FILE_O_READ);
  if (!f)
  {
    Serial.println("Failed to open keys file");
    return;
  }
  int fileSize = f.size();
  if (fileSize % sizeof(KeyEntry) != 0)
  {
    Serial.println("Invalid keys file size");
    f.close();
    return;
  }
  keysCount = fileSize / sizeof(KeyEntry);
  if (keysCount > MAX_KEYS)
    keysCount = MAX_KEYS;

  f.read((uint8_t *)keys, keysCount * sizeof(KeyEntry));
  f.close();

  Serial.printf("Loaded %d keys\n", keysCount);
}

void saveKeys()
{
  if (!InternalFS.begin())
  {
    Serial.println("Failed to mount FS");
    return;
  }
  if (keysCount == 0)
  {
    if (InternalFS.exists(KEYS_FILENAME))
    {
      InternalFS.remove(KEYS_FILENAME);
      Serial.println("Keys file deleted");
    }
    return;
  }

  File f = InternalFS.open(KEYS_FILENAME, FILE_O_WRITE);
  if (!f)
  {
    Serial.println("Failed to open keys file for writing");
    return;
  }
  f.write((uint8_t *)keys, keysCount * sizeof(KeyEntry));
  f.close();
  Serial.println("Saved!");
}

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
    {
      display.print("*");
    }
  }
  else if (locked)
  {
    display.clearDisplay();
    display.drawBitmap(0, 0, lockIcon8x8, 8, 8, WHITE);
    char masked[devicePinLength + 1];
    for (int i = 0; i < inputIndex; i++)
    {
      masked[i] = '*';
    }
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

    display.display();
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
      if (rawCode != nullptr && strlen(rawCode) == 6)
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
      int16_t y = ((SCREEN_HEIGHT + h) / 2) - 1;
      display.setCursor(x, y);
      display.print(formattedCode);

      display.setFont(nullptr);
      display.setTextSize(1);

      display.setTextColor(SSD1306_WHITE);

      const char *user = keys[selectedKeyIndex].username;
      char displayUser[18];
      strncpy(displayUser, user, 17);
      displayUser[17] = '\0';

      int16_t ux, uy;
      uint16_t uw, uh;
      display.getTextBounds(displayUser, 0, 0, &ux, &uy, &uw, &uh);
      if (uw > SCREEN_WIDTH)
      {
        strncpy(displayUser, user, 13);
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
      {
        display.fillRoundRect(1, barY + 1, fillWidth, barHeight - 2, 1, SSD1306_WHITE);
      }
    }
  }

  display.display();
}

void handleButtons()
{
  static uint32_t lastDebounceTime = 0;
  const uint32_t debounceDelay = 50;

  bool upState = digitalRead(BUTTON_UP_PIN) == LOW;
  bool downState = digitalRead(BUTTON_DOWN_PIN) == LOW;

  if (inPinSetup)
  {
    if (upState && !buttonUpPressed && millis() - lastDebounceTime > debounceDelay)
    {
      buttonUpPressed = true;
      lastDebounceTime = millis();

      if (inputIndex < MAX_PIN_LENGTH)
      {
        inputBuffer[inputIndex++] = 'u';
        inputBuffer[inputIndex] = 0;
        lastPinInputTime = millis();
        Serial.print("PIN so far: ");
        Serial.println(inputBuffer);
      }
    }
    else if (!upState)
    {
      buttonUpPressed = false;
    }

    if (downState && !buttonDownPressed && millis() - lastDebounceTime > debounceDelay)
    {
      buttonDownPressed = true;
      lastDebounceTime = millis();

      if (inputIndex < MAX_PIN_LENGTH)
      {
        inputBuffer[inputIndex++] = 'd';
        inputBuffer[inputIndex] = 0;
        lastPinInputTime = millis();
        Serial.print("PIN so far: ");
        Serial.println(inputBuffer);
      }
    }
    else if (!downState)
    {
      buttonDownPressed = false;
    }

    if (inputIndex > 0 && (millis() - lastPinInputTime) > pinSetupTimeout)
    {
      Serial.print("PIN entry done, saving: ");
      Serial.println(inputBuffer);

      strcpy(devicePin, inputBuffer);
      devicePinLength = inputIndex;
      pinSet = true;
      savePin();
      locked = true;
      inPinSetup = false;
      inputIndex = 0;
      memset(inputBuffer, 0, sizeof(inputBuffer));
      Serial.println("PIN saved, device locked.");
    }
  }
  else if (locked)
  {
    if (upState && !buttonUpPressed && millis() - lastDebounceTime > debounceDelay)
    {
      buttonUpPressed = true;
      lastDebounceTime = millis();

      if (inputIndex < devicePinLength)
      {
        inputBuffer[inputIndex++] = 'u';
      }
      if (inputIndex == devicePinLength)
      {
        inputBuffer[inputIndex] = 0;
        if (strncmp(inputBuffer, devicePin, devicePinLength) == 0)
        {
          locked = false;
          Serial.println("Unlocked!");
        }
        else
        {
          Serial.println("AYOO? Tranna steal???");
        }
        memset(inputBuffer, 0, sizeof(inputBuffer));
        inputIndex = 0;
      }
    }
    else if (!upState)
    {
      buttonUpPressed = false;
    }

    if (downState && !buttonDownPressed && millis() - lastDebounceTime > debounceDelay)
    {
      buttonDownPressed = true;
      lastDebounceTime = millis();

      if (inputIndex < devicePinLength)
      {
        inputBuffer[inputIndex++] = 'd';
      }
      if (inputIndex == devicePinLength)
      {
        inputBuffer[inputIndex] = 0;
        if (strncmp(inputBuffer, devicePin, devicePinLength) == 0)
        {
          locked = false;
          Serial.println("Unlocked!");
        }
        else
        {
          Serial.println("AYOO? Tranna steal???");
        }
        memset(inputBuffer, 0, sizeof(inputBuffer));
        inputIndex = 0;
      }
    }
    else if (!downState)
    {
      buttonDownPressed = false;
    }
  }
  else
  {
    if (upState && !buttonUpPressed && millis() - lastDebounceTime > debounceDelay)
    {
      buttonUpPressed = true;
      lastDebounceTime = millis();
      if (keysCount > 0)
      {
        selectedKeyIndex--;
        if (selectedKeyIndex < 0)
          selectedKeyIndex = keysCount - 1;
      }
    }
    else if (!upState)
    {
      buttonUpPressed = false;
    }

    if (downState && !buttonDownPressed && millis() - lastDebounceTime > debounceDelay)
    {
      buttonDownPressed = true;
      lastDebounceTime = millis();
      if (keysCount > 0)
      {
        selectedKeyIndex++;
        if (selectedKeyIndex >= keysCount)
          selectedKeyIndex = 0;
      }
    }
    else if (!downState)
    {
      buttonDownPressed = false;
    }
  }
}

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

        // To add keys: add <username> <base32secret>
        // dont add the < >
        if (serialLine.startsWith("add "))
        {
          serialLine.remove(0, 4);
          int spaceIndex = serialLine.indexOf(' ');
          if (spaceIndex > 0)
          {
            String username = serialLine.substring(0, spaceIndex);
            String secret = serialLine.substring(spaceIndex + 1);

            if (username.length() > 15 || secret.length() > 32)
            {
              Serial.println("Error: username or secret too long");
            }
            else if (keysCount >= MAX_KEYS)
            {
              Serial.println("Wachu need more than 50 keys 4? (Change MAX_KEYS in code)");
            }
            else
            {
              username.toCharArray(keys[keysCount].username, 16);
              secret.toCharArray(keys[keysCount].base32secret, 33);
              keysCount++;
              saveKeys();
              Serial.print("Added key: ");
              Serial.println(username);
            }
          }
          else
          {
            Serial.println("Invalid add command. Format: add username base32secret");
          }
        }
        // Set Unix time: setunixtime 1700000000
        else if (serialLine.startsWith("setunixtime "))
        {
          String epochStr = serialLine.substring(11);
          unsigned long epoch = epochStr.toInt();
          if (epoch == 0)
          {
            Serial.println("Hmm, Invalid.");
          }
          else
          {
            rtc.adjust(DateTime(epoch));
            Serial.println("New time set!");
          }
        }
        // factoryreset to factoryreset
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

            Serial.println("Factory reset done.. Please set new PIN.");
          }
          else
          {
            Serial.println("FS mount failed");
          }
          serialLine = "";
          return;
        }

        else if (serialLine.startsWith("del "))
        {
          int index = serialLine.substring(4).toInt();
          if (index >= 0 && index < keysCount)
          {
            Serial.printf("Deleting key %d: %s\n", index, keys[index].username);
            for (int i = index; i < keysCount - 1; i++)
            {
              keys[i] = keys[i + 1];
            }
            keysCount--;
            saveKeys();
            Serial.println("Key deleted");
          }
          else
          {
            Serial.println("Invalid key index");
          }
        }

        if (serialLine == "list")
        {
          if (locked)
          {
            Serial.println("Device locked. Unlock to use this command.");
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
          inputIndex = 0;
          memset(inputBuffer, 0, sizeof(inputBuffer));
          lastPinInputTime = millis();
          Serial.println("Enter new PIN by pressing buttons. Timeout 10s.");
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

void setup(){
  Serial.begin(115200);

  Serial.println("Starting...");

  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println("SSD1306 allocation failed");
    while (1)
      ;
  }
  display.clearDisplay();
  display.display();

  if (!rtc.begin())
  {
    Serial.println("Couldn't find RTC");
    while (1)
      ;
  }

  loadPin();
  loadKeys();

  if (!pinSet) {
    inPinSetup = true;
    inputIndex = 0;
    memset(inputBuffer, 0, sizeof(inputBuffer));
    lastPinInputTime = millis();  // Ensure timeout starts
  }

  locked = pinSet;
}

void loop()
{
  handleButtons();
  processSerialInput();
  displayCode();
  delay(200);
}