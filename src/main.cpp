// Hello! This is a nRF52840 based TOTP with a SSD... something display.
// Has a pin to lock your stuff, and more soon
//
// commands include:
// setunixtime
// manualcalibration <aging offset>
// clearcalibration
// add <username> <base32secret>
// factoryreset (power cycle the device)
// list
// del <GetTheIDFromListCommand>
// clear
// pinsetup
// lock
// NEW << A app to configure
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
#include <Adafruit_nRFCrypto.h>
#include <AES.h>
#include <GCM.h>
#include <SHA256.h>
#include <hal/nrf_power.h>
#include "nrf_nvic.h"

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

#define BUTTON_UP_PIN PIN_029
#define BUTTON_DOWN_PIN PIN_031

const unsigned long INACTIVITY_TIMEOUT = 60000;
const unsigned long CONFIGURATOR_TIMEOUT = 5000;
const int MAX_PIN_LENGTH = 20;
const int MIN_PIN_LENGTH = 4;
const int MAX_KEYS = 50;
const int MAX_FAILED_ATTEMPTS = 10;
const uint32_t PBKDF2_ITERATIONS = 50000;
const char *PIN_SEARCH_ESTIMATES[17] = {
    "~128ms", "~1.3s", "~12.8s", "~2m", "~21m", "~4h", "~1d", "~15d", "~148d",
    "~4y", "~41y", "~406y", "~4.1ky", "~40.6ky", "~406ky", "~4.1My", "~40.6My"};
const uint32_t BUTTON_HOLD_NEXT_MS = 350;
const uint32_t BUTTON_HOLD_SELECT_MS = 400;
const uint32_t MENU_HOLD_MS = 400;

const char *PIN_FILENAME = "/pin.dat";
const char *KEYS_FILENAME = "/keys.dat";
const char *VAULT_FILENAME = "/vault.dat";
const char *ATTEMPTS_FILENAME = "/attempts.dat";
const char *CLOCK_DISPLAY_FILENAME = "/clock.cfg";
const char *CALIBRATION_FILENAME = "/calibration.dat";
const uint32_t CALIBRATION_INTERVAL = 30UL * 24UL * 60UL * 60UL;
const uint8_t DS3231_ADDRESS = 0x68;
const uint8_t DS3231_AGING_REGISTER = 0x10;

uint8_t failedAttempts = 0;
uint32_t lockoutUntilMillis = 0;
uint32_t lockoutDurationMillis = 0;
bool lockoutActive = false;

unsigned long lastActivityTime = 0;
unsigned long lastConfiguratorHeartbeat = 0;

char passcodeInput[MAX_PIN_LENGTH + 1] = {0};
uint8_t inputIndex = 0;
uint8_t selectedDigit = 0;
char pendingPasscode[MAX_PIN_LENGTH + 1] = {0};
uint8_t pendingPasscodeLength = 0;
char legacyPasscode[MAX_PIN_LENGTH + 1] = {0};
bool legacyMode = false;
bool legacyDataLoaded = false;

bool locked = true;
bool inPinSetup = false;
bool confirmingPasscode = false;
bool setupMismatch = false;
bool changingPasscode = false;
bool verifyingCurrentPasscode = false;
bool hashingPasscode = false;
bool hardwareCryptoReady = false;
uint32_t setupMessageUntil = 0;
bool menuActive = false;
uint8_t menuSelection = 0;
bool clockSettingsActive = false;
uint8_t clockSettingsSelection = 0;
bool editingClockOffset = false;
uint32_t wrongPinFlashStart = 0;
bool wrongPinFlashActive = false;
bool vaultPresent = false;
bool vaultUnlocked = false;
bool storageKeyReady = false;
uint8_t vaultSalt[16] = {0};
uint8_t storageKey[32] = {0};
uint8_t vaultFormatVersion = 3;
bool allowNonceInitialization = false;
uint32_t originalSetTime = 0;
int8_t agingOffset = 0;
bool calibrationLocked = false;
int16_t utcOffsetMinutes = 0;
bool clock24Hour = true;
uint32_t clockTextUpdatedAt = 0;
bool clockTextReady = false;
char cachedClockText[8] = {0};

struct KeyEntry
{
  char username[32];
  char base32secret[128];
};

KeyEntry keys[MAX_KEYS];
int keysCount = 0;
const size_t VAULT_CALIBRATION_OFFSET = 1 + MAX_KEYS * sizeof(KeyEntry);
const size_t VAULT_CLOCK_SETTINGS_OFFSET = VAULT_CALIBRATION_OFFSET + sizeof(uint32_t) + sizeof(int8_t) + 1;
const size_t VAULT_V2_PAYLOAD_SIZE = VAULT_CLOCK_SETTINGS_OFFSET;
uint8_t vaultPayload[VAULT_V2_PAYLOAD_SIZE + sizeof(utcOffsetMinutes) + 1] = {0};
bool saveKeys();
bool vaultFileIsComplete(const char *path);
bool saveClockDisplaySettings();
void loadClockDisplaySettings();

int selectedKeyIndex = 0;

TOTP totp;

void secureZero(void *memory, size_t length)
{
  volatile uint8_t *bytes = (volatile uint8_t *)memory;
  while (length-- > 0)
    *bytes++ = 0;
}

bool usbExposed = false;
bool usbExposureInitialized = false;

void setUSBExposure(bool exposed)
{
  if (usbExposureInitialized && exposed == usbExposed)
    return;

  if (exposed)
  {
    TinyUSBDevice.attach();
    delay(50);
  }
  else
  {
    TinyUSBDevice.detach();
    delay(50);
  }

  usbExposed = exposed;
  usbExposureInitialized = true;
}

void secureClearString(String &value)
{
  for (unsigned int i = 0; i < value.length(); i++)
    value.setCharAt(i, '\0');
  value = "";
}

bool fillCryptoRandom(uint8_t *output, size_t length)
{
  NRF_RNG->CONFIG = RNG_CONFIG_DERCEN_Enabled;
  NRF_RNG->TASKS_START = 1;
  for (size_t i = 0; i < length; i++)
  {
    NRF_RNG->EVENTS_VALRDY = 0;
    uint32_t timeout = 1000000;
    while (!NRF_RNG->EVENTS_VALRDY && timeout > 0)
      timeout--;
    if (timeout == 0)
    {
      NRF_RNG->TASKS_STOP = 1;
      secureZero(output, i);
      return false;
    }
    output[i] = (uint8_t)NRF_RNG->VALUE;
  }
  NRF_RNG->TASKS_STOP = 1;
  return true;
}

bool computeHmacSha256(const uint8_t *key, size_t keyLength, const uint8_t *data,
                       size_t dataLength, uint8_t output[32])
{
  if (hardwareCryptoReady)
  {
    nRFCrypto_Hmac hmac;
    uint32_t digest[16] = {0};
    bool okay = hmac.begin(CRYS_HASH_SHA256_mode, (uint8_t *)key, (uint16_t)keyLength) &&
                hmac.update((uint8_t *)data, dataLength) && hmac.end(digest) == 32;
    if (okay)
    {
      memcpy(output, digest, 32);
      secureZero(digest, sizeof(digest));
      return true;
    }
    secureZero(digest, sizeof(digest));
  }

  SHA256 hash;
  hash.resetHMAC(key, keyLength);
  hash.update(data, dataLength);
  hash.finalizeHMAC(key, keyLength, output, 32);
  hash.clear();
  return true;
}

void displayCode();

bool deriveVaultKey(const uint8_t *passcode, size_t passcodeLength, const uint8_t salt[16], uint8_t output[32])
{
  if (passcodeLength < MIN_PIN_LENGTH || passcodeLength > MAX_PIN_LENGTH)
    return false;

  uint8_t block[20];
  uint8_t u[32];
  uint8_t result[32];
  memcpy(block, salt, 16);
  block[16] = 0;
  block[17] = 0;
  block[18] = 0;
  block[19] = 1;

  if (!computeHmacSha256(passcode, passcodeLength, block, sizeof(block), u))
  {
    secureZero(block, sizeof(block));
    secureZero(u, sizeof(u));
    secureZero(result, sizeof(result));
    return false;
  }
  memcpy(result, u, sizeof(result));
  for (uint32_t iteration = 1; iteration < PBKDF2_ITERATIONS; iteration++)
  {
    if (!computeHmacSha256(passcode, passcodeLength, u, sizeof(u), u))
    {
      secureZero(block, sizeof(block));
      secureZero(u, sizeof(u));
      secureZero(result, sizeof(result));
      return false;
    }
    for (size_t i = 0; i < sizeof(result); i++)
      result[i] ^= u[i];
    if ((iteration & 0xFF) == 0)
    {
      displayCode();
      yield();
    }
  }
  memcpy(output, result, sizeof(result));
  secureZero(block, sizeof(block));
  secureZero(u, sizeof(u));
  secureZero(result, sizeof(result));
  return true;
}

bool deriveVaultEncryptionKey(const uint8_t *passcode, size_t passcodeLength,
                              const uint8_t salt[16], uint8_t version, uint8_t output[32])
{
  uint8_t baseKey[32];
  if (!deriveVaultKey(passcode, passcodeLength, salt, baseKey))
  {
    secureZero(baseKey, sizeof(baseKey));
    return false;
  }
  if (version == 1)
  {
    memcpy(output, baseKey, sizeof(baseKey));
    secureZero(baseKey, sizeof(baseKey));
    return true;
  }

  static const uint8_t context[] = "NiceTOTP vault AES-256-GCM key v2";
  uint8_t version2Key[sizeof(baseKey)];
  bool okay = computeHmacSha256(baseKey, sizeof(baseKey), context, sizeof(context) - 1, version2Key);
  if (okay && version >= 3)
  {
    static const uint8_t context3[] = "NiceTOTP vault AES-256-GCM settings key v3";
    okay = computeHmacSha256(version2Key, sizeof(version2Key), context3, sizeof(context3) - 1, output);
  }
  else if (okay)
  {
    memcpy(output, version2Key, sizeof(version2Key));
  }
  secureZero(version2Key, sizeof(version2Key));
  secureZero(baseKey, sizeof(baseKey));
  return okay;
}

