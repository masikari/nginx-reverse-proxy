/*
   =============================================================================
   Electronic Nose (e-Nose) for Sorghum Fungal Disease Detection
   Calibrated VOC PPM Engine & LCD Display Update
   Hardware: Arduino Uno | AHT20 | ENS160 | LCD 16x2 | MQ-2, MQ-3, MQ-135
   =============================================================================
*/

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS160.h>

// ===== HARDWARE PINS =====
const uint8_t WARMUP_BTN = 2;   // Push-button wired between Pin 2 and GND
const uint8_t SD_CS      = 10;
const uint8_t RED_LED    = 5;
const uint8_t GREEN_LED  = 6;
const uint8_t BUZZER     = 7;

// ===== SENSOR CALIBRATION SLOPES (k = Volts per PPM) =====
struct CalibrationSlopes {
  float k_mq2_hexanal   = 0.0150; // V/PPM for MQ-2 against Hexanal
  float k_mq3_ethanol   = 0.0085; // V/PPM for MQ-3 against Ethanol
  float k_mq135_acetone = 0.0120; // V/PPM for MQ-135 against Acetone
} cal;

// ===== DISEASE PPM THRESHOLDS =====
struct PPMThresholds {
  float ethanol_ppm   = 15.0; // PPM trigger for Anthracnose
  float hexanal_ppm   = 10.0; // PPM trigger for Leaf Blight
  float acetone_ppm   = 8.0;  // PPM trigger for Gray Leaf Spot
  int   tvoc_ppb      = 200;  // TVOC threshold
} thresh;

enum Disease : uint8_t { HEALTHY, ANTHRACNOSE, LEAF_BLIGHT, GRAY_LEAF_SPOT, UNKNOWN };

// ===== SENSOR DATA STRUCTURES =====
struct SensorReadings {
  float temp = 0.0, hum = 0.0;
  
  // Raw Voltages (0.0V - 5.0V)
  float mq2_volts = 0.0, mq3_volts = 0.0, mq135_volts = 0.0;
  
  // Calculated VOC Concentrations (PPM)
  float ethanol_ppm = 0.0; // MQ-3
  float hexanal_ppm = 0.0; // MQ-2
  float acetone_ppm = 0.0; // MQ-135
  
  int tvoc = 0, eco2 = 0, aqi = 0;
} cur, base;

struct Diagnosis {
  Disease disease = HEALTHY;
  float primary_ppm = 0.0;
  float confidence = 1.0;
  bool confirmed = false;
} diag;

enum SystemState : uint8_t { STATE_WARMUP, STATE_RUNNING };
SystemState sysState = STATE_WARMUP;

// ===== PERIPHERAL OBJECTS =====
LiquidCrystal_I2C lcd(0x27, 16, 2);      
Adafruit_AHTX0 aht;                      
ScioSense_ENS160 ens160(ENS160_I2CADDR_1); 
File logFile;

// ===== TIMING & COUNTERS =====
unsigned long warmupStartTime = 0;
const unsigned long WARMUP_DURATION = 1800000UL; // 30 mins

unsigned long lastLog = 0, lastLCD = 0, lastSerial = 0;
const unsigned long LOG_INT    = 10000; // 10 sec log
const unsigned long LCD_INT    = 2500;  // 2.5 sec LCD page flip
const unsigned long SERIAL_INT = 2000;  // 2 sec Serial

uint16_t recordCount = 0;
uint8_t lcdMode = 0;
bool baselineSet = false;
bool sdAvailable = false;

// ===== HELPER: PRINT DISEASE STRINGS DIRECTLY FROM FLASH =====
void printDiseaseName(Print &out, Disease d) {
  switch (d) {
    case HEALTHY:        out.print(F("Healthy")); break;
    case ANTHRACNOSE:    out.print(F("Anthracnose")); break;
    case LEAF_BLIGHT:    out.print(F("Leaf Blight")); break;
    case GRAY_LEAF_SPOT: out.print(F("Gray Leaf Spot")); break;
    default:             out.print(F("Unknown")); break;
  }
}

// ===== HELPER: FORMAT MILLIS TO EXCEL HH:MM:SS =====
void printFormattedTime(Print &out, unsigned long ms) {
  unsigned long totalSeconds = ms / 1000;
  unsigned long seconds = totalSeconds % 60;
  unsigned long totalMinutes = totalSeconds / 60;
  unsigned long minutes = totalMinutes % 60;
  unsigned long hours = totalMinutes / 60;

  if (hours < 10) out.print('0');
  out.print(hours);
  out.print(':');
  if (minutes < 10) out.print('0');
  out.print(minutes);
  out.print(':');
  if (seconds < 10) out.print('0');
  out.print(seconds);
}

