/*

  Features:   AM; SSB; LW/MW/SW; external mute circuit control; AGC; Attenuation gain control;
              SSB filter; CW; AM filter; 1, 5, 10, 50 and 500kHz step on AM and 10Hhz sep on SSB

  Optimierte Version:
  - Modus pro Band wird gespeichert
  - ALL bleibt immer AM
  - EEPROM mit struct + EEPROM.get/put
  - zentrale Modus- und Bandlogik
  - Start mit maximaler Empfindlichkeit / minimaler Dämpfung
  - Fuchs-Startbild auf dem LCD
  - FM verbessert: 50us De-Emphasis, Suchlauf-Schwellen, Soft-Mute, Stereo-Blend
  - AM verbessert: bandabhängige Suchlauf-Schwellen, dynamische Seek-Spacing, ruhigeres Soft-Mute/AGC

*/

#include <SI4735.h>
#include <EEPROM.h>
#include <LiquidCrystal.h>
#include "Rotary.h"
#include <patch_init.h>

const uint16_t size_content = sizeof ssb_patch_content;

#define FM_BAND_TYPE 0
#define MW_BAND_TYPE 1
#define SW_BAND_TYPE 2
#define LW_BAND_TYPE 3

#define RESET_PIN 10

#define ENCODER_PIN_A 0
#define ENCODER_PIN_B 1

#define LCD_D7 7
#define LCD_D6 6
#define LCD_D5 5
#define LCD_D4 4
#define LCD_RS 14
#define LCD_E 16

#define ENCODER_PUSH_BUTTON 15
#define DUMMY_BUTTON 19

#define MIN_ELAPSED_TIME 300
#define MIN_ELAPSED_RSSI_TIME 150
#define ELAPSED_COMMAND 2000
#define ELAPSED_CLICK 1500
#define DEFAULT_VOLUME 53

#define FM 0
#define LSB 1
#define USB 2
#define AM 3
#define LW 4

#define EEPROM_SIZE 512
#define STORE_TIME 10000

const uint8_t app_id = 41;
const int eeprom_address = 0;

long storeTime = millis();
bool itIsTimeToSave = false;

bool bfoOn = false;
bool ssbLoaded = false;

int8_t agcIdx = 1;
uint8_t disableAgc = 1;
int8_t agcNdx = 0;
int8_t softMuteMaxAttIdx = 0;
uint8_t countClick = 0;
uint8_t seekDirection = 1;

bool cmdBand = false;
bool cmdVolume = false;
bool cmdAgc = false;
bool cmdBandwidth = false;
bool cmdStep = false;
bool cmdMode = false;
bool cmdMenu = false;
bool cmdSoftMuteMaxAtt = false;

int16_t currentBFO = 0;
long elapsedRSSI = millis();
long elapsedButton = millis();
long elapsedCommand = millis();
long elapsedClick = millis();
volatile int encoderCount = 0;
uint16_t currentFrequency;

const uint8_t currentBFOStep = 25;  // Erhöht für aggressiveres BFO-Tuning

// PROGMEM-Strings für Speicheroptimierung
const char menu_U[] PROGMEM = { "Volume" };
const char menu_U2[] PROGMEM = { "Step" };
const char menu_U3[] PROGMEM = { "Mode" };
const char menu_U4[] PROGMEM = { "BFO" };
const char menu_U5[] PROGMEM = { "BW" };
const char menu_U6[] PROGMEM = { "AGC/Att" };
const char menu_U7[] PROGMEM = { "SoftMute" };
const char menu_U8[] PROGMEM = { "Seek Up" };
const char menu_U9[] PROGMEM = { "Seek Down" };

const char* const menu[] PROGMEM = { 
  menu_U, menu_U2, menu_U3, menu_U4, menu_U5, menu_U6, menu_U7, menu_U8, menu_U9 
};
int8_t menuIdx = 0;
const int lastMenu = 8;
int8_t currentMenuCmd = -1;

typedef struct
{
  uint8_t idx;
  const char *desc;
} Bandwidth;

int8_t bwIdxSSB = 4;
const int8_t maxSsbBw = 5;
Bandwidth bandwidthSSB[] = {
  { 4, "0.5" },
  { 5, "1.0" },
  { 0, "1.2" },
  { 1, "2.2" },
  { 2, "3.0" },
  { 3, "4.0" }
};

int8_t bwIdxAM = 4;
const int8_t maxAmBw = 6;
Bandwidth bandwidthAM[] = {
  { 4, "1.0" },
  { 5, "1.8" },
  { 3, "2.0" },
  { 6, "2.5" },
  { 2, "3.0" },
  { 1, "4.0" },
  { 0, "6.0" }
};