bool deriveVaultV2KeyFromBase(const uint8_t baseKey[32], uint8_t output[32])
{
  static const uint8_t context[] = "NiceTOTP vault AES-256-GCM key v2";
  return computeHmacSha256(baseKey, 32, context, sizeof(context) - 1, output);
}

bool deriveVaultV3KeyFromV2(const uint8_t version2Key[32], uint8_t output[32])
{
  static const uint8_t context3[] = "NiceTOTP vault AES-256-GCM settings key v3";
  return computeHmacSha256(version2Key, 32, context3, sizeof(context3) - 1, output);
}

void enableApprotect() //(qq5)
{
#if APPROTECT_ENABLED
  if (NRF_UICR->APPROTECT != 0)
  {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    while (NRF_NVMC->READY == 0)
    {
    }
    NRF_UICR->APPROTECT = 0;
    while (NRF_NVMC->READY == 0)
    {
    }
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    while (NRF_NVMC->READY == 0)
    {
    }
    NVIC_SystemReset();
  }
#endif
}

void saveCalibration()
{
  if (vaultUnlocked)
    saveKeys();
}

void loadCalibration()
{
  originalSetTime = 0;
  agingOffset = 0;
  calibrationLocked = false;
}

void loadLegacyCalibration()
{
  loadCalibration();
  if (!InternalFS.begin() || !InternalFS.exists(CALIBRATION_FILENAME))
    return;

  File file = InternalFS.open(CALIBRATION_FILENAME, FILE_O_READ);
  if (!file || file.size() != sizeof(originalSetTime) + sizeof(agingOffset) + 1)
  {
    if (file)
      file.close();
    return;
  }

  uint8_t lockedValue;
  file.read((uint8_t *)&originalSetTime, sizeof(originalSetTime));
  file.read((uint8_t *)&agingOffset, sizeof(agingOffset));
  file.read(&lockedValue, sizeof(lockedValue));
  calibrationLocked = lockedValue != 0;
  file.close();
}

int8_t readAgingOffset()
{
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write(DS3231_AGING_REGISTER);
  if (Wire.endTransmission() != 0 || Wire.requestFrom(DS3231_ADDRESS, (uint8_t)1) != 1)
    return agingOffset;
  return (int8_t)Wire.read();
}

bool writeAgingOffset(int8_t value)
{
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write(DS3231_AGING_REGISTER);
  Wire.write((uint8_t)value);
  return Wire.endTransmission() == 0;
}

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

void drawTopStatus(bool showBack, bool backSelected)
{
  if (showBack)
  {
    if (backSelected)
    {
      display.fillRoundRect(0, 0, 10, 7, 2, SSD1306_WHITE);
      display.drawLine(6, 0, 3, 3, SSD1306_BLACK);
      display.drawLine(3, 3, 6, 6, SSD1306_BLACK);
      display.drawFastHLine(3, 3, 5, SSD1306_BLACK);
    }
    else
    {
      display.drawLine(6, 0, 3, 3, SSD1306_WHITE);
      display.drawLine(3, 3, 6, 6, SSD1306_WHITE);
      display.drawFastHLine(3, 3, 5, SSD1306_WHITE);
    }
  }

  uint32_t nowMillis = millis();
  if (!clockTextReady || nowMillis - clockTextUpdatedAt >= 1000)
  {
    int64_t localEpoch = (int64_t)rtc.now().unixtime() + (int64_t)utcOffsetMinutes * 60;
    if (localEpoch < 0)
      localEpoch = 0;
    DateTime localTime((uint32_t)localEpoch);
    if (clock24Hour)
    {
      snprintf(cachedClockText, sizeof(cachedClockText), "%02u:%02u",
               (unsigned int)localTime.hour(), (unsigned int)localTime.minute());
    }
    else
    {
      uint8_t hour = localTime.hour() % 12;
      if (hour == 0)
        hour = 12;
      snprintf(cachedClockText, sizeof(cachedClockText), "%u:%02u%c", (unsigned int)hour,
               (unsigned int)localTime.minute(), localTime.hour() < 12 ? 'a' : 'p');
    }
    clockTextUpdatedAt = nowMillis;
    clockTextReady = true;
  }
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int16_t textX, textY;
  uint16_t textWidth, textHeight;
  display.getTextBounds(cachedClockText, 0, 0, &textX, &textY, &textWidth, &textHeight);
  display.setCursor(SCREEN_WIDTH - 22 - textWidth, 0);
  display.print(cachedClockText);
  drawBatteryIcon(readBatteryVoltage());
}

// Files

const uint8_t VAULT_MAGIC_V1[5] = {'N', 'T', 'V', '1', 1};
const uint8_t VAULT_MAGIC_V2[5] = {'N', 'T', 'V', '2', 2};
const uint8_t VAULT_MAGIC_V3[5] = {'N', 'T', 'V', '3', 3};
const size_t VAULT_HEADER_SIZE = 33;
const size_t VAULT_TAG_SIZE = 16;
const uint32_t LOCKOUT_DELAYS_MS[10] = {
    0, 0, 30000UL, 60000UL, 300000UL,
    600000UL, 1800000UL, 21600000UL, 86400000UL, 0};

bool saveAttemptState()
{
  if (!InternalFS.begin())
    return false;
  uint8_t record[5] = {'A', 'T', 1, failedAttempts, 0};
  record[4] = (uint8_t)(record[0] ^ record[1] ^ record[2] ^ record[3] ^ 0xA5);
  if (InternalFS.exists("/attempts.tmp"))
    InternalFS.remove("/attempts.tmp");
  File file = InternalFS.open("/attempts.tmp", FILE_O_WRITE);
  if (!file)
    return false;
  bool written = file.write(record, sizeof(record)) == sizeof(record);
  file.flush();
  file.close();
  if (!written)
  {
    InternalFS.remove("/attempts.tmp");
    return false;
  }
  if (InternalFS.exists(ATTEMPTS_FILENAME) && !InternalFS.remove(ATTEMPTS_FILENAME))
    return false;
  return InternalFS.rename("/attempts.tmp", ATTEMPTS_FILENAME);
}

void loadAttemptState()
{
  failedAttempts = 0;
  if (!InternalFS.begin())
    return;
  if (!InternalFS.exists(ATTEMPTS_FILENAME) && InternalFS.exists("/attempts.tmp"))
    InternalFS.rename("/attempts.tmp", ATTEMPTS_FILENAME);
  else if (InternalFS.exists(ATTEMPTS_FILENAME) && InternalFS.exists("/attempts.tmp"))
    InternalFS.remove("/attempts.tmp");
  if (!InternalFS.exists(ATTEMPTS_FILENAME))
    return;

  File file = InternalFS.open(ATTEMPTS_FILENAME, FILE_O_READ);
  if (!file)
    return;
  uint8_t record[5] = {0};
  bool valid = file.size() == sizeof(record) && file.read(record, sizeof(record)) == sizeof(record);
  file.close();
  if (valid && record[0] == 'A' && record[1] == 'T' && record[2] == 1 &&
      record[4] == (uint8_t)(record[0] ^ record[1] ^ record[2] ^ record[3] ^ 0xA5) &&
      record[3] <= MAX_FAILED_ATTEMPTS)
    failedAttempts = record[3];
}

bool saveClockDisplaySettings()
{
  if (!InternalFS.begin())
    return false;
  uint8_t record[6] = {'C', 'L', (uint8_t)utcOffsetMinutes,
                       (uint8_t)((uint16_t)utcOffsetMinutes >> 8),
                       (uint8_t)(clock24Hour ? 1 : 0), 0};
  record[5] = (uint8_t)(record[0] ^ record[1] ^ record[2] ^ record[3] ^ record[4] ^ 0xA5);
  if (InternalFS.exists("/clock.tmp"))
    InternalFS.remove("/clock.tmp");
  File file = InternalFS.open("/clock.tmp", FILE_O_WRITE);
  if (!file)
    return false;
  bool written = file.write(record, sizeof(record)) == sizeof(record);
  file.flush();
  file.close();
  if (!written)
  {
    InternalFS.remove("/clock.tmp");
    return false;
  }
  if (InternalFS.exists(CLOCK_DISPLAY_FILENAME) && !InternalFS.remove(CLOCK_DISPLAY_FILENAME))
    return false;
  return InternalFS.rename("/clock.tmp", CLOCK_DISPLAY_FILENAME);
}

