#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>

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
#define TOPIC_OTA   "$sys/" PRODUCT_ID "/" DEVICE_NAME "/thing/ota"

#define OTA_SERVER_BASE "http://10.85.156.229:8080"   // 修改为您的服务器地址

WiFiClient espClient;
PubSubClient mqttClient(espClient);

String inputString = "";
bool stringComplete = false;
bool otaInProgress = false;
bool otaTransmitting = false;

// 调试开关：仅在非 OTA 传输时允许串口输出
#define DEBUG_ENABLED()  (!otaTransmitting)

// 条件调试宏
#define DEBUG_PRINT(x)    do { if (DEBUG_ENABLED()) Serial.print(x); } while(0)
#define DEBUG_PRINTLN(x)  do { if (DEBUG_ENABLED()) Serial.println(x); } while(0)

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  DEBUG_PRINTLN("STA:WIFI_OK");
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    if (mqttClient.connect(DEVICE_NAME, PRODUCT_ID, PASSWORD_TOKEN)) {
      DEBUG_PRINTLN("STA:MQTT_OK");
      mqttClient.subscribe(TOPIC_SUB);
      mqttClient.subscribe(TOPIC_OTA);
    } else {
      delay(5000);
    }
  }
}

bool downloadAndSendFirmware(String url) {
    if (otaInProgress) return false;
    otaInProgress = true;
    otaTransmitting = true;   // 进入传输模式，禁用所有调试输出

    WiFiClient client;
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(10000);
    int httpCode = http.GET();
    if (httpCode != 200) {
        http.end();
        otaInProgress = false;
        otaTransmitting = false;
        return false;
    }
    int contentLength = http.getSize();
    if (contentLength <= 0) {
        http.end();
        otaInProgress = false;
        otaTransmitting = false;
        return false;
    }

    // 清空串口缓冲区（无打印）
    while (Serial.available()) Serial.read();
    Serial.flush();
    delay(100);
    while (Serial.available()) Serial.read();
    Serial.flush();
    delay(20);

    // 发送 4 字节长度（大端）
    uint8_t lenBuf[4];
    lenBuf[0] = (contentLength >> 24) & 0xFF;
    lenBuf[1] = (contentLength >> 16) & 0xFF;
    lenBuf[2] = (contentLength >> 8) & 0xFF;
    lenBuf[3] = contentLength & 0xFF;
    Serial.write(lenBuf, 4);
    Serial.flush();
    delay(20);

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buf[1024];
    uint32_t total = 0;
    bool ack_ok = true;
    unsigned long lastDataTime = millis();

    // 主循环：逐块读取，若 readBytes 返回 0 则等待数据直到超时（5秒）或读满
    while (total < contentLength) {
        mqttClient.loop();
        yield();

        uint32_t remaining = contentLength - total;
        uint16_t readLen = (remaining < 1024) ? (uint16_t)remaining : 1024;
        int len = stream->readBytes(buf, readLen);
        if (len > 0) {
            Serial.write(buf, len);
            total += len;
            lastDataTime = millis();
            Serial.flush();

            // 等待 ACK (0x06)
            ack_ok = false;
            unsigned long wait_start = millis();
            while (millis() - wait_start < 3000) {
                if (Serial.available()) {
                    uint8_t ack = Serial.read();
                    if (ack == 0x06) {
                        ack_ok = true;
                        break;
                    }
                }
                delay(5);
            }
            if (!ack_ok) break;
        } else {
            // 没有读到数据，检查是否超时或流结束
            if (millis() - lastDataTime > 5000) {
                break; // 超时退出
            }
            delay(10);
        }
    }

    // 如果还有剩余数据且未超时，尝试逐字节补读
    if (total < contentLength && millis() - lastDataTime < 5000) {
        while (total < contentLength && (stream->available() || millis() - lastDataTime < 2000)) {
            if (stream->available()) {
                uint8_t byte = stream->read();
                Serial.write(byte);
                total++;
                lastDataTime = millis();
                // 每 1024 字节发送 ACK 逻辑（此处可简化，但保留）
                if (total % 1024 == 0) {
                    Serial.flush();
                    ack_ok = false;
                    unsigned long wait_start = millis();
                    while (millis() - wait_start < 3000) {
                        if (Serial.available()) {
                            uint8_t ack = Serial.read();
                            if (ack == 0x06) {
                                ack_ok = true;
                                break;
                            }
                        }
                        delay(5);
                    }
                    if (!ack_ok) break;
                }
            } else {
                delay(1);
            }
        }
        // 最后再刷新一次，确认完整发送
        if (total == contentLength) {
            Serial.flush();
            ack_ok = false;
            unsigned long wait_start = millis();
            while (millis() - wait_start < 3000) {
                if (Serial.available()) {
                    uint8_t ack = Serial.read();
                    if (ack == 0x06) {
                        ack_ok = true;
                        break;
                    }
                }
                delay(5);
            }
        }
    }

    http.end();
    otaInProgress = false;
    otaTransmitting = false;   // 恢复调试输出

    if (total == contentLength && ack_ok) {
        // 成功后输出一次简短提示（已恢复调试）
        Serial.println("OTA OK");
        return true;
    } else {
        Serial.println("OTA FAIL");
        return false;
    }
}