int8_t bwIdxFM = 0;
const int8_t maxFmBw = 4;
Bandwidth bandwidthFM[] = {
  { 0, "AUT" },
  { 1, "110" },
  { 2, " 84" },
  { 3, " 60" },
  { 4, " 40" }
};

int tabAmStep[] = { 1, 5, 9, 10, 50, 100 };
const int lastAmStep = (sizeof tabAmStep / sizeof(int)) - 1;
int idxAmStep = 3;

int tabFmStep[] = { 5, 10, 20 };
const int lastFmStep = (sizeof tabFmStep / sizeof(int)) - 1;
int idxFmStep = 1;

uint16_t currentStepIdx = 1;

// Bandmode-Beschreibungen und Bandnamen ins PROGMEM
const char bandMode_U[] PROGMEM = { "FM " };
const char bandMode_U2[] PROGMEM = { "LSB" };
const char bandMode_U3[] PROGMEM = { "USB" };
const char bandMode_U4[] PROGMEM = { "AM " };

const char* const bandModeDesc[] PROGMEM = { 
  bandMode_U, bandMode_U2, bandMode_U3, bandMode_U4 
};
uint8_t currentMode = FM;

typedef struct
{
  const char *bandName;
  uint8_t bandType;
  uint16_t minimumFreq;
  uint16_t maximumFreq;
  uint16_t currentFreq;
  int8_t currentStepIdx;
  int8_t bandwidthIdx;
  uint8_t prefMode;
} Band;

Band band[] = {
  { "UKW", FM_BAND_TYPE, 8700, 10800, 9140, 1, 0, FM },
  { "MW1", MW_BAND_TYPE, 150, 1720, 550, 3, 4, AM },
  { "MW2", MW_BAND_TYPE, 531, 1701, 783, 2, 4, AM },
  { "MW3", MW_BAND_TYPE, 1700, 3500, 2500, 1, 4, AM },
  { "80M", MW_BAND_TYPE, 3500, 3800, 3700, 0, 4, LSB },
  { "SW1", SW_BAND_TYPE, 4000, 5500, 4885, 1, 4, AM },
  { "SW2", SW_BAND_TYPE, 5500, 6999, 6200, 1, 4, AM },
  { "40M", SW_BAND_TYPE, 7000, 7200, 7000, 0, 4, LSB },
  { "SW3", SW_BAND_TYPE, 7200, 8999, 7800, 1, 4, AM },
  { "SW4", SW_BAND_TYPE, 9000, 11000, 9500, 1, 4, AM },
  { "SW5", SW_BAND_TYPE, 11000, 13000, 11900, 1, 4, AM },
  { "SW6", SW_BAND_TYPE, 13000, 14000, 13200, 1, 4, AM },
  { "20M", SW_BAND_TYPE, 14000, 14350, 14000, 0, 4, USB },
  { "SW7", SW_BAND_TYPE, 14350, 17000, 15300, 1, 4, AM },
  { "SW8", SW_BAND_TYPE, 17000, 20999, 18500, 1, 4, AM },
  { "15M", SW_BAND_TYPE, 21000, 21450, 21100, 0, 4, USB },
  { "SW9", SW_BAND_TYPE, 21000, 25999, 22500, 1, 4, AM },
  { "CB ", SW_BAND_TYPE, 26000, 28000, 27500, 0, 4, AM },
  { "10M", SW_BAND_TYPE, 28000, 29700, 28125, 0, 4, USB },
  { "ALL", SW_BAND_TYPE, 150, 30000, 15000, 0, 4, AM }
};

const int lastBand = (sizeof band / sizeof(Band)) - 1;
int bandIdx = 0;

typedef struct
{
  uint8_t appId;
  uint8_t volume;
  uint8_t bandIdx;
  uint8_t currentMode;
  int16_t currentBFO;
  uint8_t disableAgc;
  int8_t agcIdx;
  int8_t agcNdx;
  int8_t softMuteMaxAttIdx;
  uint16_t bandFreq[20];
  int8_t bandStepIdx[20];
  int8_t bandBandwidthIdx[20];
  uint8_t bandMode[20];
} ReceiverState;

uint8_t rssi = 0;
uint8_t volume = DEFAULT_VOLUME;

Rotary encoder = Rotary(ENCODER_PIN_A, ENCODER_PIN_B);
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
SI4735 rx;