void loadClockDisplaySettings()
{
  utcOffsetMinutes = 0;
  clock24Hour = true;
  clockTextReady = false;
  if (!InternalFS.begin())
    return;

  if (InternalFS.exists("/clock.tmp"))
  {
    File temporary = InternalFS.open("/clock.tmp", FILE_O_READ);
    uint8_t record[6] = {0};
    bool valid = temporary && temporary.size() == sizeof(record) &&
                 temporary.read(record, sizeof(record)) == sizeof(record) &&
                 record[0] == 'C' && record[1] == 'L' &&
                 record[5] == (uint8_t)(record[0] ^ record[1] ^ record[2] ^ record[3] ^ record[4] ^ 0xA5);
    if (temporary)
      temporary.close();
    if (valid)
    {
      if (InternalFS.exists(CLOCK_DISPLAY_FILENAME))
        InternalFS.remove(CLOCK_DISPLAY_FILENAME);
      InternalFS.rename("/clock.tmp", CLOCK_DISPLAY_FILENAME);
    }
    else
      InternalFS.remove("/clock.tmp");
    secureZero(record, sizeof(record));
  }

  if (!InternalFS.exists(CLOCK_DISPLAY_FILENAME))
    return;
  File file = InternalFS.open(CLOCK_DISPLAY_FILENAME, FILE_O_READ);
  if (!file)
    return;
  uint8_t record[6] = {0};
  bool valid = file.size() == sizeof(record) && file.read(record, sizeof(record)) == sizeof(record) &&
               record[0] == 'C' && record[1] == 'L' && record[4] <= 1 &&
               record[5] == (uint8_t)(record[0] ^ record[1] ^ record[2] ^ record[3] ^ record[4] ^ 0xA5);
  file.close();
  if (valid)
  {
    uint16_t rawOffset = (uint16_t)record[2] | ((uint16_t)record[3] << 8);
    int16_t offset = (int16_t)rawOffset;
    if (offset >= -840 && offset <= 840 && offset % 30 == 0)
    {
      utcOffsetMinutes = offset;
      clock24Hour = record[4] != 0;
      clockTextReady = false;
    }
  }
  secureZero(record, sizeof(record));
}

void recoverVaultFile()
{
  if (!InternalFS.begin())
    return;
  if (!InternalFS.exists(VAULT_FILENAME))
  {
    bool completeTemp = vaultFileIsComplete("/vault.tmp");
    if (completeTemp)
      InternalFS.rename("/vault.tmp", VAULT_FILENAME);
    if (!InternalFS.exists(VAULT_FILENAME) && InternalFS.exists("/vault.bak"))
      InternalFS.rename("/vault.bak", VAULT_FILENAME);
    else if (!completeTemp && !InternalFS.exists("/vault.bak") && InternalFS.exists("/vault.tmp"))
      InternalFS.remove("/vault.tmp");
  }
  vaultPresent = InternalFS.exists(VAULT_FILENAME);
  if (vaultPresent)
  {
    if (InternalFS.exists(PIN_FILENAME))
      InternalFS.remove(PIN_FILENAME);
    if (InternalFS.exists(KEYS_FILENAME))
      InternalFS.remove(KEYS_FILENAME);
    if (InternalFS.exists(CALIBRATION_FILENAME))
      InternalFS.remove(CALIBRATION_FILENAME);
  }
}

bool vaultFileIsComplete(const char *path)
{
  if (!InternalFS.exists(path))
    return false;
  File file = InternalFS.open(path, FILE_O_READ);
  if (!file)
    return false;
  uint8_t header[VAULT_HEADER_SIZE];
  bool readOkay = file.size() >= sizeof(header) && file.read(header, sizeof(header)) == sizeof(header);
  uint8_t version = readOkay ? header[4] : 0;
  bool knownVersion = readOkay &&
                      ((version == 1 && memcmp(header, VAULT_MAGIC_V1, sizeof(VAULT_MAGIC_V1)) == 0) ||
                       (version == 2 && memcmp(header, VAULT_MAGIC_V2, sizeof(VAULT_MAGIC_V2)) == 0) ||
                       (version == 3 && memcmp(header, VAULT_MAGIC_V3, sizeof(VAULT_MAGIC_V3)) == 0));
  size_t payloadSize = version >= 3 ? sizeof(vaultPayload) : VAULT_V2_PAYLOAD_SIZE;
  bool complete = knownVersion && file.size() == sizeof(header) + payloadSize + VAULT_TAG_SIZE;
  file.close();
  secureZero(header, sizeof(header));
  return complete;
}

bool readV2VaultNonce(const char *path, uint8_t salt[16], uint8_t nonce[12])
{
  if (!InternalFS.exists(path))
    return false;
  File file = InternalFS.open(path, FILE_O_READ);
  if (!file)
    return false;
  uint8_t header[VAULT_HEADER_SIZE];
  bool readOkay = file.size() >= sizeof(header) && file.read(header, sizeof(header)) == sizeof(header);
  bool valid = readOkay &&
               (memcmp(header, VAULT_MAGIC_V2, sizeof(VAULT_MAGIC_V2)) == 0 ||
                memcmp(header, VAULT_MAGIC_V3, sizeof(VAULT_MAGIC_V3)) == 0);
  file.close();
  if (!valid)
  {
    secureZero(header, sizeof(header));
    return false;
  }
  memcpy(salt, header + sizeof(VAULT_MAGIC_V2), 16);
  memcpy(nonce, header + sizeof(VAULT_MAGIC_V2) + 16, 12);
  secureZero(header, sizeof(header));
  return true;
}

bool incrementVaultNonce(uint8_t nonce[12])
{
  for (int index = 11; index >= 0; index--)
  {
    nonce[index]++;
    if (nonce[index] != 0)
      return true;
  }
  return false;
}

bool allocateVaultNonce(uint8_t nonce[12])
{
  uint8_t savedSalt[16];
  uint8_t savedNonce[12];
  bool hasCurrent = readV2VaultNonce(VAULT_FILENAME, savedSalt, savedNonce) &&
                    memcmp(savedSalt, vaultSalt, sizeof(vaultSalt)) == 0;
  uint8_t temporarySalt[16];
  uint8_t temporaryNonce[12];
  bool hasTemporary = readV2VaultNonce("/vault.tmp", temporarySalt, temporaryNonce) &&
                      memcmp(temporarySalt, vaultSalt, sizeof(vaultSalt)) == 0;

  if (hasCurrent || hasTemporary)
  {
    if (hasCurrent && hasTemporary)
      memcpy(nonce, memcmp(savedNonce, temporaryNonce, sizeof(savedNonce)) >= 0 ? savedNonce : temporaryNonce, 12);
    else
      memcpy(nonce, hasCurrent ? savedNonce : temporaryNonce, 12);
    secureZero(savedSalt, sizeof(savedSalt));
    secureZero(savedNonce, sizeof(savedNonce));
    secureZero(temporarySalt, sizeof(temporarySalt));
    secureZero(temporaryNonce, sizeof(temporaryNonce));
    return incrementVaultNonce(nonce);
  }

  secureZero(savedSalt, sizeof(savedSalt));
  secureZero(savedNonce, sizeof(savedNonce));
  secureZero(temporarySalt, sizeof(temporarySalt));
  secureZero(temporaryNonce, sizeof(temporaryNonce));
  return allowNonceInitialization && fillCryptoRandom(nonce, 12);
}

bool loadLegacyPasscode()
{
  if (!InternalFS.begin() || !InternalFS.exists(PIN_FILENAME))
    return false;
  File file = InternalFS.open(PIN_FILENAME, FILE_O_READ);
  if (!file || file.size() < 2)
  {
    if (file)
      file.close();
    return false;
  }
  uint8_t length = 0;
  bool valid = file.read(&length, 1) == 1 && length > 0 && length <= MAX_PIN_LENGTH &&
               file.size() == (size_t)length + 1 &&
               file.read((uint8_t *)legacyPasscode, length) == length;
  file.close();
  if (!valid)
  {
    secureZero(legacyPasscode, sizeof(legacyPasscode));
    return false;
  }
  legacyPasscode[length] = '\0';
  for (uint8_t i = 0; i < length; i++)
  {
    if (legacyPasscode[i] != 'u' && legacyPasscode[i] != 'd')
    {
      secureZero(legacyPasscode, sizeof(legacyPasscode));
      return false;
    }
  }
  return true;
}

bool loadLegacyKeys()
{
  keysCount = 0;
  memset(keys, 0, sizeof(keys));
  if (!InternalFS.begin() || !InternalFS.exists(KEYS_FILENAME))
    return true;
  File file = InternalFS.open(KEYS_FILENAME, FILE_O_READ);
  if (!file)
    return false;
  while (file.available() && keysCount < MAX_KEYS)
  {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      continue;
    int separator = line.lastIndexOf(' ');
    if (separator <= 0)
    {
      secureClearString(line);
      continue;
    }
    String username = line.substring(0, separator);
    String secret = line.substring(separator + 1);
    secret.replace(" ", "");
    if (username.length() >= sizeof(keys[keysCount].username) ||
        secret.length() >= sizeof(keys[keysCount].base32secret))
    {
      secureClearString(username);
      secureClearString(secret);
      secureClearString(line);
      continue;
    }
    username.toCharArray(keys[keysCount].username, sizeof(keys[keysCount].username));
    secret.toCharArray(keys[keysCount].base32secret, sizeof(keys[keysCount].base32secret));
    keysCount++;
    secureClearString(username);
    secureClearString(secret);
    secureClearString(line);
  }
  file.close();
  legacyDataLoaded = true;
  return true;
}

