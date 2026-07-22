// Combined Arduino data logger with SD card, DHT11, MQ-2 gas sensor, and LCD display
// Values displayed on LCD and written to SD card every 10 seconds

#include <SPI.h>        // Include SPI library (needed for the SD card)
#include <SD.h>         // Include SD library
#include <DHT.h>        // Include DHT sensor library
#include <LiquidCrystal.h> // Include LCD library

// SD Card Configuration
File dataFile;
const int chipSelect = 10; // SD card CS pin (change according to your setup)

// DHT11 Configuration
#define DHTPIN 8            // DHT11 data pin connected to Arduino pin 8
#define DHTTYPE DHT11       // DHT11 sensor is used
DHT dht(DHTPIN, DHTTYPE);   // Initialize DHT library

// MQ-2 Gas Sensor Configuration
#define MQ2_PIN A0          // MQ-2 analog output connected to A0 (change if needed)
#define MQ2_DIGITAL_PIN 9   // MQ-2 digital output connected to pin 9

// LCD Configuration (RS, E, D4, D5, D6, D7)
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);

// Variables for LCD display
char temperature[] = "Temp = 00.0 C  ";
char humidity[]    = "RH   = 00.0 %  ";
char gasLevel[]    = "Gas  = 0000   ";
char gasStatus[]   = "Gas: Normal   ";

// Variables for logging
uint16_t line = 1;
unsigned long previousMillis = 0;
const long interval = 10000; // 10 seconds interval for logging to SD card

// Gas sensor calibration variables
int gasSensorValue = 0;
float gasVoltage = 0;
bool gasDetected = false;

// Threshold for gas detection (adjust based on your sensor and environment)
const int GAS_THRESHOLD = 150; // Analog value threshold for gas detection

void setup() {
  // Initialize serial communication for debugging
  Serial.begin(9600);
  while (!Serial)
    ; // wait for serial port to connect (for native USB port only)
  
  // Configure MQ-2 digital pin as input
  pinMode(MQ2_DIGITAL_PIN, INPUT);
  
  // Initialize LCD
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  
  // Initialize DHT sensor
  dht.begin();
  
  // Initialize SD card
  Serial.print("Initializing SD card...");
  lcd.setCursor(0, 1);
  lcd.print("SD Card Init...");
  
  if (!SD.begin(chipSelect)) {
    Serial.println("SD card initialization failed!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SD Card Error!");
    while (1); // Halt if SD card fails
  }
  Serial.println("SD card initialization done.");
  
  // Write header to SD card
  dataFile = SD.open("SensorLog.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println("=== Sensor Data Log ===");
    dataFile.println("Line,Temp(C),Humidity(%),Gas(Analog),Gas(Digital)");
    dataFile.close();
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready!");
  delay(2000);
  lcd.clear();
}

void loop() {
  // Read DHT11 sensor
  float RH = dht.readHumidity();
  float Temp = dht.readTemperature();
  
  // Read MQ-2 gas sensor
  gasSensorValue = analogRead(MQ2_PIN);
  gasVoltage = (gasSensorValue / 1024.0) * 5.0; // Convert to voltage
  gasDetected = digitalRead(MQ2_DIGITAL_PIN);   // Read digital output (LOW if gas detected)
  
  // Check if DHT sensor reads failed
  if (isnan(RH) || isnan(Temp)) {
    lcd.clear();
    lcd.setCursor(3, 0);
    lcd.print("Sensor");
    lcd.setCursor(1, 1);
    lcd.print("DHT11 Error!");
    Serial.println("Failed to read from DHT sensor!");
    delay(1000); // Wait before retrying
    return;
  }
  
  // Update LCD display with current readings
  updateLCD(Temp, RH, gasSensorValue, gasDetected);
  
  // Check if it's time to write to SD card (every 10 seconds)
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    writeToSDCard(Temp, RH, gasSensorValue, gasDetected);
    
    // Also print to Serial monitor for debugging
    Serial.print(line);
    Serial.print(":    Temp = ");
    Serial.print(Temp);
    Serial.print("°C,    RH = ");
    Serial.print(RH);
    Serial.print("%,    Gas = ");
    Serial.print(gasSensorValue);
    Serial.print(" (");
    if (gasDetected == LOW) {
      Serial.print("GAS DETECTED");
    } else {
      Serial.print("Normal");
    }
    Serial.println(")");
  }
  
  delay(100); // Small delay to prevent excessive CPU usage
}

// Function to update LCD display
void updateLCD(float Temp, float RH, int gasValue, bool gasDetected) {
  // Format temperature for LCD
  int tempInt = (int)Temp;
  temperature[7] = (tempInt / 10) + 48;      // Tens digit
  temperature[8] = (tempInt % 10) + 48;      // Ones digit
  temperature[11] = 223;                     // Degree symbol (℃)
  
  // Format humidity for LCD
  int humidInt = (int)RH;
  humidity[7] = (humidInt / 10) + 48;        // Tens digit
  humidity[8] = (humidInt % 10) + 48;        // Ones digit
  
  // Format gas sensor value for LCD (4 digits)
  gasLevel[7] = (gasValue / 1000) + 48;      // Thousands digit
  gasLevel[8] = ((gasValue / 100) % 10) + 48; // Hundreds digit
  gasLevel[9] = ((gasValue / 10) % 10) + 48;  // Tens digit
  gasLevel[10] = (gasValue % 10) + 48;        // Ones digit
  
  // Display on LCD - First line (Temperature and Humidity)
  lcd.setCursor(0, 0);
  lcd.print(temperature);
  lcd.setCursor(12, 0);
  lcd.print(humidity);
  lcd.setCursor(11, 0);
  lcd.print("|");
  
  // Display on LCD - Second line (Gas sensor)
  lcd.setCursor(0, 1);
  lcd.print(gasLevel);
  
  // Display gas status
  lcd.setCursor(9, 1);
  if (gasDetected == LOW) {
    lcd.print(" ALERT!");
    // Optional: Blink LCD backlight or show warning
  } else {
    lcd.print(" Normal");
  }
}

// Function to write data to SD card
void writeToSDCard(float Temp, float RH, int gasValue, bool gasDetected) {
  dataFile = SD.open("SensorLog.txt", FILE_WRITE);
  
  if (dataFile) {
    // Write data in CSV format for easy importing to spreadsheets
    dataFile.print(line);
    dataFile.print(",");
    dataFile.print(Temp);
    dataFile.print(",");
    dataFile.print(RH);
    dataFile.print(",");
    dataFile.print(gasValue);
    dataFile.print(",");
    if (gasDetected == LOW) {
      dataFile.println("ALERT");
    } else {
      dataFile.println("NORMAL");
    }
    dataFile.close();
    line++; // Increment line counter
  } else {
    Serial.println("Error opening SensorLog.txt");
    // Show error on LCD briefly
    lcd.setCursor(0, 0);
    lcd.print("SD Write Error!");
    delay(500);
    // Restore LCD display
    updateLCD(Temp, RH, gasValue, gasDetected);
  }
}