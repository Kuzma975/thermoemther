#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_AHTX0.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_sleep.h>
#include <WiFiManager.h> // Встановити через бібліотеки!
#include <Preferences.h> // Вбудована бібліотека для зберігання налаштувань
#include "secrets.h"

// --- Налаштування пінів ---
#define I2C_SDA 8
#define I2C_SCL 9
#define BAT_PIN 3        // Пін для зчитування напруги (ADC)
#define BTN_DISP_PIN  4  // Кнопка перемикання дисплея
#define BTN_CONFIG_PIN 5 // Кнопка входу в налаштування

// --- Налаштування сну ---
#define TIME_TO_SLEEP  30        
#define uS_TO_S_FACTOR 1000000ULL 

// --- Дисплей ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_AHTX0 aht;
WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences; // Для зберігання налаштувань у Flash пам'яті

RTC_DATA_ATTR bool isDisplayEnabled = true; // Стан дисплея (за замовчуванням увімкнено)

char wifi_ssid[32] = WIFI_SSID;
char wifi_pass[20] = WIFI_PASSWORD;
char mqtt_server[40] = MQTT_SERVER;
char mqtt_port[6] = "1883";
char mqtt_topic[40] = "home/livingroom";
char mqtt_user[20] = MQTT_USER; // Якщо потрібена автентифікація
char mqtt_pass[20] = MQTT_PASSWORD;
int  sleep_interval = 60;
int  contrast = 1;
bool send_to_ha = true;


// Змінні у пам'яті RTC
RTC_DATA_ATTR float savedTemp = 0;
RTC_DATA_ATTR float savedHum = 0;
RTC_DATA_ATTR float savedBat = 0; // Зберігаємо вольтаж
RTC_DATA_ATTR int bootCount = 0;

void loadSettings() {
  preferences.begin("config", true); // true = read only
  
  // Якщо ключів немає - беруться значення за замовчуванням
  String w_ssid = preferences.getString("wifi_ssid", WIFI_SSID);
  w_ssid.toCharArray(wifi_ssid, 32);

  String w_pass = preferences.getString("wifi_pass", WIFI_PASSWORD);
  w_pass.toCharArray(wifi_pass, 20);

  String s_serv = preferences.getString("mqtt_server", MQTT_SERVER);
  s_serv.toCharArray(mqtt_server, 40);
  
  String s_port = preferences.getString("mqtt_port", "1883");
  s_port.toCharArray(mqtt_port, 6);
  
  String s_user = preferences.getString("mqtt_user", MQTT_USER);
  s_user.toCharArray(mqtt_user, 20);

  String s_pass = preferences.getString("mqtt_pass", MQTT_PASSWORD);
  s_pass.toCharArray(mqtt_pass, 20);

  String s_topic = preferences.getString("mqtt_topic", "home/livingroom");
  s_topic.toCharArray(mqtt_topic, 40);

  sleep_interval = preferences.getInt("interval", 60);
  contrast = preferences.getInt("contrast", 1);
  send_to_ha = preferences.getBool("send_ha", true);
  
  preferences.end();
}

void saveConfigCallback() {
  // Ця функція викликається, коли WiFiManager зберіг нові дані
}