bool saveKeys()
{
  if (!vaultUnlocked || !storageKeyReady || keysCount < 0 || keysCount > MAX_KEYS || !InternalFS.begin())
    return false;
  uint8_t header[VAULT_HEADER_SIZE];
  const uint8_t *magic = vaultFormatVersion == 1 ? VAULT_MAGIC_V1 : (vaultFormatVersion == 2 ? VAULT_MAGIC_V2 : VAULT_MAGIC_V3);
  memcpy(header, magic, sizeof(VAULT_MAGIC_V3));
  memcpy(header + sizeof(VAULT_MAGIC_V3), vaultSalt, sizeof(vaultSalt));
  uint8_t *nonce = header + sizeof(VAULT_MAGIC_V3) + sizeof(vaultSalt);
  bool nonceReady = vaultFormatVersion == 1 ? fillCryptoRandom(nonce, 12) : allocateVaultNonce(nonce);
  if (!nonceReady)
  {
    secureZero(header, sizeof(header));
    return false;
  }

  size_t payloadLength = vaultFormatVersion >= 3 ? sizeof(vaultPayload) : VAULT_V2_PAYLOAD_SIZE;
  memset(vaultPayload, 0, payloadLength);
  vaultPayload[0] = (uint8_t)keysCount;
  if (keysCount > 0)
    memcpy(vaultPayload + 1, keys, (size_t)keysCount * sizeof(KeyEntry));
  memcpy(vaultPayload + VAULT_CALIBRATION_OFFSET, &originalSetTime, sizeof(originalSetTime));
  memcpy(vaultPayload + VAULT_CALIBRATION_OFFSET + sizeof(originalSetTime), &agingOffset, sizeof(agingOffset));
  vaultPayload[VAULT_CALIBRATION_OFFSET + sizeof(originalSetTime) + sizeof(agingOffset)] = calibrationLocked ? 1 : 0;
  if (vaultFormatVersion >= 3)
  {
    memcpy(vaultPayload + VAULT_CLOCK_SETTINGS_OFFSET, &utcOffsetMinutes, sizeof(utcOffsetMinutes));
    vaultPayload[VAULT_CLOCK_SETTINGS_OFFSET + sizeof(utcOffsetMinutes)] = clock24Hour ? 1 : 0;
  }
  GCM<AES256> cipher;
  uint8_t tag[VAULT_TAG_SIZE];
  bool encrypted = cipher.setKey(storageKey, sizeof(storageKey)) &&
                   cipher.setIV(nonce, 12);
  if (encrypted)
  {
    cipher.addAuthData(header, sizeof(header));
    cipher.encrypt(vaultPayload, vaultPayload, payloadLength);
    cipher.computeTag(tag, sizeof(tag));
  }
  if (!encrypted)
  {
    cipher.clear();
    secureZero(vaultPayload, payloadLength);
    secureZero(tag, sizeof(tag));
    secureZero(header, sizeof(header));
    return false;
  }

  if (InternalFS.exists("/vault.tmp"))
    InternalFS.remove("/vault.tmp");
  File file = InternalFS.open("/vault.tmp", FILE_O_WRITE);
  if (!file)
  {
    cipher.clear();
    secureZero(vaultPayload, payloadLength);
    secureZero(tag, sizeof(tag));
    secureZero(header, sizeof(header));
    return false;
  }
  bool written = file.write(header, sizeof(header)) == sizeof(header) &&
                 file.write(vaultPayload, payloadLength) == payloadLength &&
                 file.write(tag, sizeof(tag)) == sizeof(tag);
  file.flush();
  file.close();
  cipher.clear();
  secureZero(vaultPayload, payloadLength);
  secureZero(tag, sizeof(tag));
  if (!written)
  {
    InternalFS.remove("/vault.tmp");
    secureZero(header, sizeof(header));
    return false;
  }
  bool hadVault = InternalFS.exists(VAULT_FILENAME);
  if (InternalFS.exists(VAULT_FILENAME) && InternalFS.exists("/vault.bak"))
    InternalFS.remove("/vault.bak");
  if (hadVault && !InternalFS.rename(VAULT_FILENAME, "/vault.bak"))
  {
    secureZero(header, sizeof(header));
    return false;
  }
  if (!InternalFS.rename("/vault.tmp", VAULT_FILENAME))
  {
    if (hadVault)
      InternalFS.rename("/vault.bak", VAULT_FILENAME);
    secureZero(header, sizeof(header));
    return false;
  }
  if (hadVault)
    InternalFS.remove("/vault.bak");
  secureZero(header, sizeof(header));
  vaultPresent = true;
  return true;
}

bool unlockVault(const uint8_t *passcode, size_t passcodeLength)
{
  if (!InternalFS.begin() || !InternalFS.exists(VAULT_FILENAME))
    return false;
  File file = InternalFS.open(VAULT_FILENAME, FILE_O_READ);
  if (!file)
    return false;
  uint8_t header[VAULT_HEADER_SIZE];
  uint8_t tag[VAULT_TAG_SIZE];
  if (file.size() < sizeof(header) + VAULT_V2_PAYLOAD_SIZE + sizeof(tag) ||
      file.size() > sizeof(header) + sizeof(vaultPayload) + sizeof(tag) ||
      file.read(header, sizeof(header)) != sizeof(header) ||
      (memcmp(header, VAULT_MAGIC_V1, sizeof(VAULT_MAGIC_V1)) != 0 &&
       memcmp(header, VAULT_MAGIC_V2, sizeof(VAULT_MAGIC_V2)) != 0 &&
       memcmp(header, VAULT_MAGIC_V3, sizeof(VAULT_MAGIC_V3)) != 0))
  {
    file.close();
    secureZero(vaultPayload, sizeof(vaultPayload));
    secureZero(storageKey, sizeof(storageKey));
    secureZero(vaultSalt, sizeof(vaultSalt));
    return false;
  }
  uint8_t loadedVersion = header[4];
  size_t payloadLength = loadedVersion >= 3 ? sizeof(vaultPayload) : VAULT_V2_PAYLOAD_SIZE;
  if (file.size() != sizeof(header) + payloadLength + sizeof(tag))
  {
    file.close();
    secureZero(vaultPayload, sizeof(vaultPayload));
    secureZero(storageKey, sizeof(storageKey));
    secureZero(vaultSalt, sizeof(vaultSalt));
    secureZero(header, sizeof(header));
    return false;
  }
  memset(vaultPayload, 0, sizeof(vaultPayload));
  memcpy(vaultSalt, header + sizeof(VAULT_MAGIC_V1), sizeof(vaultSalt));
  bool readOkay = file.read(vaultPayload, payloadLength) == (int)payloadLength &&
                  file.read(tag, sizeof(tag)) == sizeof(tag);
  file.close();
  if (!readOkay || !deriveVaultEncryptionKey(passcode, passcodeLength, vaultSalt,
                                             loadedVersion, storageKey))
  {
    secureZero(vaultPayload, payloadLength);
    secureZero(tag, sizeof(tag));
    secureZero(storageKey, sizeof(storageKey));
    secureZero(vaultSalt, sizeof(vaultSalt));
    secureZero(header, sizeof(header));
    return false;
  }

  GCM<AES256> cipher;
  bool decrypted = cipher.setKey(storageKey, sizeof(storageKey)) &&
                   cipher.setIV(header + sizeof(VAULT_MAGIC_V1) + sizeof(vaultSalt), 12);
  if (decrypted)
  {
    cipher.addAuthData(header, sizeof(header));
    cipher.decrypt(vaultPayload, vaultPayload, payloadLength);
    decrypted = cipher.checkTag(tag, sizeof(tag));
  }
  cipher.clear();
  secureZero(tag, sizeof(tag));
  if (!decrypted || payloadLength < 1 || vaultPayload[0] > MAX_KEYS)
  {
    secureZero(vaultPayload, payloadLength);
    secureZero(storageKey, sizeof(storageKey));
    secureZero(vaultSalt, sizeof(vaultSalt));
    secureZero(header, sizeof(header));
    return false;
  }

  size_t entriesEnd = 1 + (size_t)vaultPayload[0] * sizeof(KeyEntry);
  for (size_t i = entriesEnd; i < VAULT_CALIBRATION_OFFSET; i++)
  {
    if (vaultPayload[i] != 0)
    {
      secureZero(vaultPayload, payloadLength);
      secureZero(storageKey, sizeof(storageKey));
      secureZero(vaultSalt, sizeof(vaultSalt));
      secureZero(header, sizeof(header));
      return false;
    }
  }
  size_t calibrationLockOffset = VAULT_CALIBRATION_OFFSET + sizeof(originalSetTime) + sizeof(agingOffset);
  if (vaultPayload[calibrationLockOffset] > 1)
  {
    secureZero(vaultPayload, payloadLength);
    secureZero(storageKey, sizeof(storageKey));
    secureZero(vaultSalt, sizeof(vaultSalt));
    secureZero(header, sizeof(header));
    return false;
  }

  int16_t loadedOffset = 0;
  uint8_t loadedClockFormat = 1;
  if (loadedVersion >= 3)
  {
    memcpy(&loadedOffset, vaultPayload + VAULT_CLOCK_SETTINGS_OFFSET, sizeof(loadedOffset));
    loadedClockFormat = vaultPayload[VAULT_CLOCK_SETTINGS_OFFSET + sizeof(loadedOffset)];
    if (loadedOffset < -840 || loadedOffset > 840 || loadedOffset % 30 != 0 || loadedClockFormat > 1)
    {
      secureZero(vaultPayload, sizeof(vaultPayload));
      secureZero(storageKey, sizeof(storageKey));
      secureZero(vaultSalt, sizeof(vaultSalt));
      secureZero(header, sizeof(header));
      return false;
    }
  }

  keysCount = vaultPayload[0];
  memcpy(&originalSetTime, vaultPayload + VAULT_CALIBRATION_OFFSET, sizeof(originalSetTime));
  memcpy(&agingOffset, vaultPayload + VAULT_CALIBRATION_OFFSET + sizeof(originalSetTime), sizeof(agingOffset));
  calibrationLocked = vaultPayload[calibrationLockOffset] != 0;
  utcOffsetMinutes = loadedOffset;
  clock24Hour = loadedClockFormat != 0;
  clockTextReady = false;
  memset(keys, 0, sizeof(keys));
  if (keysCount > 0)
    memcpy(keys, vaultPayload + 1, (size_t)keysCount * sizeof(KeyEntry));
  for (int i = 0; i < keysCount; i++)
  {
    if (!memchr(keys[i].username, '\0', sizeof(keys[i].username)) ||
        !memchr(keys[i].base32secret, '\0', sizeof(keys[i].base32secret)))
    {
      secureZero(keys, sizeof(keys));
      keysCount = 0;
      secureZero(vaultPayload, payloadLength);
      secureZero(storageKey, sizeof(storageKey));
      secureZero(vaultSalt, sizeof(vaultSalt));
      secureZero(header, sizeof(header));
      return false;
    }
  }
  secureZero(vaultPayload, payloadLength);
  storageKeyReady = true;
  vaultUnlocked = true;
  vaultFormatVersion = loadedVersion;

  if (loadedVersion < 3)
  {
    uint8_t oldKey[sizeof(storageKey)];
    uint8_t version2Key[sizeof(storageKey)];
    uint8_t upgradedKey[sizeof(storageKey)];
    memcpy(oldKey, storageKey, sizeof(oldKey));
    bool migrated;
    if (loadedVersion == 1)
      migrated = deriveVaultV2KeyFromBase(oldKey, version2Key);
    else
    {
      memcpy(version2Key, oldKey, sizeof(version2Key));
      migrated = true;
    }
    if (migrated)
      migrated = deriveVaultV3KeyFromV2(version2Key, upgradedKey);
    if (migrated)
    {
      memcpy(storageKey, upgradedKey, sizeof(storageKey));
      vaultFormatVersion = 3;
      bool previousAllow = allowNonceInitialization;
      allowNonceInitialization = true;
      migrated = saveKeys();
      allowNonceInitialization = previousAllow;
    }
    if (!migrated)
    {
      memcpy(storageKey, oldKey, sizeof(storageKey));
      vaultFormatVersion = loadedVersion;
    }
    secureZero(oldKey, sizeof(oldKey));
    secureZero(version2Key, sizeof(version2Key));
    secureZero(upgradedKey, sizeof(upgradedKey));
  }

  if (InternalFS.exists(VAULT_FILENAME) && InternalFS.exists("/vault.bak"))
    InternalFS.remove("/vault.bak");
  if (vaultFormatVersion >= 3)
    saveClockDisplaySettings();
  secureZero(header, sizeof(header));
  return true;
}

