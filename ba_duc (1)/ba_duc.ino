#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// WIFI
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// MQTT
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

const char* topicData = "aquarium/data";
const char* topicPump = "aquarium/pump/set";
const char* topicValve = "aquarium/valve/set";
const char* topicMode = "aquarium/mode/set";

const char* topicPumpStatus = "aquarium/pump/status";
const char* topicValveStatus = "aquarium/valve/status";

WiFiClient espClient;
PubSubClient mqtt(espClient);

// GPIO
#define TRIG_PIN 5
#define ECHO_PIN 18
#define TEMP_PIN 4
#define PUMP_PIN 26
#define VALVE_PIN 27

#define TANK_HEIGHT 250.0
#define PWM_FREQ 5000
#define PWM_RESOLUTION 8

OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// DATA
struct SensorData {
  float distance;
  float waterLevel;
  float temperature;
};

// Dung kieu du lieu don gian de truyen qua Queue
struct ControlCommand {
  char device[8];
  char value[8];
};

struct ActuatorStatus {
  bool pump;
  bool valve;
};

QueueHandle_t dataQueue;
QueueHandle_t dataAlert;
QueueHandle_t commandQueue;
QueueHandle_t statusQueue;

bool autoMode = true;
bool pumpOn = false;
bool valveOpen = false;

enum WaterZone {
  ZONE_NORMAL,
  ZONE_PUMP_50,
  ZONE_PUMP_100,
  ZONE_DRAIN
};

WaterZone currentZone = ZONE_NORMAL;

unsigned long actionStart = 0;
unsigned long actionDuration = 0;
bool actionRunning = false;

// CAP NHAT TRANG THAI THIET BI
void updateStatus(bool pump, bool valve) {
  pumpOn = pump;
  valveOpen = valve;

  ActuatorStatus status;
  status.pump = pump;
  status.valve = valve;

  if (statusQueue != NULL) {
    xQueueOverwrite(statusQueue, &status);
  }
}

// DIEU KHIEN BOM
void controlPump(int power) {
  int pwmValue = map(power, 0, 100, 0, 255);
  ledcWrite(PUMP_PIN, pwmValue);
  updateStatus(power > 0, false);
}

// DIEU KHIEN VAN
void controlValve(bool open) {
  digitalWrite(VALVE_PIN, open ? HIGH : LOW);
  updateStatus(false, open);
}

// TAT TAT CA
void stopDevices() {
  ledcWrite(PUMP_PIN, 0);
  digitalWrite(VALVE_PIN, LOW);
  updateStatus(false, false);
}

// DOC KHOANG CACH
float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) return -1;

  return duration * 0.0343 / 2.0;
}

// KET NOI WIFI
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("Dang ket noi WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }

  Serial.println("\nWiFi da ket noi!");
}

// MQTT CALLBACK
void mqttCallback(char* topic, byte* payload,
                  unsigned int length) {
  ControlCommand cmd = {};

  String message = "";

  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (strcmp(topic, topicPump) == 0) {
    strcpy(cmd.device, "pump");
  }
  else if (strcmp(topic, topicValve) == 0) {
    strcpy(cmd.device, "valve");
  }
  else if (strcmp(topic, topicMode) == 0) {
    strcpy(cmd.device, "mode");
  }
  else {
    return;
  }

  message.toCharArray(cmd.value, sizeof(cmd.value));
  xQueueSend(commandQueue, &cmd, 0);
}

