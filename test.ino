/*
Environmental Monitoring Data Logger
Sensors
DHT11   -> D8
MQ-2    -> A0 (Digital Input)
MQ-3    -> A1 (Digital Input)
MQ-135  -> A2 (Digital Input)
LCD--RS,E,D4,D5,D6,D7 --7,6,5,4,3,2
SD Card
CS -> D10
Logs every 10 seconds
*/
#include <SPI.h>
#include <SD.h>
#include <DHT.h>
#include <LiquidCrystal.h>

//SD CARD
File dataFile;
const int chipSelect = 10;

//DHT11
#define DHTPIN 8
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

//MQ SENSORS
#define MQ2_PIN A0
#define MQ3_PIN A1
#define MQ135_PIN A2

//LCD
LiquidCrystal lcd(7,6,5,4,3,2);

//VARIABLES
unsigned long previousMillis = 0;
const unsigned long interval = 1000;
unsigned int lineNumber = 1;
void setup()
{
  Serial.begin(9600);
  dht.begin();
  pinMode(MQ2_PIN, INPUT);
  pinMode(MQ3_PIN, INPUT);
  pinMode(MQ135_PIN, INPUT);
  lcd.begin(16,2);
  lcd.print("Initializing");
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
  dataFile = SD.open("SensorLog.txt", FILE_WRITE);
  if(dataFile)
  {
    dataFile.println("Environmental Sensor Log");
    dataFile.println("Line,Temperature(C),Humidity(%),MQ2,MQ3,MQ135");
    dataFile.close();
  }
  lcd.clear();
  lcd.print("System Ready");
  delay(2000);
  lcd.clear();
}
void loop()
{
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  bool mq2 = digitalRead(MQ2_PIN);
  bool mq3 = digitalRead(MQ3_PIN);
  bool mq135 = digitalRead(MQ135_PIN);
  if(isnan(humidity) || isnan(temperature))
  {
    lcd.clear();
    lcd.print("DHT ERROR");
    Serial.println("DHT Read Error");
    delay(1000);
    return;
  }
  displayLCD(temperature,humidity,mq2,mq3,mq135);
  unsigned long currentMillis = millis();
  if(currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;
    saveData(temperature,humidity,mq2,mq3,mq135);
    printSerial(temperature,humidity,mq2,mq3,mq135);
  }
  delay(500);
}
void displayLCD(float temp,float hum,bool mq2,bool mq3,bool mq135)
{
  static bool screen=false;
  lcd.clear();
  if(!screen)
  {
      lcd.setCursor(0,0);
      lcd.print("T:");
      lcd.print((int)temp);
      lcd.write(byte(223));
      lcd.print("C ");
      lcd.print("H:");
      lcd.print((int)hum);
      lcd.print("%");
      lcd.setCursor(0,1);
      lcd.print("MQ2:");
      if(mq2)
          lcd.print("DETECTED");
      else
          lcd.print("NORMAL");
  }
  else
  {
      lcd.setCursor(0,0);
      lcd.print("MQ3:");
      if(mq3)
          lcd.print("DETECTED");
      else
          lcd.print("NORMAL");
      lcd.setCursor(0,1);
      lcd.print("MQ135:");
      if(mq135)
          lcd.print("DETECTED");
      else
          lcd.print("NORMAL");
  }
  screen=!screen;
}
void saveData(float temp,float hum,bool mq2,bool mq3,bool mq135)
{
  dataFile = SD.open("SensorLog.txt", FILE_WRITE);
  if(dataFile)
  {
      dataFile.print(lineNumber++);
      dataFile.print(",");
      dataFile.print(temp);
      dataFile.print(",");
      dataFile.print(hum);
      dataFile.print(",");
      dataFile.print(mq2 ? "DETECTED" : "NORMAL");
      dataFile.print(",");
      dataFile.print(mq3 ? "DETECTED" : "NORMAL");
      dataFile.print(",");
      dataFile.println(mq135 ? "DETECTED" : "NORMAL");
      dataFile.close();
  }
  else
  {
      Serial.println("SD Write Error");
  }
}
void printSerial(float temp,float hum,bool mq2,bool mq3,bool mq135)
{
  Serial.print("Temperature : ");
  Serial.print(temp);
  Serial.println(" C");
  Serial.print("Humidity    : ");
  Serial.print(hum);
  Serial.println(" %");
  Serial.print("MQ2         : ");
  Serial.println(mq2 ? "DETECTED" : "NORMAL");
  Serial.print("MQ3         : ");
  Serial.println(mq3 ? "DETECTED" : "NORMAL");
  Serial.print("MQ135       : ");
  Serial.println(mq135 ? "DETECTED" : "NORMAL");
}