void clearUnlockedSecrets()
{
  secureZero(keys, sizeof(keys));
  keysCount = 0;
  secureZero(storageKey, sizeof(storageKey));
  secureZero(vaultPayload, sizeof(vaultPayload));
  secureZero(passcodeInput, sizeof(passcodeInput));
  secureZero(pendingPasscode, sizeof(pendingPasscode));
  secureZero(legacyPasscode, sizeof(legacyPasscode));
  secureZero(vaultSalt, sizeof(vaultSalt));
  originalSetTime = 0;
  agingOffset = 0;
  calibrationLocked = false;
  storageKeyReady = false;
  vaultUnlocked = false;
  selectedKeyIndex = 0;
}

void beginLockout(uint32_t currentMillis)
{
  if (failedAttempts == 0 || failedAttempts >= MAX_FAILED_ATTEMPTS)
    return;
  lockoutDurationMillis = LOCKOUT_DELAYS_MS[failedAttempts - 1];
  lockoutActive = lockoutDurationMillis > 0;
  lockoutUntilMillis = currentMillis + lockoutDurationMillis;
}

void eraseFilesystemAndRestart()
{
  setUSBExposure(false);
  clearUnlockedSecrets();
  secureZero(passcodeInput, sizeof(passcodeInput));
  secureZero(pendingPasscode, sizeof(pendingPasscode));
  secureZero(legacyPasscode, sizeof(legacyPasscode));
  secureZero(vaultSalt, sizeof(vaultSalt));
  writeAgingOffset(0);
  InternalFS.begin();
  InternalFS.format();
  InternalFS.begin();
  failedAttempts = 0;
  lockoutActive = false;
  vaultPresent = false;
  locked = true;
  inPinSetup = true;
  confirmingPasscode = false;
  setupMismatch = false;
  changingPasscode = false;
  verifyingCurrentPasscode = false;
  legacyMode = false;
  legacyDataLoaded = false;
  menuActive = false;
  clockSettingsActive = false;
  editingClockOffset = false;
  inputIndex = 0;
  selectedDigit = 0;
  calibrationLocked = false;
  agingOffset = 0;
  originalSetTime = 0;
  utcOffsetMinutes = 0;
  clock24Hour = true;
}

void recordFailedPasscode(uint32_t currentMillis)
{
  wrongPinFlashStart = currentMillis;
  wrongPinFlashActive = true;
  if (failedAttempts < MAX_FAILED_ATTEMPTS)
    failedAttempts++;
  saveAttemptState();
  if (failedAttempts >= MAX_FAILED_ATTEMPTS)
  {
    eraseFilesystemAndRestart();
    return;
  }
  beginLockout(currentMillis);
}

void resetPasscodeInput()
{
  secureZero(passcodeInput, sizeof(passcodeInput));
  inputIndex = 0;
  selectedDigit = 0;
}

void startPasscodeSetup(bool changeExisting)
{
  changingPasscode = changeExisting;
  verifyingCurrentPasscode = changeExisting;
  inPinSetup = true;
  confirmingPasscode = false;
  setupMismatch = false;
  setupMessageUntil = 0;
  menuActive = false;
  setUSBExposure(false);
  if (changeExisting || !legacyDataLoaded)
    clearUnlockedSecrets();
  locked = true;
  resetPasscodeInput();
}

bool storeConfirmedPasscode()
{
  uint8_t previousKey[sizeof(storageKey)];
  uint8_t previousSalt[sizeof(vaultSalt)];
  uint8_t previousFormatVersion = vaultFormatVersion;
  bool previousKeyReady = storageKeyReady;
  bool previousVaultUnlocked = vaultUnlocked;
  bool previousAllowNonceInitialization = allowNonceInitialization;
  memcpy(previousKey, storageKey, sizeof(previousKey));
  memcpy(previousSalt, vaultSalt, sizeof(previousSalt));

  bool success = fillCryptoRandom(vaultSalt, sizeof(vaultSalt)) &&
                 deriveVaultEncryptionKey((const uint8_t *)pendingPasscode, pendingPasscodeLength,
                                          vaultSalt, 3, storageKey);
  if (success)
  {
    storageKeyReady = true;
    vaultUnlocked = true;
    vaultFormatVersion = 3;
    allowNonceInitialization = true;
    success = saveKeys();
    allowNonceInitialization = previousAllowNonceInitialization;
  }

  if (success)
  {
    if (InternalFS.exists(PIN_FILENAME))
      InternalFS.remove(PIN_FILENAME);
    if (InternalFS.exists(KEYS_FILENAME))
      InternalFS.remove(KEYS_FILENAME);
    if (InternalFS.exists(CALIBRATION_FILENAME))
      InternalFS.remove(CALIBRATION_FILENAME);
    legacyDataLoaded = false;
    legacyMode = false;
    secureZero(legacyPasscode, sizeof(legacyPasscode));
    failedAttempts = 0;
    saveAttemptState();
    vaultPresent = true;
    saveClockDisplaySettings();
    inPinSetup = false;
    confirmingPasscode = false;
    changingPasscode = false;
    verifyingCurrentPasscode = false;
    locked = false;
    selectedKeyIndex = 0;
    setUSBExposure(true);
  }
  else
  {
    memcpy(storageKey, previousKey, sizeof(storageKey));
    memcpy(vaultSalt, previousSalt, sizeof(vaultSalt));
    storageKeyReady = previousKeyReady;
    vaultUnlocked = previousVaultUnlocked;
    vaultFormatVersion = previousFormatVersion;
    allowNonceInitialization = previousAllowNonceInitialization;
  }

  secureZero(previousKey, sizeof(previousKey));
  secureZero(previousSalt, sizeof(previousSalt));
  return success;
}