byte lcdSignal[8][8] = {
  { B00000, B00000, B00000, B00000, B00000, B11111, B01110, B00100 },
  { B00000, B00000, B00000, B00000, B11111, B01110, B00100, B00100 },
  { B00000, B00000, B00000, B11111, B01110, B00100, B00100, B00100 },
  { B00000, B00000, B11111, B01110, B00100, B00100, B00100, B00100 },
  { B00000, B11111, B01110, B00100, B00100, B00100, B00100, B00100 },
  { B11111, B01110, B00100, B00100, B00100, B00100, B00100, B00100 },
  { B11111, B11111, B01110, B00100, B00100, B00100, B00100, B00100 },
  { B11111, B01110, B00100, B00100, B00100, B00100, B00100, B00000 }
};

byte foxTL[8] = {
  B00011,
  B00111,
  B00111,
  B01111,
  B01111,
  B01111,
  B01111,
  B01111
};

byte foxTR[8] = {
  B11000,
  B11100,
  B11100,
  B11110,
  B11110,
  B11110,
  B11110,
  B11110
};

byte foxBL[8] = {
  B01111,
  B01110,
  B01110,
  B01100,
  B01100,
  B00110,
  B00011,
  B00001
};

byte foxBR[8] = {
  B11110,
  B01110,
  B01110,
  B00110,
  B00110,
  B01100,
  B11000,
  B10000
};

void loadSignalChars() {
  for (int i = 0; i < 8; i++) lcd.createChar(i, lcdSignal[i]);
}

void loadSplashChars() {
  lcd.createChar(0, foxTL);
  lcd.createChar(1, foxTR);
  lcd.createChar(2, foxBL);
  lcd.createChar(3, foxBR);
}

void showFoxSplash() {
  lcd.clear();
  loadSplashChars();

  lcd.setCursor(0, 0);
  lcd.write(byte(0));
  lcd.write(byte(1));

  lcd.setCursor(0, 1);
  lcd.write(byte(2));
  lcd.write(byte(3));

  lcd.setCursor(4, 0);
  lcd.print("Allwellenradio");

  lcd.setCursor(4, 1);
  lcd.print("DIY");

  lcd.setCursor(4, 2);
  lcd.print("by webert15");

  delay(1800);
  lcd.clear();
  loadSignalChars();
}

void normalizeBandMode(uint8_t idx) {
  if (band[idx].bandType == FM_BAND_TYPE) {
    band[idx].prefMode = FM;
    return;
  }

  if (strcmp(band[idx].bandName, "ALL") == 0) {
    band[idx].prefMode = AM;
    return;
  }

  if (band[idx].prefMode != AM && band[idx].prefMode != LSB && band[idx].prefMode != USB)
    band[idx].prefMode = AM;
}

void loadSSBIfNeeded() {
  if (!ssbLoaded) {
    rx.setI2CFastModeCustom(400000);
    rx.loadPatch(ssb_patch_content, size_content, bandwidthSSB[bwIdxSSB].idx);
    rx.setI2CFastModeCustom(100000);
    ssbLoaded = true;
  }
}

uint16_t getCurrentAmSeekSpacing() {
  uint16_t step = tabAmStep[currentStepIdx];
  if (step <= 5) return 5;
  if (step <= 9) return 9;
  return 10;
}

void applyFmOptimizations() {
  rx.setFMDeEmphasis(1);
  rx.setFmSoftMuteMaxAttenuation(4);
  rx.setSeekFmSpacing(tabFmStep[currentStepIdx]);
  rx.setSeekFmRssiThreshold(18);
  rx.setSeekFmSNRThreshold(4);

  rx.setFmBlendRssiStereoThreshold(38);
  rx.setFmBLendRssiMonoThreshold(18);
  rx.setFmBlendSnrStereoThreshold(20);
  rx.setFmBLendSnrMonoThreshold(8);
  rx.setFmBlendMultiPathStereoThreshold(20);
  rx.setFmBlendMultiPathMonoThreshold(60);
}

void applyAmOptimizations() {
  uint16_t minF = band[bandIdx].minimumFreq;
  uint16_t maxF = band[bandIdx].maximumFreq;
  uint16_t seekSpacing = getCurrentAmSeekSpacing();

  rx.setSeekAmSpacing(seekSpacing);

  if (maxF <= 1720) {
    rx.setSeekAmRssiThreshold(20);
    rx.setSeekAmSNRThreshold(10);
  } else if (minF >= 3500 && maxF <= 30000) {
    rx.setSeekAmRssiThreshold(8);
    rx.setSeekAmSNRThreshold(5);
  } else {
    rx.setSeekAmRssiThreshold(12);
    rx.setSeekAmSNRThreshold(6);
  }

  rx.setAMSoftMuteSnrThreshold(8);
  rx.setAMSoftMuteAttackRate(4);
  rx.setAMSoftMuteReleaseRate(24);
  rx.setAMSoftMuteRate(8);
  rx.setAMSoftMuteSlop(1);

  rx.setAmAgcAttackRate(4);
  rx.setAmAgcReleaseRate(64);
}

