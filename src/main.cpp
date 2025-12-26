#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_AHTX0.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_sleep.h>
#include "secrets.h"

// --- Налаштування пінів ---
#define I2C_SDA 8
#define I2C_SCL 9
#define BAT_PIN 3        // Пін для зчитування напруги (ADC)

// --- Налаштування сну ---
#define TIME_TO_SLEEP  30        
#define uS_TO_S_FACTOR 1000000ULL 

// --- Дисплей ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* mqtt_server = MQTT_SERVER;
const char* mqtt_topic_sensor = "home/livingroom/sensor";
const char* mqtt_topic_set = "home/livingroom/display/set"; // Топік для керування
const char* mqtt_user = MQTT_USER; // Якщо потрібена автентифікація
const char* mqtt_pass = MQTT_PASSWORD;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_AHTX0 aht;
WiFiClient espClient;
PubSubClient client(espClient);

// Змінні у пам'яті RTC
RTC_DATA_ATTR float savedTemp = 0;
RTC_DATA_ATTR float savedHum = 0;
RTC_DATA_ATTR float savedBat = 0; // Зберігаємо вольтаж
// RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR bool isDisplayEnabled = true; // Стан дисплея (за замовчуванням увімкнено)

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  // Якщо прийшло "OFF" або "0" - вимикаємо
  if (message == "OFF" || message == "0" || message == "false") {
    isDisplayEnabled = false;
  } 
  // Якщо прийшло "ON" або "1" - вмикаємо
  else if (message == "ON" || message == "1" || message == "true") {
    isDisplayEnabled = true;
  }
}

void connectAndSync() {
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500); attempts++; }

  if (WiFi.status() == WL_CONNECTED) {
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback); // Встановлюємо функцію слухача

    if (client.connect("ESP32_Sensor", mqtt_user, mqtt_pass)) {
      
      // 1. Підписуємось на топік керування
      client.subscribe(mqtt_topic_set);
      
      // 2. Даємо час MQTT брокеру надіслати нам "утримане" повідомлення
      // Це критично важливо! Без loop() і затримки ми не встигнемо отримати команду.
      for(int i=0; i<10; i++) {
        client.loop(); 
        delay(500);
      }

      // 3. Відправляємо дані сенсорів
      String json = "{";
      json += "\"temperature\":" + String(savedTemp, 1) + ",";
      json += "\"humidity\":" + String(savedHum, 1) + ",";
      json += "\"voltage\":" + String(savedBat, 2) + ",";
      // Додамо статус дисплея, щоб HA знав поточний стан
      json += "\"display_status\":\"" + String(isDisplayEnabled ? "ON" : "OFF") + "\"";
      json += "}";
      bool success = client.publish(mqtt_topic_sensor, json.c_str(), true);

      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 10);
      if(success) {
        display.print("Data Sent!");
      } else {
        display.print("Send Fail!");
      }
      display.display();

      delay(1500); // Час на відправку
    } else {
      // Помилка підключення до MQTT
      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 10);
      display.print("MQTT Fail");
      display.display();
      delay(1500);
    }
  } else {
    // Помилка підключення до WiFi
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 10);
    display.print("WiFi Fail");
    display.display();
    delay(1500);
  }
}

// Функція для читання та розрахунку напруги
float readBatteryVoltage() {
  // Робимо кілька замірів для точності (усереднення)
  long sum = 0;
  for(int i=0; i<10; i++) {
    sum += analogRead(BAT_PIN);
    delay(2);
  }
  float raw = sum / 10.0;

  // РОЗРАХУНОК:
  // 3.3 - опорна напруга ESP32
  // 4095 - макс значення 12-бітного ADC
  // 2 - коефіцієнт дільника (бо резистори однакові, ми ділили напругу на 2)
  // 1.05 - коефіцієнт калібрування (підбирається вручну!)
  
  float voltage = (raw / 4095.0) * 3.3 * 2.0 * 0.9; 
  
  return voltage;
}

void drawScreen() {
  display.clearDisplay();
  
  // --- Верхній рядок (Батарея) ---
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  // Малюємо батарейку текстом
  display.print("Bat:");
  display.print(savedBat, 2); // 2 знаки після коми
  display.print("V");

  // Можна додати індикатор % (приблизно)
  // 4.2V = 100%, 3.3V = 0%
  int percent = map((long)(savedBat * 100), 330, 420, 0, 100);
  if(percent < 0) percent = 0;
  if(percent > 100) percent = 100;
  
  display.setCursor(80, 0);
  display.print(percent);
  display.print("%");

  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // --- Основні дані ---
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(savedTemp, 1);
  display.print(" C");
  display.drawCircle(70, 22, 2, SSD1306_WHITE);

  display.setCursor(0, 45);
  display.print(savedHum, 1);
  display.print(" %");

  display.display();
}

void setup() {
  setCpuFrequencyMhz(80);
  
  // Налаштування ADC (АЦП)
  analogReadResolution(12); // 12 біт (значення від 0 до 4095)
  
  Wire.begin(I2C_SDA, I2C_SCL);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    return; 
  }

  if (!aht.begin()) {
    // Якщо датчик не знайдено (можна додати обробку)
  } else {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    savedTemp = temp.temperature;
    savedHum = humidity.relative_humidity;
  }
  
  // Зчитуємо батарею
  savedBat = readBatteryVoltage();

  if(isDisplayEnabled) {
    // display.ssd1306_command(SSD1306_DISPLAYON);
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(1);
    drawScreen();
  } else {
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
  }

  // 2. Підключення до мережі та перевірка нових команд
  // connectAndSync();

  // 3. Фінальна перевірка перед сном
  // if (!isDisplayEnabled) {
  //    // Переконуємось, що екран вимкнено перед сном
  //    display.clearDisplay();
  //    display.display();
  //    display.ssd1306_command(SSD1306_DISPLAYOFF); 
  // } else {
  //    // Якщо увімкнено - оновлюємо дані (показуємо, що відправлено)
  //    display.ssd1306_command(SSD1306_DISPLAYON);
  //    // Можна домалювати галочку "Sent" тут
  // }

  // bootCount++;
  // drawScreen();

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {
  // Не використовується в Deep Sleep
}