void confirmPasscodeEntry(uint32_t currentMillis)
{
  if (lockoutActive && (int32_t)(currentMillis - lockoutUntilMillis) < 0)
    return;
  lockoutActive = false;

  if (legacyMode)
  {
    bool matches = inputIndex == strlen(legacyPasscode) &&
                   memcmp(passcodeInput, legacyPasscode, inputIndex) == 0;
    if (matches && loadLegacyKeys())
    {
      legacyMode = false;
      failedAttempts = 0;
      lockoutActive = false;
      saveAttemptState();
      startPasscodeSetup(false);
      return;
    }
    resetPasscodeInput();
    recordFailedPasscode(currentMillis);
    return;
  }

  if (verifyingCurrentPasscode)
  {
    if (inputIndex < MIN_PIN_LENGTH)
      return;
    hashingPasscode = true;
    bool currentPinValid = unlockVault((const uint8_t *)passcodeInput, inputIndex);
    hashingPasscode = false;
    resetPasscodeInput();
    if (currentPinValid)
    {
      verifyingCurrentPasscode = false;
      failedAttempts = 0;
      lockoutActive = false;
      saveAttemptState();
      setupMismatch = false;
      locked = true;
    }
    else
    {
      clearUnlockedSecrets();
      recordFailedPasscode(millis());
    }
    return;
  }

  if (inputIndex < MIN_PIN_LENGTH)
    return;

  if (inPinSetup)
  {
    if (!confirmingPasscode)
    {
      memcpy(pendingPasscode, passcodeInput, inputIndex);
      pendingPasscode[inputIndex] = '\0';
      pendingPasscodeLength = inputIndex;
      confirmingPasscode = true;
      resetPasscodeInput();
      return;
    }

    bool matches = inputIndex == pendingPasscodeLength &&
                   memcmp(passcodeInput, pendingPasscode, inputIndex) == 0;
    resetPasscodeInput();
    if (!matches)
    {
      secureZero(pendingPasscode, sizeof(pendingPasscode));
      pendingPasscodeLength = 0;
      confirmingPasscode = false;
      setupMismatch = true;
      setupMessageUntil = currentMillis + 1500;
      return;
    }

    hashingPasscode = true;
    bool stored = storeConfirmedPasscode();
    hashingPasscode = false;
    if (!stored)
    {
      secureZero(pendingPasscode, sizeof(pendingPasscode));
      pendingPasscodeLength = 0;
      confirmingPasscode = false;
      setupMismatch = true;
      setupMessageUntil = currentMillis + 1500;
      return;
    }
    secureZero(pendingPasscode, sizeof(pendingPasscode));
    pendingPasscodeLength = 0;
    return;
  }

  hashingPasscode = true;
  bool unlocked = unlockVault((const uint8_t *)passcodeInput, inputIndex);
  hashingPasscode = false;
  resetPasscodeInput();
  if (unlocked)
  {
    locked = false;
    failedAttempts = 0;
    lockoutActive = false;
    saveAttemptState();
    setUSBExposure(true);
    lastActivityTime = currentMillis;
  }
  else
  {
    clearUnlockedSecrets();
    recordFailedPasscode(millis());
  }
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
  if (inPinSetup || locked || legacyMode)
  {
    display.clearDisplay();
    bool showLock = true;
    if (wrongPinFlashActive)
    {
      uint32_t elapsed = millis() - wrongPinFlashStart;
      if (elapsed >= 1400)
        wrongPinFlashActive = false;
      else
        showLock = ((elapsed / 233) % 2) == 0;
    }
    if (showLock)
      display.drawBitmap(0, 0, lockIcon8x8, 8, 8, SSD1306_WHITE);
    display.setFont(nullptr);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(12, 0);

    bool showingMismatch = setupMismatch && (int32_t)(millis() - setupMessageUntil) < 0;
    if (showingMismatch)
      display.print("TRY AGAIN");
    else if (hashingPasscode)
      display.print("CHECKING");
    else if (verifyingCurrentPasscode)
      display.print("VERIFY");
    else if (legacyMode)
      display.print("OLD PIN U/D");
    else if (inPinSetup && confirmingPasscode)
      display.print("REPEAT");
    else if (inPinSetup)
      display.print(changingPasscode ? "NEW" : (legacyDataLoaded ? "IMPORT" : "SET"));
    else
      display.print("PIN");

    if (inPinSetup && !legacyMode && !verifyingCurrentPasscode && inputIndex >= MIN_PIN_LENGTH)
    {
      const char *estimate = PIN_SEARCH_ESTIMATES[inputIndex - MIN_PIN_LENGTH];
      int16_t estimateX, estimateY;
      uint16_t estimateWidth, estimateHeight;
      display.getTextBounds(estimate, 0, 0, &estimateX, &estimateY, &estimateWidth, &estimateHeight);
      display.setCursor(0, 11);
      display.print(estimate);
    }

    if (lockoutActive && (int32_t)(millis() - lockoutUntilMillis) < 0)
    {
      uint32_t seconds = (lockoutUntilMillis - millis() + 999) / 1000;
      char waitText[20];
      snprintf(waitText, sizeof(waitText), "WAIT %lus", (unsigned long)seconds);
      int16_t x1, y1;
      uint16_t width, height;
      display.getTextBounds(waitText, 0, 0, &x1, &y1, &width, &height);
      display.setCursor((SCREEN_WIDTH - width) / 2, 14);
      display.print(waitText);
    }
    else if (!showingMismatch && legacyMode)
    {
      display.setTextSize(2);
      display.setCursor(40, 9);
      display.print("U/D");
    }
    else if (!showingMismatch)
    {
      char current = (char)('0' + selectedDigit);
      display.setFont(&FreeSansBold9pt7b);
      int16_t digitX, digitY;
      uint16_t digitWidth, digitHeight;
      display.getTextBounds(&current, 0, 0, &digitX, &digitY, &digitWidth, &digitHeight);
      int16_t digitScreenX = (SCREEN_WIDTH - digitWidth) / 2;
      display.setCursor(digitScreenX, 19);
      display.print(current);
      if ((millis() / 350) % 2 == 0)
        display.drawFastHLine(digitScreenX, 22, digitWidth, SSD1306_WHITE);
      display.setFont(nullptr);
    }

    uint16_t dotsWidth = inputIndex == 0 ? 0 : inputIndex * 6 - 2;
    uint16_t dotsStartX = (SCREEN_WIDTH - dotsWidth) / 2;
    for (uint8_t i = 0; i < inputIndex; i++)
      display.fillCircle(dotsStartX + 2 + i * 6, 28, 2, SSD1306_WHITE);
    drawTopStatus(false, false);
    display.display();
    return;
  }

  if (clockSettingsActive)
  {
    display.clearDisplay();
    drawTopStatus(true, clockSettingsSelection == 2);
    uint8_t firstVisible = clockSettingsSelection == 2 ? 1 : 0;
    uint8_t selectedY = clockSettingsSelection == firstVisible ? 10 : 21;
    display.fillRoundRect(10, selectedY, 116, 10, 3, SSD1306_WHITE);
    display.fillRoundRect(2, selectedY + 1, 4, 8, 2, SSD1306_WHITE);
    display.setFont(nullptr);
    display.setTextSize(1);
    display.setCursor(18, 12);
    display.setTextColor(clockSettingsSelection == firstVisible ? SSD1306_BLACK : SSD1306_WHITE);
    if (firstVisible == 0)
    {
      char offsetText[20];
      int hours = abs(utcOffsetMinutes) / 60;
      int minutes = abs(utcOffsetMinutes) % 60;
      snprintf(offsetText, sizeof(offsetText), "UTC%c%02d:%02d", utcOffsetMinutes < 0 ? '-' : '+', hours, minutes);
      display.print(editingClockOffset ? "* " : "");
      display.print(offsetText);
    }
    else
      display.print("Time format");

    display.setCursor(18, 23);
    display.setTextColor(clockSettingsSelection == firstVisible + 1 ? SSD1306_BLACK : SSD1306_WHITE);
    if (firstVisible == 0)
    {
      display.print("Format ");
      display.print(clock24Hour ? "24h" : "12h");
    }
    else
      display.print("Back");
    display.display();
    return;
  }

  if (menuActive)
  {
    display.clearDisplay();

    // Menu scrollbar
    display.drawFastVLine(5, 8, 23, SSD1306_WHITE);

    uint8_t firstVisible = menuSelection >= 3 ? 2 : 1;

    // Only draw a menu-item highlight when an actual menu item
    // is selected. Selection 0 is the back icon.
    if (menuSelection != 0)
    {
      uint8_t selectedY = menuSelection == firstVisible ? 9 : 21;

      display.fillRoundRect(10, selectedY, 116, 10, 3, SSD1306_WHITE);
      display.fillRoundRect(2, selectedY + 1, 4, 8, 2, SSD1306_WHITE);
    }

    display.setFont(nullptr);
    display.setTextSize(1);

    display.setCursor(18, 11);
    display.setTextColor(menuSelection == firstVisible ? SSD1306_BLACK : SSD1306_WHITE);
    display.print(firstVisible == 1 ? "Change PIN" : "Clock");

    display.setCursor(18, 23);
    display.setTextColor(menuSelection == firstVisible + 1 ? SSD1306_BLACK : SSD1306_WHITE);
    display.print(firstVisible == 1 ? "Clock" : "Sleep");

    // Back icon is the only highlighted element when menuSelection == 0.
    drawTopStatus(true, menuSelection == 0);

    display.display();
    return;
  }

  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  if (keysCount == 0)
  {
    display.setCursor(0, 0);
    display.println("No keys saved");
  }
  else
  {
    uint64_t now = rtc.now().unixtime();
    char *rawCode = totp.getCode(keys[selectedKeyIndex].base32secret, 30, now);
    char formattedCode[8] = "------";
    if (rawCode && strlen(rawCode) == 6)
    {
      memcpy(formattedCode, rawCode, 3);
      formattedCode[3] = ' ';
      memcpy(&formattedCode[4], &rawCode[3], 3);
      formattedCode[7] = '\0';
    }

    display.setFont(&FreeSansBold9pt7b);
    int16_t x1, y1;
    uint16_t width, height;
    display.getTextBounds(formattedCode, 0, 0, &x1, &y1, &width, &height);
    display.setCursor((SCREEN_WIDTH - width) / 2, 21);
    display.print(formattedCode);

    display.setFont(nullptr);
    display.setTextSize(1);
    char displayUser[18];
    strncpy(displayUser, keys[selectedKeyIndex].username, sizeof(displayUser) - 1);
    displayUser[sizeof(displayUser) - 1] = '\0';
    uint16_t userWidth;
    int16_t userX, userY;
    uint16_t userHeight;
    display.getTextBounds(displayUser, 0, 0, &userX, &userY, &userWidth, &userHeight);
    if (userWidth > SCREEN_WIDTH)
    {
      strncpy(displayUser, keys[selectedKeyIndex].username, 13);
      strcpy(&displayUser[13], "...");
    }
    display.setCursor(0, 23);
    display.print(displayUser);

    char countdown[3];
    uint8_t secondsRemaining = 30 - (uint8_t)(now % 30);
    snprintf(countdown, sizeof(countdown), "%02u", secondsRemaining);
    display.setCursor(SCREEN_WIDTH - 18, 10);
    display.print(countdown);
  }
  drawTopStatus(false, false);
  display.display();
}