void saveReceiverState() {
  ReceiverState state;
  state.appId = app_id;
  state.volume = rx.getVolume();
  state.bandIdx = bandIdx;
  state.currentMode = currentMode;
  state.currentBFO = currentBFO;
  state.disableAgc = disableAgc;
  state.agcIdx = agcIdx;
  state.agcNdx = agcNdx;
  state.softMuteMaxAttIdx = softMuteMaxAttIdx;

  band[bandIdx].currentFreq = currentFrequency;
  band[bandIdx].currentStepIdx = currentStepIdx;
  band[bandIdx].prefMode = currentMode;

  for (int i = 0; i <= lastBand; i++) {
    state.bandFreq[i] = band[i].currentFreq;
    state.bandStepIdx[i] = band[i].currentStepIdx;
    state.bandBandwidthIdx[i] = band[i].bandwidthIdx;
    state.bandMode[i] = band[i].prefMode;
  }

  EEPROM.put(eeprom_address, state);
}

bool readReceiverState() {
  ReceiverState state;
  EEPROM.get(eeprom_address, state);

  if (state.appId != app_id)
    return false;

  volume = state.volume;
  bandIdx = state.bandIdx;
  currentMode = state.currentMode;
  currentBFO = state.currentBFO;
  disableAgc = state.disableAgc;
  agcIdx = state.agcIdx;
  agcNdx = state.agcNdx;
  softMuteMaxAttIdx = state.softMuteMaxAttIdx;

  for (int i = 0; i <= lastBand; i++) {
    band[i].currentFreq = state.bandFreq[i];
    band[i].currentStepIdx = state.bandStepIdx[i];
    band[i].bandwidthIdx = state.bandBandwidthIdx[i];
    band[i].prefMode = state.bandMode[i];
    normalizeBandMode(i);
  }

  normalizeBandMode(bandIdx);
  currentMode = band[bandIdx].prefMode;
  currentFrequency = band[bandIdx].currentFreq;

  return true;
}

void applyStepFromCurrentBand() {
  if (band[bandIdx].bandType == FM_BAND_TYPE) {
    currentStepIdx = idxFmStep = band[bandIdx].currentStepIdx;
    rx.setFrequencyStep(tabFmStep[currentStepIdx]);
  } else {
    currentStepIdx = idxAmStep = band[bandIdx].currentStepIdx;
    rx.setFrequencyStep(tabAmStep[currentStepIdx]);
  }
}

void applyBandwidthFromCurrentMode() {
  int bwIdx = band[bandIdx].bandwidthIdx;

  if (currentMode == LSB || currentMode == USB) {
    bwIdxSSB = (bwIdx > maxSsbBw) ? maxSsbBw : bwIdx;
    rx.setSSBAudioBandwidth(bandwidthSSB[bwIdxSSB].idx);
    if (bandwidthSSB[bwIdxSSB].idx == 0 || bandwidthSSB[bwIdxSSB].idx == 4 || bandwidthSSB[bwIdxSSB].idx == 5)
      rx.setSSBSidebandCutoffFilter(0);
    else
      rx.setSSBSidebandCutoffFilter(1);
  } else if (currentMode == AM) {
    bwIdxAM = bwIdx;
    rx.setBandwidth(bandwidthAM[bwIdxAM].idx, 1);
  } else {
    bwIdxFM = bwIdx;
    rx.setFmBandwidth(bandwidthFM[bwIdxFM].idx);
  }
}

void applyCurrentBand() {
  normalizeBandMode(bandIdx);
  currentMode = band[bandIdx].prefMode;

  if (band[bandIdx].bandType == FM_BAND_TYPE) {
    currentMode = FM;
    band[bandIdx].prefMode = FM;
    bfoOn = false;
    ssbLoaded = false;

    rx.setTuneFrequencyAntennaCapacitor(0);
    rx.setFM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, tabFmStep[band[bandIdx].currentStepIdx]);
    rx.setSeekFmLimits(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq);
  } else {
    rx.setTuneFrequencyAntennaCapacitor((band[bandIdx].bandType == MW_BAND_TYPE || band[bandIdx].bandType == LW_BAND_TYPE) ? 0 : 1);

    if (currentMode == LSB || currentMode == USB) {
      loadSSBIfNeeded();
      rx.setSSB(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, tabAmStep[band[bandIdx].currentStepIdx], currentMode);
      rx.setSSBAutomaticVolumeControl(1);
      rx.setSsbSoftMuteMaxAttenuation(softMuteMaxAttIdx);
    } else {
      currentMode = AM;
      band[bandIdx].prefMode = AM;
      ssbLoaded = false;
      bfoOn = false;
      rx.setAM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, tabAmStep[band[bandIdx].currentStepIdx]);
      rx.setAmSoftMuteMaxAttenuation(softMuteMaxAttIdx);
    }

    rx.setAutomaticGainControl(disableAgc, agcNdx);
    rx.setSeekAmLimits(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq);
  }

  applyStepFromCurrentBand();
  applyBandwidthFromCurrentMode();

  if (currentMode == FM) {
    applyFmOptimizations();
  } else if (currentMode == AM) {
    applyAmOptimizations();
  }

  delay(80);
  currentFrequency = band[bandIdx].currentFreq;
  currentStepIdx = band[bandIdx].currentStepIdx;
  rx.setVolume(volume);

  rssi = 0;
}