// ===== CONVERT RAW ADC TO VOLTS =====
float adcToVolts(int rawAdc) {
  return (rawAdc * 5.0) / 1023.0;
}

// ===== FATAL ERROR HANDLER =====
void fatalError(const __FlashStringHelper* msg) {
  Serial.print(F("\nCRITICAL ERROR: ")); Serial.println(msg);
  Serial.flush();
  lcd.clear(); 
  lcd.print(F("Init Failed:")); 
  lcd.setCursor(0, 1); 
  lcd.print(msg);
  while (1) {
    digitalWrite(RED_LED, !digitalRead(RED_LED));
    delay(500);
  }
}

// ===== SETUP =====
void setup() {
  pinMode(WARMUP_BTN, INPUT_PULLUP); 
  pinMode(RED_LED, OUTPUT); 
  pinMode(GREEN_LED, OUTPUT); 
  pinMode(BUZZER, OUTPUT);
  
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);

  delay(1000); 

  Serial.begin(9600);
  while (!Serial && millis() < 3000); 
  
  Serial.println(F("\n--- E-NOSE CALIBRATED VOC DETECTOR ---"));

  Wire.begin();
  Wire.setClock(100000); 

  // Init LCD
  lcd.init(); 
  lcd.backlight();
  lcd.clear();
  lcd.print(F("E-Nose System")); 
  lcd.setCursor(0, 1); 
  lcd.print(F("Initializing..."));

  // Init AHT20
  if (!aht.begin(&Wire)) fatalError(F("AHT20 Error"));

  // Init ENS160
  if (!ens160.begin()) fatalError(F("ENS160 Error"));
  ens160.setMode(ENS160_OPMODE_STD);

  // Init SD Card
  pinMode(SD_CS, OUTPUT);
  if (!SD.begin(SD_CS)) {
    Serial.println(F("SD Card not ready. Logging disabled."));
    sdAvailable = false;
  } else {
    sdAvailable = true;
    if (!SD.exists("SORGHUM.CSV")) {
      logFile = SD.open("SORGHUM.CSV", FILE_WRITE);
      if (logFile) {
        logFile.println(F("Record,Timestamp(HH:MM:SS),Temp(C),Hum(%),Hexanal_PPM,Ethanol_PPM,Acetone_PPM,TVOC_ppb,eCO2_ppm,Disease,Primary_PPM"));
        logFile.close();
      }
    }
  }

  warmupStartTime = millis();
  sysState = STATE_WARMUP;
  tone(BUZZER, 2000, 100);
}

// ===== MAIN LOOP =====
void loop() {
  if (sysState == STATE_WARMUP) {
    handleWarmup();
  } else {
    runSamplingPipeline();
  }
}

// ===== WARM-UP & BYPASS =====
void handleWarmup() {
  unsigned long elapsed = millis() - warmupStartTime;

  if (digitalRead(WARMUP_BTN) == LOW) {
    delay(80); // Debounce delay
    if (digitalRead(WARMUP_BTN) == LOW) {
      Serial.println(F("Warm-Up BYPASSED by User."));
      
      // Wait for user to release button before proceeding
      while (digitalRead(WARMUP_BTN) == LOW) {
        delay(10);
      }
      
      finishWarmup();
      return;
    }
  }

  if (elapsed >= WARMUP_DURATION) {
    finishWarmup();
    return;
  }

  if (millis() - lastLCD >= 500) {
    lastLCD = millis();

    unsigned long remainingSec = (WARMUP_DURATION - elapsed) / 1000;
    int mins = remainingSec / 60;
    int secs = remainingSec % 60;

    lcd.setCursor(0, 0);
    lcd.print(F("Warming Up: "));
    if (mins < 10) lcd.print('0');
    lcd.print(mins); lcd.print(':');
    if (secs < 10) lcd.print('0');
    lcd.print(secs);
    lcd.print(F("  "));

    lcd.setCursor(0, 1);
    lcd.print(F("BTN: Skip Warmup"));
  }
}

void finishWarmup() {
  lcd.clear();
  lcd.print(F("Setting Base V0"));
  
  measureBaseline();
  delay(200); // Allow power stabilization
  
  sysState = STATE_RUNNING;
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);
  delay(100);
  
  tone(BUZZER, 1500, 100); delay(150);
  tone(BUZZER, 2000, 150); delay(100);
  
  lcd.clear();
  lastLCD = millis();
  lastSerial = millis();
  lastLog = millis();
}

