#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_AHTX0.h>
#include "esp_sleep.h"

// --- Налаштування пінів ---
#define I2C_SDA 8
#define I2C_SCL 9
#define BAT_PIN 3        // Пін для зчитування напруги (ADC)

// --- Налаштування сну ---
#define TIME_TO_SLEEP  60        
#define uS_TO_S_FACTOR 1000000ULL 

// --- Дисплей ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_AHTX0 aht;

// Змінні у пам'яті RTC
RTC_DATA_ATTR float savedTemp = 0;
RTC_DATA_ATTR float savedHum = 0;
RTC_DATA_ATTR float savedBat = 0; // Зберігаємо вольтаж
RTC_DATA_ATTR int bootCount = 0;

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
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(1);

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

  bootCount++;
  drawScreen();

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {
  // Не використовується в Deep Sleep
}