void resetEepromDelay() {
  elapsedCommand = storeTime = millis();
  itIsTimeToSave = true;
}

void disableCommands() {
  cmdBand = false;
  bfoOn = false;
  cmdVolume = false;
  cmdAgc = false;
  cmdBandwidth = false;
  cmdStep = false;
  cmdMode = false;
  cmdMenu = false;
  cmdSoftMuteMaxAtt = false;
  countClick = 0;
  lcd.setCursor(8, 0);
  lcd.print("VFO ");
}

void rotaryEncoder() {
  uint8_t encoderStatus = encoder.process();
  if (encoderStatus)
    encoderCount = (encoderStatus == DIR_CW) ? 1 : -1;
}

void showMode() {
  char bandMode[5];
  if (currentFrequency < 520)
    strcpy(bandMode, "LW  ");
  else
    strcpy_P(bandMode, (char*)pgm_read_ptr(&bandModeDesc[currentMode]));

  lcd.setCursor(0, 0);
  lcd.print(bandMode);

  if (currentMode != FM) {
    lcd.setCursor(0, 1);
    lcd.print(band[bandIdx].bandName);
  }
}

void showFrequency() {
  char tmp[15];
  char bufferDisplay[15];
  char *unit;
  int col = 6;

  sprintf(tmp, "%5.5u", currentFrequency);
  bufferDisplay[0] = (tmp[0] == '0') ? ' ' : tmp[0];
  bufferDisplay[1] = tmp[1];

  if (rx.isCurrentTuneFM()) {
    bufferDisplay[2] = tmp[2];
    bufferDisplay[3] = '.';
    bufferDisplay[4] = tmp[3];
    unit = (char *)"MHz";
  } else {
    if (currentFrequency < 1000) {
      bufferDisplay[1] = ' ';
      bufferDisplay[2] = tmp[2];
      bufferDisplay[3] = tmp[3];
      bufferDisplay[4] = tmp[4];
    } else {
      bufferDisplay[2] = tmp[2];
      bufferDisplay[3] = tmp[3];
      bufferDisplay[4] = tmp[4];
    }
    unit = (char *)"kHz";
  }

  bufferDisplay[5] = '\0';
  strcat(bufferDisplay, unit);
  lcd.setCursor(col, 1);
  lcd.print(bufferDisplay);
  showMode();
}

void showBandwidth() {
  char *bw;
  char bandwidth[20];

  if (currentMode == LSB || currentMode == USB) {
    bw = (char *)bandwidthSSB[bwIdxSSB].desc;
  } else if (currentMode == AM) {
    bw = (char *)bandwidthAM[bwIdxAM].desc;
  } else {
    bw = (char *)bandwidthFM[bwIdxFM].desc;
  }

  sprintf(bandwidth, "BW:%s", bw);
  lcd.setCursor(14, 3);
  lcd.print(bandwidth);
}

void showRSSI() {
  int rssiAux = 0;

  if (currentMode == FM) {
    lcd.setCursor(18, 0);
    lcd.print((rx.getCurrentPilot()) ? "ST" : "MO");
    lcd.setCursor(0, 1);
    lcd.print("   ");
  }

  if (rssi < 2)
    rssiAux = 4;
  else if (rssi < 4)
    rssiAux = 5;
  else if (rssi < 12)
    rssiAux = 6;
  else if (rssi < 25)
    rssiAux = 7;
  else if (rssi < 50)
    rssiAux = 8;
  else
    rssiAux = 9;

  lcd.setCursor(19, 1);
  lcd.write(rssiAux - 2);
}