// ===== SAMPLING & CONVERSION PIPELINE =====
void runSamplingPipeline() {
  readSensors();
  calculatePPM();
  classifyDisease();
  updateLCD();
  updateSerialMonitor();

  if (sdAvailable && (millis() - lastLog >= LOG_INT)) {
    lastLog = millis();
    logData();
  }

  handleAlarm();
  delay(50);
}

// ===== SENSOR READINGS =====
void readSensors() {
  sensors_event_t humidity, temp;
  if (aht.getEvent(&humidity, &temp)) {
    cur.temp = temp.temperature;
    cur.hum  = humidity.relative_humidity;
  }

  cur.mq2_volts   = adcToVolts(analogRead(A0));
  cur.mq3_volts   = adcToVolts(analogRead(A1));
  cur.mq135_volts = adcToVolts(analogRead(A2));

  if (ens160.available()) {
    ens160.measure(false); 
    cur.tvoc = ens160.getTVOC();
    cur.eco2 = ens160.geteCO2();
    cur.aqi  = ens160.getAQI();
  }
}

// ===== BASELINE V0 SNAPSHOT =====
void measureBaseline() {
  readSensors();
  
  base.temp        = cur.temp;
  base.hum         = cur.hum;
  base.mq2_volts   = cur.mq2_volts;
  base.mq3_volts   = cur.mq3_volts;
  base.mq135_volts = cur.mq135_volts;

  baselineSet = true;
  Serial.println(F("Baseline Clean-Air Voltages (V0) Set:"));
  Serial.print(F("  MQ2 V0: ")); Serial.println(base.mq2_volts, 3);
  Serial.print(F("  MQ3 V0: ")); Serial.println(base.mq3_volts, 3);
  Serial.print(F("MQ135 V0: ")); Serial.println(base.mq135_volts, 3);
}

// ===== PPM CALCULATION ENGINE: C = (V - V0) / k =====
void calculatePPM() {
  if (!baselineSet) return;

  // Calculate Delta V (V - V0)
  float dV_mq2   = cur.mq2_volts - base.mq2_volts;
  float dV_mq3   = cur.mq3_volts - base.mq3_volts;
  float dV_mq135 = cur.mq135_volts - base.mq135_volts;

  // Convert Delta V to PPM using slope k (Ensure no negative PPM)
  cur.hexanal_ppm = (dV_mq2 > 0)   ? (dV_mq2 / cal.k_mq2_hexanal)   : 0.0;
  cur.ethanol_ppm = (dV_mq3 > 0)   ? (dV_mq3 / cal.k_mq3_ethanol)   : 0.0;
  cur.acetone_ppm = (dV_mq135 > 0) ? (dV_mq135 / cal.k_mq135_acetone) : 0.0;
}

// ===== PPM-BASED CLASSIFICATION ENGINE =====
void classifyDisease() {
  if (!baselineSet) return;

  float scores[3] = {0.0, 0.0, 0.0}; 

  // Anthracnose (Ethanol Focus)
  if (cur.ethanol_ppm > thresh.ethanol_ppm) {
    scores[0] = cur.ethanol_ppm / thresh.ethanol_ppm;
  }

  // Leaf Blight (Hexanal Focus)
  if (cur.hexanal_ppm > thresh.hexanal_ppm) {
    scores[1] = cur.hexanal_ppm / thresh.hexanal_ppm;
  }

  // Gray Leaf Spot (Acetone Focus)
  if (cur.acetone_ppm > thresh.acetone_ppm) {
    scores[2] = cur.acetone_ppm / thresh.acetone_ppm;
  }

  int maxIdx = 0;
  for (int i = 1; i < 3; i++) {
    if (scores[i] > scores[maxIdx]) maxIdx = i;
  }

  if (scores[maxIdx] < 1.0) {
    diag.disease = HEALTHY;
    diag.primary_ppm = 0.0;
    diag.confidence = 1.0;
    diag.confirmed = false;
  } else {
    diag.disease = (Disease)(maxIdx + 1);
    diag.confirmed = (scores[maxIdx] >= 1.5);
    
    if (diag.disease == ANTHRACNOSE)       diag.primary_ppm = cur.ethanol_ppm;
    else if (diag.disease == LEAF_BLIGHT)  diag.primary_ppm = cur.hexanal_ppm;
    else if (diag.disease == GRAY_LEAF_SPOT) diag.primary_ppm = cur.acetone_ppm;
  }
}