// Buttons

bool readDebouncedButton(uint8_t pin, bool &lastRaw, bool &stableState, uint32_t &changedAt, uint32_t now)
{
  bool raw = digitalRead(pin) == LOW;
  if (raw != lastRaw)
  {
    lastRaw = raw;
    changedAt = now;
  }
  if (now - changedAt >= 30)
    stableState = lastRaw;
  return stableState;
}

bool isPasscodeScreen()
{
  return inPinSetup || locked || legacyMode;
}

void addCurrentDigit()
{
  if (inputIndex >= MAX_PIN_LENGTH)
    return;
  passcodeInput[inputIndex++] = (char)('0' + selectedDigit);
  passcodeInput[inputIndex] = '\0';
  selectedDigit = 0;
}

void removeLastDigit()
{
  if (inputIndex == 0)
    return;
  passcodeInput[--inputIndex] = '\0';
  selectedDigit = 0;
}

void moveMenuSelection(int direction)
{
  menuSelection = (menuSelection + 4 + direction) % 4;
}

void moveClockSettingsSelection(int direction)
{
  clockSettingsSelection = (clockSettingsSelection + 3 + direction) % 3;
}

void enterUltraSleep();

void selectClockSetting()
{
  if (clockSettingsSelection == 0)
  {
    if (vaultFormatVersion < 3)
      return;

    if (editingClockOffset)
    {
      // Confirm the currently displayed offset.
      // This is the only point where scrolling changes are
      // committed to the encrypted vault.
      if (saveKeys())
      {
        editingClockOffset = false;

        // Keep the small display-settings cache synchronized.
        saveClockDisplaySettings();
      }
    }
    else
    {
      editingClockOffset = true;
    }
  }
  else if (clockSettingsSelection == 1)
  {
    if (vaultFormatVersion < 3)
      return;

    bool previousFormat = clock24Hour;
    clock24Hour = !clock24Hour;
    clockTextReady = false;

    if (!saveClockDisplaySettings() || !saveKeys())
    {
      clock24Hour = previousFormat;
      clockTextReady = false;
      saveClockDisplaySettings();
    }
  }
  else
  {
    clockSettingsActive = false;
    editingClockOffset = false;
    menuActive = true;
    menuSelection = 2;
  }
}

void selectMenuItem()
{
  if (menuSelection == 0)
  {
    menuActive = false;
    return;
  }
  if (menuSelection == 1)
  {
    startPasscodeSetup(true);
    return;
  }
  if (menuSelection == 2)
  {
    menuActive = false;
    clockSettingsActive = true;
    clockSettingsSelection = 0;
    editingClockOffset = false;
    return;
  }
  menuActive = false;
  clearUnlockedSecrets();
  enterUltraSleep();
}

void adjustClockOffset(int direction)
{
  if (vaultFormatVersion < 3)
    return;

  int16_t updated = utcOffsetMinutes + direction * 30;

  if (updated < -840)
    updated = -840;

  if (updated > 840)
    updated = 840;

  if (updated != utcOffsetMinutes)
  {
    utcOffsetMinutes = updated;
    clockTextReady = false;
  }
}