void showAgcAtt() {
  char sAgc[15];
  if (disableAgc == 0 && agcNdx == 0)
    strcpy(sAgc, "AGC");
  else
    sprintf(sAgc, "At:%2.2d", agcNdx);

  lcd.setCursor(0, 3);
  lcd.print(sAgc);
}

void showStep() {
  char sStep[15];
  sprintf(sStep, "St:%3d", (currentMode == FM) ? (tabFmStep[currentStepIdx] * 10) : tabAmStep[currentStepIdx]);
  lcd.setCursor(6, 3);
  lcd.print(sStep);
}

void showBFO() {
  char bfo[15];
  if (currentBFO > 0)
    sprintf(bfo, "BFO:+%4.4d", currentBFO);
  else
    sprintf(bfo, "BFO: %4.4d", currentBFO);

  lcd.setCursor(0, 2);
  lcd.print(bfo);
}

void showVolume() {
  char volAux[12];
  sprintf(volAux, "VOL:%2u", rx.getVolume());
  lcd.setCursor(14, 2);
  lcd.print(volAux);
}

void showSoftMute() {
  char sMute[18];
  sprintf(sMute, "Soft Mute: %2d", softMuteMaxAttIdx);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(sMute);
}

void showStatus() {
  lcd.clear();
  showFrequency();
  showRSSI();
  showVolume();
  showAgcAtt();
  showStep();
  showBandwidth();
  if (currentMode == LSB || currentMode == USB)
    showBFO();
}

void setBand(int8_t up_down) {
  band[bandIdx].currentFreq = currentFrequency;
  band[bandIdx].currentStepIdx = currentStepIdx;
  band[bandIdx].prefMode = currentMode;

  if (up_down == 1)
    bandIdx = (bandIdx < lastBand) ? (bandIdx + 1) : 0;
  else
    bandIdx = (bandIdx > 0) ? (bandIdx - 1) : lastBand;

  applyCurrentBand();
  showStatus();
  lcd.setCursor(8, 0);
  lcd.print("Band");
  delay(MIN_ELAPSED_TIME);
}

void doBandwidth(int8_t v) {
  if (currentMode == LSB || currentMode == USB) {
    bwIdxSSB = (v == 1) ? bwIdxSSB + 1 : bwIdxSSB - 1;
    if (bwIdxSSB > maxSsbBw) bwIdxSSB = 0;
    else if (bwIdxSSB < 0) bwIdxSSB = maxSsbBw;
    band[bandIdx].bandwidthIdx = bwIdxSSB;
  } else if (currentMode == AM) {
    bwIdxAM = (v == 1) ? bwIdxAM + 1 : bwIdxAM - 1;
    if (bwIdxAM > maxAmBw) bwIdxAM = 0;
    else if (bwIdxAM < 0) bwIdxAM = maxAmBw;
    band[bandIdx].bandwidthIdx = bwIdxAM;
  } else {
    bwIdxFM = (v == 1) ? bwIdxFM + 1 : bwIdxFM - 1;
    if (bwIdxFM > maxFmBw) bwIdxFM = 0;
    else if (bwIdxFM < 0) bwIdxFM = maxFmBw;
    band[bandIdx].bandwidthIdx = bwIdxFM;
  }

  applyBandwidthFromCurrentMode();

  if (currentMode == FM)
    applyFmOptimizations();
  else if (currentMode == AM)
    applyAmOptimizations();

  showBandwidth();
  delay(MIN_ELAPSED_TIME);
}

void doAgc(int8_t v) {
  agcIdx = (v == 1) ? agcIdx + 1 : agcIdx - 1;
  if (agcIdx < 0) agcIdx = 35;
  else if (agcIdx > 35) agcIdx = 0;

  disableAgc = (agcIdx > 0);
  agcNdx = (agcIdx > 1) ? (agcIdx - 1) : 0;

  rx.setAutomaticGainControl(disableAgc, agcNdx);

  if (currentMode == AM)
    applyAmOptimizations();

  showAgcAtt();
  delay(MIN_ELAPSED_TIME);
  elapsedCommand = millis();
}

void doStep(int8_t v) {
  if (currentMode == FM) {
    idxFmStep = (v == 1) ? idxFmStep + 1 : idxFmStep - 1;
    if (idxFmStep > lastFmStep) idxFmStep = 0;
    else if (idxFmStep < 0) idxFmStep = lastFmStep;
    currentStepIdx = idxFmStep;
    rx.setFrequencyStep(tabFmStep[currentStepIdx]);
    rx.setSeekFmSpacing(tabFmStep[currentStepIdx]);
  } else {
    idxAmStep = (v == 1) ? idxAmStep + 1 : idxAmStep - 1;
    if (idxAmStep > lastAmStep) idxAmStep = 0;
    else if (idxAmStep < 0) idxAmStep = lastAmStep;
    currentStepIdx = idxAmStep;
    rx.setFrequencyStep(tabAmStep[currentStepIdx]);

    if (currentMode == AM)
      rx.setSeekAmSpacing(getCurrentAmSeekSpacing());
  }

  band[bandIdx].currentStepIdx = currentStepIdx;
  showStep();
  delay(MIN_ELAPSED_TIME);
  elapsedCommand = millis();
}

