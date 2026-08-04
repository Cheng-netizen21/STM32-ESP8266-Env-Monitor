#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>   // ====== 新增：OTA支持 ======

#define WIFI_SSID     "cheng"
#define WIFI_PASSWORD "66666666"
#define ONENET_BROKER "mqtts.heclouds.com"
#define ONENET_PORT   1883
#define PRODUCT_ID    "7S7vzBikJk"
#define DEVICE_NAME   "wenshiduguangzhao"
#define PASSWORD_TOKEN "version=2018-10-31&res=products%2F7S7vzBikJk%2Fdevices%2Fwenshiduguangzhao&et=4070880000&method=md5&sign=95CBmzAyNRsaVsl9iCbuXQ%3D%3D"

#define TOPIC_PUB "$sys/" PRODUCT_ID "/" DEVICE_NAME "/thing/property/post"
#define TOPIC_SUB "$sys/" PRODUCT_ID "/" DEVICE_NAME "/thing/property/set"
#define TOPIC_EVENT "$sys/" PRODUCT_ID "/" DEVICE_NAME "/thing/event/post"

WiFiClient espClient;
PubSubClient mqttClient(espClient);
String inputString = "";
bool stringComplete = false;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  Serial.println("STA:WIFI_OK");
  Serial.println("STA:WIFI_OK (OTA test)");
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    if (mqttClient.connect(DEVICE_NAME, PRODUCT_ID, PASSWORD_TOKEN)) {
      Serial.println("STA:MQTT_OK");
    } else {
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  
  if (msg.indexOf("\"led_switch\":1") > 0) {
    Serial.println("CMD:LED_ON");
  } else if (msg.indexOf("\"led_switch\":0") > 0) {
    Serial.println("CMD:LED_OFF");
  } else if (msg.indexOf("\"buzzer_switch\":1") > 0) {
    Serial.println("CMD:BUZZER_ON");
  } else if (msg.indexOf("\"buzzer_switch\":0") > 0) {
    Serial.println("CMD:BUZZER_OFF");
  }
}

void setup() {
  Serial.begin(115200);
  connectWiFi();

  // ====== OTA 配置（新增） ======
  ArduinoOTA.setHostname("ESP8266_Env");   // 设备名称，在 Arduino IDE 端口里显示
  ArduinoOTA.setPassword("123456");     // 可选：设置密码，防止别人乱刷
  ArduinoOTA.begin();
  Serial.println("OTA ready");

  mqttClient.setServer(ONENET_BROKER, ONENET_PORT);
  connectMQTT();
  mqttClient.setCallback(callback);
  mqttClient.subscribe(TOPIC_SUB);
}

void loop() {
  // ====== 处理 OTA 请求（新增，必须放在最前面） ======
  ArduinoOTA.handle();

  mqttClient.loop();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("STA:WIFI_ERR");
    connectWiFi();
    connectMQTT();
    mqttClient.subscribe(TOPIC_SUB);
  }

  if (!mqttClient.connected()) {
    connectMQTT();
    mqttClient.subscribe(TOPIC_SUB);
  }

  // 读取 STM32 发来的数据
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      stringComplete = true;
    } else if (c != '\r') {
      inputString += c;
    }
  }

  if (stringComplete) {
    // ====== 检测告警指令（优先级最高） ======
    if (inputString.indexOf("ALERT") >= 0) {
      // 上报告警事件到云端
      String eventPayload = "{\"event\":\"ALERT\",\"timestamp\":\"" + String(millis()) + "\"}";
      if (mqttClient.publish(TOPIC_EVENT, eventPayload.c_str())) {
        Serial.println("ALERT sent to cloud");
      } else {
        Serial.println("ALERT send FAIL");
      }
      inputString = "";
      stringComplete = false;
      return;  // 处理完告警，跳过传感器解析
    }

    // ====== 解析传感器数据（原有逻辑） ======
    float temp = 0, humi = 0;
    int light_raw = 0;
    
    int parsed = sscanf(inputString.c_str(), "T:%f,H:%f,L:%d", &temp, &humi, &light_raw);
    
    if (parsed == 3) {
      int light_level = 0;
      if (light_raw > 3000) light_level = 0;
      else if (light_raw > 2000) light_level = 1;
      else if (light_raw > 1000) light_level = 2;
      else if (light_raw > 500)  light_level = 3;
      else light_level = 4;

      int smoke_level = 0;

      String payload = "{";
      payload += "\"id\":\"123\",";
      payload += "\"version\":\"1.0\",";
      payload += "\"params\":{";
      payload += "\"temperature\":{\"value\":" + String(temp) + "},";
      payload += "\"humidity\":{\"value\":" + String(humi) + "},";
      payload += "\"light_level\":{\"value\":" + String(light_level) + "},";
      payload += "\"smoke_level\":{\"value\":" + String(smoke_level) + "}";
      payload += "}}";

      if (mqttClient.publish(TOPIC_PUB, payload.c_str())) {
        // 发布成功
      } else {
        Serial.println("PUB_FAIL");
      }
    } else {
      // 解析失败，但如果是调试信息则忽略（不打印以免刷屏）
      if (inputString.indexOf("Received:") < 0 && inputString.indexOf("MQTT") < 0) {
        Serial.print("PARSE_ERR:");
        Serial.println(inputString);
      }
    }

    inputString = "";
    stringComplete = false;
  }
}