void handleButtons()
{
  static bool lastRawUp = false;
  static bool stableUp = false;
  static uint32_t upChangedAt = 0;
  static bool lastRawDown = false;
  static bool stableDown = false;
  static uint32_t downChangedAt = 0;
  static bool singleActive = false;
  static bool singleWasUp = false;
  static bool singleLongHandled = false;
  static uint32_t singleStartedAt = 0;
  static bool comboActive = false;
  static bool comboHandled = false;
  static uint32_t comboStartedAt = 0;
  static bool suppressUntilReleased = false;

  uint32_t now = millis();
  bool up = readDebouncedButton(BUTTON_UP_PIN, lastRawUp, stableUp, upChangedAt, now);
  bool down = readDebouncedButton(BUTTON_DOWN_PIN, lastRawDown, stableDown, downChangedAt, now);

  if (lockoutActive && (int32_t)(now - lockoutUntilMillis) >= 0)
    lockoutActive = false;
  if (lockoutActive)
  {
    if (!up && !down)
      suppressUntilReleased = false;
    else
      suppressUntilReleased = true;
    return;
  }

  if (up && down)
  {
    if (!comboActive)
    {
      comboActive = true;
      comboHandled = false;
      comboStartedAt = now;
      singleActive = false;
      suppressUntilReleased = true;
    }

    uint32_t requiredHold = (menuActive || clockSettingsActive || isPasscodeScreen()) ? BUTTON_HOLD_SELECT_MS : MENU_HOLD_MS;
    if (!comboHandled && now - comboStartedAt >= requiredHold)
    {
      comboHandled = true;
      lastActivityTime = now;
      if (menuActive)
        selectMenuItem();
      else if (clockSettingsActive)
        selectClockSetting();
      else if (isPasscodeScreen())
        confirmPasscodeEntry(now);
      else
      {
        menuActive = true;
        menuSelection = 1;
      }
    }
    return;
  }

  if (comboActive)
  {
    if (!comboHandled && isPasscodeScreen())
      removeLastDigit();
    comboActive = false;
    comboHandled = false;
    suppressUntilReleased = up || down;
    lastActivityTime = now;
    return;
  }

  if (suppressUntilReleased)
  {
    if (!up && !down)
      suppressUntilReleased = false;
    return;
  }

  if (up || down)
  {
    if (!singleActive)
    {
      singleActive = true;
      singleWasUp = up;
      singleStartedAt = now;
      singleLongHandled = false;
    }
    else if (!singleLongHandled && now - singleStartedAt >= BUTTON_HOLD_NEXT_MS)
    {
      singleLongHandled = true;
      if (menuActive)
        selectMenuItem();
      else if (clockSettingsActive)
        selectClockSetting();
      else if (isPasscodeScreen() && !legacyMode)
        addCurrentDigit();
      lastActivityTime = now;
    }
    return;
  }

  if (singleActive)
  {
    singleActive = false;
    lastActivityTime = now;
    if (singleLongHandled)
      return;

    if (legacyMode)
    {
      if (inputIndex < MAX_PIN_LENGTH)
      {
        passcodeInput[inputIndex++] = singleWasUp ? 'u' : 'd';
        passcodeInput[inputIndex] = '\0';
      }
    }
    else if (isPasscodeScreen())
    {
      setupMismatch = false;
      selectedDigit = (selectedDigit + (singleWasUp ? 9 : 1)) % 10;
    }
    else if (menuActive)
    {
      moveMenuSelection(singleWasUp ? -1 : 1);
    }
    else if (clockSettingsActive)
    {
      int direction = singleWasUp ? -1 : 1;
      if (editingClockOffset)
        adjustClockOffset(direction);
      else
        moveClockSettingsSelection(direction);
    }
    else if (keysCount > 0)
    {
      selectedKeyIndex = (selectedKeyIndex + keysCount + (singleWasUp ? -1 : 1)) % keysCount;
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

        if (serialLine == "configurator")
        {
          if (!locked)
            lastConfiguratorHeartbeat = millis();
          secureClearString(serialLine);
          continue;
        }

        if (locked)
        {
          Serial.println("Device is locked. Unlock first.");
          secureClearString(serialLine);
          continue;
        }

        if (serialLine.startsWith("add "))
        {
          String cmd = serialLine.substring(4);
          int spaceIndex = cmd.lastIndexOf(' ');
          if (spaceIndex > 0)
          {
            String username = cmd.substring(0, spaceIndex);
            String secret = cmd.substring(spaceIndex + 1);
            username.trim();
            secret.trim();
            secret.replace(" ", "");
            secret.toUpperCase();

            if (username.length() > 31 || secret.length() > 127)
            {
              Serial.println("Error: username or secret too long");
            }
            else if (keysCount >= MAX_KEYS)
            {
              Serial.println("Max keys reached");
            }
            else if (isDuplicateKey(username.c_str(), secret.c_str()))
            {
              Serial.println("Duplicate key, not added.");
            }
            else
            {
              username.toCharArray(keys[keysCount].username, sizeof(keys[keysCount].username));
              secret.toCharArray(keys[keysCount].base32secret, sizeof(keys[keysCount].base32secret));
              keysCount++;
              if (saveKeys())
              {
                Serial.print("Added key: ");
                Serial.println(username);
              }
              else
              {
                keysCount--;
                secureZero(&keys[keysCount], sizeof(KeyEntry));
                Serial.println("Failed to save encrypted vault");
              }
            }
            secureClearString(username);
            secureClearString(secret);
          }
          else
          {
            Serial.println("Invalid add command format");
          }
          secureClearString(cmd);
        }

        else if (serialLine.startsWith("del "))
        {
          int index = serialLine.substring(4).toInt();
          if (index >= 0 && index < keysCount)
          {
            Serial.printf("Deleting key %d: %s\n", index, keys[index].username);
            KeyEntry removed = keys[index];
            for (int i = index; i < keysCount - 1; i++)
              keys[i] = keys[i + 1];
            keysCount--;
            if (saveKeys())
            {
              secureZero(&keys[keysCount], sizeof(KeyEntry));
              secureZero(&removed, sizeof(removed));
              Serial.println("Key deleted");
            }
            else
            {
              for (int i = keysCount; i > index; i--)
                keys[i] = keys[i - 1];
              keys[index] = removed;
              keysCount++;
              secureZero(&removed, sizeof(removed));
              Serial.println("Failed to save encrypted vault");
            }
          }
          else
          {
            Serial.println("Invalid key index");
          }
        }

        else if (serialLine.startsWith("setunixtime "))
        {
          String timeStr = serialLine.substring(strlen("setunixtime "));
          timeStr.trim();
          unsigned long unixTime = timeStr.toInt();
          if (unixTime > 0)
          {
            DateTime rtcBefore = rtc.now();
            int8_t correction = 0;

            if (originalSetTime == 0)
            {
              originalSetTime = unixTime;
              saveCalibration();
              Serial.println("Calibration baseline saved ");
            }
            else if (!calibrationLocked && unixTime >= originalSetTime && unixTime - originalSetTime >= CALIBRATION_INTERVAL)
            {
              int64_t drift = (int64_t)rtcBefore.unixtime() - (int64_t)unixTime;
              uint32_t elapsed = unixTime - originalSetTime;
              int32_t agingChange = (int32_t)((drift * 10000000LL) / elapsed);
              int32_t newAgingOffset = (int32_t)readAgingOffset() + agingChange;
              if (newAgingOffset < -128)
                newAgingOffset = -128;
              if (newAgingOffset > 127)
                newAgingOffset = 127;

              correction = (int8_t)newAgingOffset;
              if (writeAgingOffset(correction))
              {
                agingOffset = correction;
                originalSetTime = unixTime;
                saveCalibration();
                Serial.printf("Auto-calibrated the RTC: drift %lld seconds, aging offset %d\n", drift, correction);
              }
              else
              {
                Serial.println("RTC calibration failed... If you think something's wrong, make an issue on github.");
              }
            }

            rtc.adjust(DateTime(unixTime));
            clockTextReady = false;
            Serial.print("RTC time set to Unix time: ");
            Serial.println(unixTime);
          }
          else
          {
            Serial.println("Invalid Unix time");
          }
        }

        else if (serialLine.startsWith("manualcalibration "))
        {
          int value = serialLine.substring(strlen("manualcalibration ")).toInt();
          if (value < -128 || value > 127)
          {
            Serial.println("Manual calibration must be between -128 and 127");
          }
          else if (writeAgingOffset((int8_t)value))
          {
            agingOffset = (int8_t)value;
            calibrationLocked = true;
            saveCalibration();
            Serial.printf("Manual RTC calibration set to aging offset %d\n", value);
          }
          else
          {
            Serial.println("RTC aging calibration failed");
          }
        }

        else if (serialLine == "clearcalibration")
        {
          if (writeAgingOffset(0))
          {
            originalSetTime = 0;
            agingOffset = 0;
            calibrationLocked = false;
            saveCalibration();
            Serial.println("RTC calibration cleared; setunixtime to set a new base.");
          }
          else
          {
            Serial.println("RTC calibration clear failed");
          }
        }

        else if (serialLine == "lockcalibration")
        {
          calibrationLocked = true;
          saveCalibration();
          Serial.println("RTC auto-calibration locked");
        }

        else if (serialLine == "unlockcalibration")
        {
          calibrationLocked = false;
          saveCalibration();
          Serial.println("RTC auto-calibration unlocked");
        }

        else if (serialLine == "getcalibration")
        {
          Serial.printf("RTC aging offset: %d\n", readAgingOffset());
          if (originalSetTime == 0)
          {
            Serial.println("Calibration baseline: NOT SET");
          }
          else
          {
            DateTime baseline(originalSetTime);
            char formattedBaseline[] = "YYYY-MM-DD hh:mm:ss";
            baseline.toString(formattedBaseline);
            Serial.print("Calibration baseline: ");
            Serial.println(formattedBaseline);
          }
          Serial.print("Calibration locked: ");
          Serial.println(calibrationLocked ? "YES" : "NO");
        }

        else if (serialLine == "list")
        {
          Serial.printf("Keys (%d):\n", keysCount);
          for (int i = 0; i < keysCount; i++)
          {
            Serial.printf("%d: %s\n", i, keys[i].username);
          }
        }

        else if (serialLine == "codes")
        {
          if (locked || !vaultUnlocked || !usbExposed)
          {
            Serial.println("Device is locked. Unlock first.");
          }
          else
          {
            uint64_t now = rtc.now().unixtime();
            uint8_t secondsRemaining = 30 - (uint8_t)(now % 30);
            Serial.printf("TOTP codes (%d):\n", keysCount);
            for (int i = 0; i < keysCount; i++)
            {
              char *rawCode = totp.getCode(keys[i].base32secret, 30, now);
              char code[7] = "------";
              if (rawCode && strlen(rawCode) == 6)
                memcpy(code, rawCode, sizeof(code));

              Serial.printf("%d: %s:%u\n", i, code, secondsRemaining);
              secureZero(code, sizeof(code));
              if (rawCode)
                secureZero(rawCode, strlen(rawCode));
            }
          }
        }

        else if (serialLine == "clear")
        {
          int previousCount = keysCount;
          keysCount = 0;
          if (saveKeys())
          {
            secureZero(keys, sizeof(keys));
            Serial.println("Keys cleared");
          }
          else
          {
            keysCount = previousCount;
            Serial.println("Failed to save encrypted vault");
          }
        }

        else if (serialLine == "pinsetup")
        {
          startPasscodeSetup(true);
          Serial.println("Set and confirm the new numeric passcode on the device");
        }

        else if (serialLine == "lock")
        {
          clearUnlockedSecrets();
          resetPasscodeInput();
          secureZero(pendingPasscode, sizeof(pendingPasscode));
          lastConfiguratorHeartbeat = 0;
          locked = true;
          menuActive = false;
          Serial.println("Device locked");
          delay(20);
          setUSBExposure(false);
        }

        else if (serialLine == "factoryreset")
        {
          if (InternalFS.begin())
          {
            writeAgingOffset(0);
            eraseFilesystemAndRestart();
            Serial.println("Factory reset done, rebooting...");
            delay(100);
            NVIC_SystemReset();
          }
          else
          {
            Serial.println("FS mount failed");
          }
        }

        // DONT USE. i mean u can but youll need to press rst which is annoying if u alr have the case on
        else if (serialLine == "dfu")
        {
          clearUnlockedSecrets();
          resetPasscodeInput();
          secureZero(pendingPasscode, sizeof(pendingPasscode));
          NRF_POWER->GPREGRET = DFU_MAGIC_UF2_RESET;
          NVIC_SystemReset();
        }

        else
        {
          Serial.println("Unknown command");
        }

        secureClearString(serialLine);
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

void fadeOutDisplay()
{
  for (int contrast = 200; contrast >= 0; contrast -= 15)
  {
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(contrast);
    delay(60);
  }
  display.ssd1306_command(SSD1306_DISPLAYOFF);
}

void enterUltraSleep()
{
  clearUnlockedSecrets();
  resetPasscodeInput();
  secureZero(pendingPasscode, sizeof(pendingPasscode));
  secureZero(legacyPasscode, sizeof(legacyPasscode));
  menuActive = false;
  if (hardwareCryptoReady)
  {
    nRFCrypto.end();
    hardwareCryptoReady = false;
  }
  Serial.println("Entering ultra sleep...");
  setUSBExposure(false);
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  while (digitalRead(BUTTON_UP_PIN) == LOW || digitalRead(BUTTON_DOWN_PIN) == LOW)
    delay(10);
  delay(1000);
  NRF_POWER->SYSTEMOFF = 1;
  while (true)
  {
  }
}

void setup()
{

   //enableApprotect(); //QQ

  pinMode(PIN_013, OUTPUT);
  digitalWrite(PIN_013, HIGH);
  delay(1000);

  Serial.begin(115200);
  //Serial.end();
  hardwareCryptoReady = nRFCrypto.begin();

  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println("SSD1306 init failed");
    while (1)
    { 
    }
  }
  display.display();

  if (!rtc.begin())
  {
    Serial.println("RTC init failed");
    while (1)
    {
    }
  }

  configureWakeupButtons();

  recoverVaultFile();
  loadClockDisplaySettings();
  loadAttemptState();
  if (failedAttempts >= MAX_FAILED_ATTEMPTS)
    eraseFilesystemAndRestart();

  if (!vaultPresent)
  {
    if (InternalFS.begin() && InternalFS.exists(PIN_FILENAME))
    {
      legacyMode = true;
      locked = true;
      if (!loadLegacyPasscode())
        eraseFilesystemAndRestart();
    }
    else
    {
      if (InternalFS.begin() && InternalFS.exists(KEYS_FILENAME))
        loadLegacyKeys();
      startPasscodeSetup(false);
      failedAttempts = 0;
      saveAttemptState();
    }
  }
  else
  {
    locked = true;
    inPinSetup = false;
  }

  if (failedAttempts >= 3 && (vaultPresent || legacyMode))
    beginLockout(millis());

  if (vaultPresent)
    loadCalibration();
  else
    loadLegacyCalibration();

  // BLE(); // This is for serial over BLE, I dont remember if it works but i dont need it.
  // TinyUSBDevice.detach();
  lastActivityTime = millis();

  if (locked)
    setUSBExposure(false);
  else
    setUSBExposure(true);
}

void loop()
{
  handleButtons();
  if (!locked && usbExposed)
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

  bool configuratorConnected =
      (!locked &&
        usbExposed &&
       (millis() - lastConfiguratorHeartbeat) <= CONFIGURATOR_TIMEOUT);

  if ((millis() - lastActivityTime) > INACTIVITY_TIMEOUT && !configuratorConnected)
  {
    enterUltraSleep();
  }

  delay(50);
}