void doMode(int8_t v) {
  if (currentMode == FM) return;

  normalizeBandMode(bandIdx);

  if (strcmp(band[bandIdx].bandName, "ALL") == 0) {
    currentMode = AM;
  } else {
    if (v == 1) {
      if (currentMode == AM) currentMode = LSB;
      else if (currentMode == LSB) currentMode = USB;
      else currentMode = AM;
    } else {
      if (currentMode == AM) currentMode = USB;
      else if (currentMode == USB) currentMode = LSB;
      else currentMode = AM;
    }
  }

  band[bandIdx].prefMode = currentMode;
  band[bandIdx].currentFreq = currentFrequency;
  band[bandIdx].currentStepIdx = currentStepIdx;

  applyCurrentBand();
  showStatus();
  delay(MIN_ELAPSED_TIME);
  elapsedCommand = millis();
}

void doVolume(int8_t v) {
  if (v == 1) rx.volumeUp();
  else rx.volumeDown();

  showVolume();
  delay(MIN_ELAPSED_TIME);
}

void showFrequencySeek(uint16_t freq) {
  currentFrequency = freq;
  showFrequency();
}

void doSeek() {
  if (currentMode == LSB || currentMode == USB) return;

  lcd.clear();

  if (currentMode == FM) {
    rx.setSeekFmSpacing(tabFmStep[currentStepIdx]);
    rx.setSeekFmLimits(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq);
    rx.setSeekFmRssiThreshold(18);
    rx.setSeekFmSNRThreshold(4);
  } else {
    rx.setSeekAmLimits(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq);
    rx.setSeekAmSpacing(getCurrentAmSeekSpacing());

    if (band[bandIdx].maximumFreq <= 1720) {
      rx.setSeekAmRssiThreshold(20);
      rx.setSeekAmSNRThreshold(10);
    } else if (band[bandIdx].minimumFreq >= 3500) {
      rx.setSeekAmRssiThreshold(8);
      rx.setSeekAmSNRThreshold(5);
    } else {
      rx.setSeekAmRssiThreshold(12);
      rx.setSeekAmSNRThreshold(6);
    }
  }

  rx.seekStationProgress(showFrequencySeek, seekDirection);
  showStatus();
  currentFrequency = rx.getFrequency();
  band[bandIdx].currentFreq = currentFrequency;
}

void doSoftMute(int8_t v) {
  softMuteMaxAttIdx = (v == 1) ? softMuteMaxAttIdx + 1 : softMuteMaxAttIdx - 1;
  if (softMuteMaxAttIdx > 32) softMuteMaxAttIdx = 0;
  else if (softMuteMaxAttIdx < 0) softMuteMaxAttIdx = 32;

  if (currentMode == LSB || currentMode == USB)
    rx.setSsbSoftMuteMaxAttenuation(softMuteMaxAttIdx);
  else if (currentMode == AM)
    rx.setAmSoftMuteMaxAttenuation(softMuteMaxAttIdx);
  else
    rx.setFmSoftMuteMaxAttenuation(softMuteMaxAttIdx);

  showSoftMute();
  elapsedCommand = millis();
}

void showCommandStatus(char *currentCmd) {
  lcd.setCursor(8, 0);
  lcd.print(currentCmd);
}

void showMenu() {
  lcd.clear();
  lcd.setCursor(0, 1);
  char buf[12];
  strcpy_P(buf, (char*)pgm_read_ptr(&menu[menuIdx]));
  lcd.print(buf);
  showCommandStatus((char *)"Menu");
}

void doMenu(int8_t v) {
  menuIdx = (v == 1) ? menuIdx + 1 : menuIdx - 1;
  if (menuIdx > lastMenu) menuIdx = 0;
  else if (menuIdx < 0) menuIdx = lastMenu;

  showMenu();
  delay(MIN_ELAPSED_TIME);
  elapsedCommand = millis();
}