void checkForUpdate() {
    if (otaInProgress) return;
    WiFiClient client;
    HTTPClient http;
    String url = String(OTA_SERVER_BASE) + "/version.txt";
    http.begin(client, url);
    http.setTimeout(10000);
    int httpCode = http.GET();
    if (httpCode == 200) {
        String newVersion = http.getString();
        newVersion.trim();
        downloadAndSendFirmware(String(OTA_SERVER_BASE) + "/MyProject.bin");
    }
    http.end();
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  if (msg.indexOf("\"led_switch\":1") > 0) {
    if (DEBUG_ENABLED()) Serial.println("CMD:LED_ON");
  } else if (msg.indexOf("\"led_switch\":0") > 0) {
    if (DEBUG_ENABLED()) Serial.println("CMD:LED_OFF");
  } else if (msg.indexOf("\"buzzer_switch\":1") > 0) {
    if (DEBUG_ENABLED()) Serial.println("CMD:BUZZER_ON");
  } else if (msg.indexOf("\"buzzer_switch\":0") > 0) {
    if (DEBUG_ENABLED()) Serial.println("CMD:BUZZER_OFF");
  }

  if (msg.indexOf("\"cmd\":\"ota\"") > 0) {
    int urlStart = msg.indexOf("url\":\"") + 6;
    if (urlStart > 0) {
      int urlEnd = msg.indexOf("\"", urlStart);
      if (urlEnd > 0) {
        String url = msg.substring(urlStart, urlEnd);
        downloadAndSendFirmware(url);
      }
    }
  }
}

void setup() {
  Serial.begin(57600);
  connectWiFi();
  ArduinoOTA.setHostname("ESP8266_Env");
  ArduinoOTA.setPassword("123456");
  ArduinoOTA.begin();
  mqttClient.setServer(ONENET_BROKER, ONENET_PORT);
  connectMQTT();
  mqttClient.setCallback(callback);
}

void loop() {
  ArduinoOTA.handle();
  mqttClient.loop();

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    connectMQTT();
  }
  if (!mqttClient.connected()) {
    connectMQTT();
  }

  if (Serial.available()) {
    int req = Serial.read();
    if (req == 0x55) {
      while (Serial.available()) Serial.read();
      Serial.write(0xAA);
      Serial.flush();
      delay(10);
      if (!otaInProgress && WiFi.status() == WL_CONNECTED) {
        checkForUpdate();
      }
    } else if (req != -1) {
      if (req == '\n') {
        stringComplete = true;
      } else if (req != '\r') {
        inputString += (char)req;
      }
    }
  }

  if (stringComplete) {
    if (inputString.indexOf("ALERT") >= 0) {
      String eventPayload = "{\"event\":\"ALERT\",\"timestamp\":\"" + String(millis()) + "\"}";
      mqttClient.publish(TOPIC_EVENT, eventPayload.c_str());
      inputString = "";
      stringComplete = false;
      return;
    }

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
      mqttClient.publish(TOPIC_PUB, payload.c_str());
    }
    inputString = "";
    stringComplete = false;
  }
}