// KET NOI MQTT
void reconnectMQTT() {
  while (!mqtt.connected()) {
    Serial.println("Dang ket noi MQTT...");

    String clientId = "ESP32Aquarium-";
    clientId += String(random(0xffff), HEX);

    if (mqtt.connect(clientId.c_str())) {
      Serial.println("MQTT da ket noi!");

      mqtt.subscribe(topicPump);
      mqtt.subscribe(topicValve);
      mqtt.subscribe(topicMode);

      // Gui trang thai hien tai len Web
      mqtt.publish(topicPumpStatus,
                   pumpOn ? "ON" : "OFF", true);

      mqtt.publish(topicValveStatus,
                   valveOpen ? "ON" : "OFF", true);
    }
    else {
      Serial.print("Loi MQTT: ");
      Serial.println(mqtt.state());
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
}

// TASK 1: DOC CAM BIEN - CORE 1
void TaskReadData(void* pvParameters) {
  SensorData data;

  for (;;) {
    float distance = readDistance();

    sensors.requestTemperatures();
    float temperature = sensors.getTempCByIndex(0);

    if (distance > 0 && distance <= TANK_HEIGHT) {
      data.distance = distance;
      data.waterLevel = (TANK_HEIGHT - distance) / 100.0;
      data.temperature = temperature;

      Serial.println("===== DU LIEU BE NUOC =====");
      Serial.print("Khoang cach: ");
      Serial.print(data.distance);
      Serial.println(" cm");

      Serial.print("Muc nuoc: ");
      Serial.print(data.waterLevel);
      Serial.println(" m");

      Serial.print("Nhiet do: ");
      Serial.print(data.temperature);
      Serial.println(" C");

      xQueueSend(dataQueue, &data, 0);
      xQueueOverwrite(dataAlert, &data.waterLevel);
    }
    else {
      Serial.println("Loi cam bien HC-SR04!");
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// TASK 2: TRUYEN MQTT - CORE 0
void TaskTransmitData(void* pvParameters) {
  SensorData data;
  ActuatorStatus status;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    if (!mqtt.connected()) {
      reconnectMQTT();
    }

    mqtt.loop();

    // Gui du lieu cam bien
    if (xQueueReceive(dataQueue, &data,
                      pdMS_TO_TICKS(20)) == pdTRUE) {

      String payload = "{";
      payload += "\"distance\":" + String(data.distance, 2) + ",";
      payload += "\"waterLevel\":" + String(data.waterLevel, 2) + ",";
      payload += "\"temperature\":" + String(data.temperature, 2);
      payload += "}";

      mqtt.publish(topicData, payload.c_str());

      Serial.println("Da gui du lieu:");
      Serial.println(payload);
    }

    // Gui trang thai bom va van len Web
    if (xQueueReceive(statusQueue, &status, 0) == pdTRUE) {
      mqtt.publish(topicPumpStatus,
                   status.pump ? "ON" : "OFF", true);

      mqtt.publish(topicValveStatus,
                   status.valve ? "ON" : "OFF", true);

      Serial.print("May bom: ");
      Serial.println(status.pump ? "ON" : "OFF");

      Serial.print("Van thoat: ");
      Serial.println(status.valve ? "ON" : "OFF");
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// TASK 3: DIEU KHIEN TU DONG - CORE 1
void TaskAlert(void* pvParameters) {
  float waterLevel;
  ControlCommand cmd;

  for (;;) {
    // NHAN LENH TU WEB
    if (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {

      if (strcmp(cmd.device, "mode") == 0) {
        autoMode = (strcmp(cmd.value, "AUTO") == 0);

        stopDevices();
        actionRunning = false;
        currentZone = ZONE_NORMAL;

        Serial.println(autoMode ? "CHE DO AUTO" : "CHE DO MANUAL");
      }

      else if (!autoMode && strcmp(cmd.device, "pump") == 0) {
        if (strcmp(cmd.value, "ON") == 0) {
          controlPump(100);
          Serial.println("MANUAL: BAT MAY BOM");
        }
        else {
          ledcWrite(PUMP_PIN, 0);
          updateStatus(false, valveOpen);
          Serial.println("MANUAL: TAT MAY BOM");
        }
      }

      else if (!autoMode && strcmp(cmd.device, "valve") == 0) {
        if (strcmp(cmd.value, "ON") == 0) {
          controlValve(true);
          Serial.println("MANUAL: MO VAN THOAT");
        }
        else {
          digitalWrite(VALVE_PIN, LOW);
          updateStatus(pumpOn, false);
          Serial.println("MANUAL: DONG VAN THOAT");
        }
      }
    }

    // CHE DO TU DONG
    if (autoMode) {
      if (actionRunning &&
          millis() - actionStart >= actionDuration) {

        stopDevices();
        actionRunning = false;

        Serial.println("Da ket thuc chu ky dieu khien.");
      }

      if (xQueueReceive(dataAlert, &waterLevel, 0) == pdTRUE) {
        WaterZone newZone = ZONE_NORMAL;

        if (waterLevel >= 1.0 && waterLevel < 1.5) {
          newZone = ZONE_PUMP_50;
        }
        else if (waterLevel >= 1.5 && waterLevel <= 2.0) {
          newZone = ZONE_PUMP_100;
        }
        else if (waterLevel < 0.5) {
          newZone = ZONE_DRAIN;
        }

        if (newZone != currentZone) {
          currentZone = newZone;

          stopDevices();
          actionRunning = false;

          if (newZone == ZONE_PUMP_50) {
            controlPump(50);
            actionDuration = 10000;

            Serial.println("Muc nuoc 1m - 1.5m: Bom 50% trong 10 giay.");
          }

          else if (newZone == ZONE_PUMP_100) {
            controlPump(100);
            actionDuration = 20000;

            Serial.println("Muc nuoc 1.5m - 2m: Bom 100% trong 20 giay.");
          }

          else if (newZone == ZONE_DRAIN) {
            controlValve(true);
            actionDuration = 10000;

            Serial.println("Muc nuoc duoi 0.5m: Mo van 10 giay.");
          }

          if (newZone != ZONE_NORMAL) {
            actionStart = millis();
            actionRunning = true;
          }
          else {
            Serial.println("Muc nuoc binh thuong.");
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// SETUP
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(VALVE_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);

  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(VALVE_PIN, LOW);

  ledcAttach(PUMP_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(PUMP_PIN, 0);

  sensors.begin();

  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);

  dataQueue = xQueueCreate(5, sizeof(SensorData));
  dataAlert = xQueueCreate(1, sizeof(float));
  commandQueue = xQueueCreate(5, sizeof(ControlCommand));
  statusQueue = xQueueCreate(1, sizeof(ActuatorStatus));

  if (dataQueue == NULL || dataAlert == NULL ||
      commandQueue == NULL || statusQueue == NULL) {
    Serial.println("Loi tao Queue!");
    while (1) delay(1000);
  }

  // Trang thai ban dau
  ActuatorStatus initialStatus = {false, false};
  xQueueOverwrite(statusQueue, &initialStatus);

  xTaskCreatePinnedToCore(
    TaskReadData, "TaskReadData", 4096, NULL, 2, NULL, 1
  );

  xTaskCreatePinnedToCore(
    TaskTransmitData, "TaskTransmitData", 8192, NULL, 1, NULL, 0
  );

  xTaskCreatePinnedToCore(
    TaskAlert, "TaskAlert", 4096, NULL, 2, NULL, 1
  );

  Serial.println("HE THONG ESP32 AQUARIUM STARTED!");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}