void doCurrentMenuCmd() {
  disableCommands();

  switch (currentMenuCmd) {
    case 0: cmdVolume = true; showVolume(); break;
    case 1: cmdStep = true; showStep(); break;
    case 2: cmdMode = true; lcd.clear(); showMode(); break;
    case 3: bfoOn = true; if (currentMode == LSB || currentMode == USB) showBFO(); break;
    case 4: cmdBandwidth = true; showBandwidth(); break;
    case 5: cmdAgc = true; showAgcAtt(); break;
    case 6: cmdSoftMuteMaxAtt = true; showSoftMute(); break;
    case 7: seekDirection = 1; doSeek(); break;
    case 8: seekDirection = 0; doSeek(); break;
    default: showStatus(); break;
  }

  currentMenuCmd = -1;
  elapsedCommand = millis();
}

void setup() {
  pinMode(ENCODER_PUSH_BUTTON, INPUT_PULLUP);
  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);
  lcd.begin(20, 4);

  showFoxSplash();

  if (digitalRead(ENCODER_PUSH_BUTTON) == LOW) {
    ReceiverState emptyState = {0};
    EEPROM.put(eeprom_address, emptyState);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("EEPROM RESETED");
    delay(1500);
  }

  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), rotaryEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), rotaryEncoder, CHANGE);

  rx.setI2CFastModeCustom(100000);
  rx.getDeviceI2CAddress(RESET_PIN);
  rx.setup(RESET_PIN, MW_BAND_TYPE);
  delay(300);

  if (!readReceiverState()) {
    volume = DEFAULT_VOLUME;
    bandIdx = 0;
    currentMode = FM;
  }

  disableAgc = 1;
  agcNdx = 0;
  agcIdx = 1;

  applyCurrentBand();
  showStatus();
}

void loop() {
  if (encoderCount != 0) {
    if (bfoOn && (currentMode == LSB || currentMode == USB)) {
      currentBFO = (encoderCount == 1) ? (currentBFO + currentBFOStep) : (currentBFO - currentBFOStep);
      rx.setSSBBfo(currentBFO);
      showBFO();
    } else if (cmdMenu) doMenu(encoderCount);
    else if (cmdMode) doMode(encoderCount);
    else if (cmdStep) doStep(encoderCount);
    else if (cmdAgc) doAgc(encoderCount);
    else if (cmdBandwidth) doBandwidth(encoderCount);
    else if (cmdVolume) doVolume(encoderCount);
    else if (cmdSoftMuteMaxAtt) doSoftMute(encoderCount);
    else if (cmdBand) setBand(encoderCount);
    else {
      if (currentMode == LSB || currentMode == USB)
        rx.setAudioMute(true);

      if (encoderCount == 1) rx.frequencyUp();
      else rx.frequencyDown();

      currentFrequency = rx.getFrequency();
      band[bandIdx].currentFreq = currentFrequency;
      showFrequency();

      if (currentMode == LSB || currentMode == USB)
        rx.setAudioMute(false);
    }

    encoderCount = 0;
    resetEepromDelay();
  } else {
    if (digitalRead(ENCODER_PUSH_BUTTON) == LOW) {
      countClick++;

      if (cmdMenu) {
        currentMenuCmd = menuIdx;
        doCurrentMenuCmd();
      } else if (countClick == 1) {
        if (cmdStep || cmdBandwidth || cmdAgc || cmdVolume || cmdSoftMuteMaxAtt || cmdMode || cmdBand) {
          disableCommands();
          showStatus();
          showCommandStatus((char *)"VFO ");
        } else {
          cmdBand = !cmdBand;
          showCommandStatus((char *)"Band");
        }
      } else {
        cmdMenu = !cmdMenu;
        if (cmdMenu) showMenu();
      }

      delay(MIN_ELAPSED_TIME);
      elapsedCommand = millis();
    }
  }

  if ((millis() - elapsedRSSI) > MIN_ELAPSED_RSSI_TIME * 6) {
    rx.getCurrentReceivedSignalQuality();
    int aux = rx.getCurrentRSSI();
    if (rssi != aux && !(cmdStep || cmdBandwidth || cmdAgc || cmdVolume || cmdSoftMuteMaxAtt || cmdMode)) {
      rssi = aux;
      showRSSI();
    }
    elapsedRSSI = millis();
  }

  if ((millis() - elapsedCommand) > ELAPSED_COMMAND) {
    if (currentMode == LSB || currentMode == USB) {
      bfoOn = false;
      showBFO();
    }
    showStatus();
    disableCommands();
    elapsedCommand = millis();
  }

  if ((millis() - elapsedClick) > ELAPSED_CLICK) {
    countClick = 0;
    elapsedClick = millis();
  }

  if (itIsTimeToSave) {
    if ((millis() - storeTime) > STORE_TIME) {
      saveReceiverState();
      storeTime = millis();
      itIsTimeToSave = false;
    }
  }

  delay(5);
}
