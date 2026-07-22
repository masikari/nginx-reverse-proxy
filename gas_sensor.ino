/*
 Environmental Monitoring Data Logger
 Sensors:
 DHT11  -> Digital Pin 8
 MQ-2   -> Analog Pin A0
 MQ-3   -> Analog Pin A1
 MQ-135 -> Analog Pin A2
 LCD:
 RS,E,D4,D5,D6,D7 -> 7,6,5,4,3,2
 SD Card:
 CS -> Pin 10
 Logging interval: 10 seconds
*/
#include <SPI.h>
#include <SD.h>
#include <DHT.h>
#include <LiquidCrystal.h>

//  SD CARD 
File dataFile;
const int chipSelect = 10;

//DHT11
#define DHTPIN 8
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

//GAS SENSORS 
#define MQ2_PIN A0
#define MQ3_PIN A1
#define MQ135_PIN A2

// LCD 
LiquidCrystal lcd(7,6,5,4,3,2);

//VARIABLES 
unsigned long previousMillis = 0;
const unsigned long interval = 10000;
unsigned int lineNumber = 1;

// SETUP
void setup()
{
  Serial.begin(9600);
  lcd.begin(16,2);
  lcd.clear();
  lcd.print("Initializing");
  dht.begin();
  // Initialize SD card
  lcd.setCursor(0,1);
  lcd.print("SD Card...");
  if(!SD.begin(chipSelect))
  {
    lcd.clear();
    lcd.print("SD ERROR");
    Serial.println("SD initialization failed");
    while(1);
  }
  Serial.println("SD Ready");
  // Create log file
  dataFile = SD.open("SensorLog.txt",FILE_WRITE);
  if(dataFile)
  {
    dataFile.println("Environmental Sensor Log");
    dataFile.println(
      "Line,Temperature(C),Humidity(%),MQ2,MQ3,MQ135"
    );
    dataFile.close();
  }
  lcd.clear();
  lcd.print("System Ready");
  delay(2000);
  lcd.clear();
}
// LOOP
void loop()
{
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  int mq2Value = analogRead(MQ2_PIN);
  int mq3Value = analogRead(MQ3_PIN);
  int mq135Value = analogRead(MQ135_PIN);
  // Check DHT
  if(isnan(humidity) || isnan(temperature))
  {
    lcd.clear();
    lcd.print("DHT ERROR");
    Serial.println("DHT read failed");
    delay(1000);

    return;
  }
  displayLCD(
    temperature,
    humidity,
    mq2Value,
    mq3Value,
    mq135Value
  );
  unsigned long currentMillis = millis();
  if(currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;
    saveData(
      temperature,
      humidity,
      mq2Value,
      mq3Value,
      mq135Value
    );
    printSerial(
      temperature,
      humidity,
      mq2Value,
      mq3Value,
      mq135Value
    );
  }
  delay(200);
}

// LCD DISPLAY
void displayLCD(
float temperature,
float humidity,
int mq2,
int mq3,
int mq135)
{
  static bool screen = false;
  lcd.clear();
  if(screen == false)
  {
    lcd.setCursor(0,0);
    lcd.print("T:");
    lcd.print((int)temperature);
    lcd.write(byte(223));
    lcd.print("C ");
    lcd.print("H:");
    lcd.print((int)humidity);
    lcd.print("%");
    lcd.setCursor(0,1);
    lcd.print("MQ2:");
    lcd.print(mq2);
  }
  else
  {
    lcd.setCursor(0,0);
    lcd.print("MQ3:");
    lcd.print(mq3);
    lcd.setCursor(0,1);
    lcd.print("MQ135:");
    lcd.print(mq135);
  }
  screen = !screen;
}
// SD CARD LOGGING
void saveData(
float temperature,
float humidity,
int mq2,
int mq3,
int mq135)
{
  dataFile = SD.open("SensorLog.txt",FILE_WRITE);
  if(dataFile)
  {
    dataFile.print(lineNumber++);
    dataFile.print(",");
    dataFile.print(temperature);
    dataFile.print(",");
    dataFile.print(humidity);
    dataFile.print(",");
    dataFile.print(mq2);
    dataFile.print(",");
    dataFile.print(mq3);
    dataFile.print(",");
    dataFile.println(mq135);
    dataFile.close();
  }
  else
  {
    Serial.println("SD write error");
  }
}
// SERIAL MONITOR
void printSerial(
float temperature,
float humidity,
int mq2,
int mq3,
int mq135)
{
Serial.print("Temperature: ");
Serial.print(temperature);
Serial.println(" C");
Serial.print("Humidity: ");
Serial.print(humidity);
Serial.println(" %");
Serial.print("MQ2: ");
Serial.println(mq2);
Serial.print("MQ3: ");
Serial.println(mq3);
Serial.print("MQ135: ");
Serial.println(mq135);
}