// ===== DISPLAY MANAGEMENT (PPM & INFECTION STATUS) =====
void updateLCD() {
  if (millis() - lastLCD < LCD_INT) return;
  lastLCD = millis();
  lcd.clear();

  switch (lcdMode) {
    case 0: // Temp & Humidity
      lcd.print(F("Temp: ")); lcd.print(cur.temp, 1); lcd.print(F(" C"));
      lcd.setCursor(0, 1); 
      lcd.print(F("Hum : ")); lcd.print(cur.hum, 1); lcd.print(F(" %"));
      break;

    case 1: // MQ-3 Ethanol PPM
      lcd.print(F("Ethanol: ")); lcd.print(cur.ethanol_ppm, 1); lcd.print(F("PPM"));
      lcd.setCursor(0, 1); 
      lcd.print(cur.ethanol_ppm > thresh.ethanol_ppm ? F("Risk:Anthracnose") : F("Status: Normal"));
      break;

    case 2: // MQ-2 Hexanal PPM
      lcd.print(F("Hexanal: ")); lcd.print(cur.hexanal_ppm, 1); lcd.print(F("PPM"));
      lcd.setCursor(0, 1); 
      lcd.print(cur.hexanal_ppm > thresh.hexanal_ppm ? F("Risk:Leaf Blight") : F("Status: Normal"));
      break;

    case 3: // MQ-135 Acetone PPM
      lcd.print(F("Acetone: ")); lcd.print(cur.acetone_ppm, 1); lcd.print(F("PPM"));
      lcd.setCursor(0, 1); 
      lcd.print(cur.acetone_ppm > thresh.acetone_ppm ? F("Risk:Gray Spot") : F("Status: Normal"));
      break;

    case 4: // TVOC & eCO2
      lcd.print(F("TVOC: ")); lcd.print(cur.tvoc); lcd.print(F(" ppb"));
      lcd.setCursor(0, 1); 
      lcd.print(F("eCO2: ")); lcd.print(cur.eco2); lcd.print(F(" ppm"));
      break;

    case 5: // Disease Diagnosis & Primary Concentration
      lcd.print(F("Dx:")); 
      printDiseaseName(lcd, diag.disease);
      lcd.setCursor(0, 1);
      if (diag.disease == HEALTHY) {
        lcd.print(F("Crop Health: OK"));
      } else {
        lcd.print(F("Con: ")); lcd.print(diag.primary_ppm, 1); lcd.print(F(" PPM"));
      }
      break;
  }
  lcdMode = (lcdMode + 1) % 6;
}

// ===== SERIAL MONITOR DEBUG OUTPUT =====
void updateSerialMonitor() {
  if (millis() - lastSerial < SERIAL_INT) return;
  lastSerial = millis();

  Serial.print(F("Ethanol: ")); Serial.print(cur.ethanol_ppm, 2); Serial.print(F(" PPM | "));
  Serial.print(F("Hexanal: ")); Serial.print(cur.hexanal_ppm, 2); Serial.print(F(" PPM | "));
  Serial.print(F("Acetone: ")); Serial.print(cur.acetone_ppm, 2); Serial.print(F(" PPM | "));
  Serial.print(F("Diagnosis: ")); 
  printDiseaseName(Serial, diag.disease);
  if (diag.disease != HEALTHY) {
    Serial.print(F(" (")); Serial.print(diag.primary_ppm, 1); Serial.print(F(" PPM)"));
  }
  Serial.println();
}

// ===== SD CARD DATA LOGGING =====
void logData() {
  recordCount++;
  logFile = SD.open("SORGHUM.CSV", FILE_WRITE);
  if (logFile) {
    logFile.print(recordCount);                logFile.print(',');
    printFormattedTime(logFile, millis());     logFile.print(',');
    logFile.print(cur.temp, 1);                logFile.print(',');
    logFile.print(cur.hum, 1);                 logFile.print(',');
    logFile.print(cur.hexanal_ppm, 2);         logFile.print(',');
    logFile.print(cur.ethanol_ppm, 2);         logFile.print(',');
    logFile.print(cur.acetone_ppm, 2);         logFile.print(',');
    logFile.print(cur.tvoc);                   logFile.print(',');
    logFile.print(cur.eco2);                   logFile.print(',');
    printDiseaseName(logFile, diag.disease);   logFile.print(',');
    logFile.println(diag.primary_ppm, 2);
    logFile.close();

    digitalWrite(GREEN_LED, LOW); 
    delay(50); 
    digitalWrite(GREEN_LED, HIGH);
  } else {
    Serial.println(F("SD Log Write Error"));
  }
}

// ===== ALARM HANDLER =====
void handleAlarm() {
  if (diag.disease != HEALTHY) {
    digitalWrite(GREEN_LED, LOW);
    if (millis() % 1000 < 500) {
      digitalWrite(RED_LED, HIGH);
      tone(BUZZER, 1000, 100);
    } else {
      digitalWrite(RED_LED, LOW);
    }
  } else {
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
    noTone(BUZZER);
  }
}