void startConfigMode() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("CONFIG MODE");
  display.println("Connect to WiFi:");
  display.println("ESP32-Setup");
  display.println("IP: 192.168.4.1");
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYON);

  WiFiManager wm;
  
  // Додаємо власні поля на сторінку налаштувань
  WiFiManagerParameter custom_wifi_ssid("wifi_ssid", "WiFi SSID", wifi_ssid, 32);
  WiFiManagerParameter custom_wifi_pass("wifi_pass", "WiFi Pass", wifi_pass, 20);
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqtt_user, 20);
  WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Pass", mqtt_pass, 20);
  WiFiManagerParameter custom_mqtt_topic("topic", "Topic", mqtt_topic, 40);
  
  // Для чисел треба конвертація
  char c_interval[5]; itoa(sleep_interval, c_interval, 10);
  WiFiManagerParameter custom_interval("interval", "Sleep (sec)", c_interval, 60);

  char c_contrast[4]; itoa(contrast, c_contrast, 10);
  WiFiManagerParameter custom_contrast("contrast", "Contrast (0-255)", c_contrast, 4);

  // Чекбокс для відправки (трохи милиця в WiFiManager, але працює через поле)
  char c_send[2]; strcpy(c_send, send_to_ha ? "1" : "0");
  WiFiManagerParameter custom_send("send", "Send to HA (1/0)", c_send, 2);

  wm.addParameter(&custom_wifi_ssid);
  wm.addParameter(&custom_wifi_pass);
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.addParameter(&custom_mqtt_topic);
  wm.addParameter(&custom_interval);
  wm.addParameter(&custom_contrast);
  wm.addParameter(&custom_send);

  wm.setSaveConfigCallback(saveConfigCallback);

  // Створюємо точку доступу, якщо не підключились
  if (!wm.autoConnect("ESP32-Setup")) {
    delay(3000);
    ESP.restart();
  }

  // Якщо ми тут - значить користувач зберіг налаштування
  preferences.begin("config", false); // false = read/write
  
  preferences.putString("wifi_ssid", custom_wifi_ssid.getValue());
  preferences.putString("wifi_pass", custom_wifi_pass.getValue());
  preferences.putString("mqtt_server", custom_mqtt_server.getValue());
  preferences.putString("mqtt_port", custom_mqtt_port.getValue());
  preferences.putString("mqtt_user", custom_mqtt_user.getValue());
  preferences.putString("mqtt_pass", custom_mqtt_pass.getValue());
  preferences.putString("mqtt_topic", custom_mqtt_topic.getValue());
  
  preferences.putInt("interval", atoi(custom_interval.getValue()));
  preferences.putInt("contrast", atoi(custom_contrast.getValue()));
  preferences.putBool("send_ha", atoi(custom_send.getValue()) == 1);
  
  preferences.end();

  display.clearDisplay();
  display.println("Saved! Restarting...");
  display.display();
  delay(2000);
  ESP.restart();
}

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
  WiFi.begin(wifi_ssid, wifi_pass);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500); attempts++; }

  if (WiFi.status() == WL_CONNECTED) {
    client.setServer(mqtt_server, atoi(mqtt_port));
    client.setCallback(callback); // Встановлюємо функцію слухача

    if (client.connect("ESP32_Sensor", mqtt_user, mqtt_pass)) {
      
      // 1. Підписуємось на топік керування
      String topic_sub = String(mqtt_topic) + "/display/set";
      client.subscribe(topic_sub.c_str());
      
      // 2. Даємо час MQTT брокеру надіслати нам "утримане" повідомлення
      // Це критично важливо! Без loop() і затримки ми не встигнемо отримати команду.
      for(int i=0; i<10; i++) {
        client.loop(); 
        delay(50);
      }

      // 3. Відправляємо дані сенсорів
      String json = "{";
      json += "\"temperature\":" + String(savedTemp, 1) + ",";
      json += "\"humidity\":" + String(savedHum, 1) + ",";
      json += "\"voltage\":" + String(savedBat, 2) + ",";
      // Додамо статус дисплея, щоб HA знав поточний стан
      json += "\"display_status\":\"" + String(isDisplayEnabled ? "ON" : "OFF") + "\",";
      json += "\"boot_count\":\"" + String(bootCount) + "\"";
      json += "}";
      String topic_pub = String(mqtt_topic) + "/sensor";
      client.publish(topic_pub.c_str(), json.c_str(), true);
      delay(100); // Час на відправку
    } else {
      // Помилка підключення до MQTT
    }
  } else {
    // Помилка підключення до WiFi
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
  // setCpuFrequencyMhz(80);
  
  // Налаштування ADC (АЦП)
  analogReadResolution(12); // 12 біт (значення від 0 до 4095)

  pinMode(BTN_DISP_PIN, INPUT_PULLUP);
  pinMode(BTN_CONFIG_PIN, INPUT_PULLUP);
  
  Wire.begin(I2C_SDA, I2C_SCL);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    return; 
  }

  // 1. ПЕРЕВІРКА ПРИЧИНИ ПРОБУДЖЕННЯ
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // Якщо прокинулись від кнопки Дисплея (Ext0 або Ext1)
  // Або перевіряємо стан кнопки просто зараз
  if (digitalRead(BTN_DISP_PIN) == LOW) {
    isDisplayEnabled = !isDisplayEnabled; // Інвертуємо стан
  }

  // Якщо прокинулись і натиснута кнопка КОНФІГУРАЦІЇ
  if (digitalRead(BTN_CONFIG_PIN) == LOW) {
    startConfigMode(); // Заходимо в режим налаштування (точка доступу)
  }

  // 2. ЗАВАНТАЖЕННЯ НАЛАШТУВАНЬ
  loadSettings();

  bootCount++;
  // Встановлюємо контрастність
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(contrast);

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
    drawScreen();
    display.ssd1306_command(SSD1306_DISPLAYON);
  } else {
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
  }

  // 2. Підключення до мережі та перевірка нових команд
  if (send_to_ha) {
    connectAndSync();
  }

  // 3. Фінальна перевірка перед сном
  if (!isDisplayEnabled) {
  //    // Переконуємось, що екран вимкнено перед сном
     display.clearDisplay();
     display.display();
     display.ssd1306_command(SSD1306_DISPLAYOFF); 
  } else {
  //    // Якщо увімкнено - оновлюємо дані (показуємо, що відправлено)
     display.ssd1306_command(SSD1306_DISPLAYON);
  //    // Можна домалювати галочку "Sent" тут
  }

  // drawScreen();

  esp_sleep_enable_timer_wakeup(sleep_interval * uS_TO_S_FACTOR);

  // Прокидатись, якщо натиснуто кнопки (рівень LOW)
  // Для ESP32-C3 краще використовувати gpio_wakeup або ext1
  // Тут простий варіант для C3, що дозволяє будь-якому GPIO будити
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BTN_DISP_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BTN_CONFIG_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);

  esp_deep_sleep_start();
}

void loop() {
  // Не використовується в Deep Sleep
}