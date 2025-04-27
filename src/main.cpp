#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <ESP32Servo.h>
#include <ezButton.h>
#include "functions.h"
#include "webServerStyle.h"
#include <SPIFFS.h>
#include <Preferences.h> // Replaces EEPROM for ESP32
#include "mqttConfig.h"

int DEBOUNCE_TIME_HALL = 0; // if you notice bouncing on wheel pos reads, increase this slowly. too high of a value will ignore rotations if your cat is sanic speed.
bool DEBUG_DIST = true;


// Global state machine
enum class NetworkState
{
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
  AP_MODE,
  TRIAL_MODE
};
volatile NetworkState networkState = NetworkState::DISCONNECTED;

// Configuration structure stored in Preferences (replaces EEPROM)
struct WiFiConfig
{
  char ssid[32];
  char password[64];
};

WiFiConfig config;
MQTTConfig mqttConf;

WiFiClient espClient;
PubSubClient mqttClient(espClient);;

// FreeRTOS handles
TaskHandle_t wifiTaskHandle;
TaskHandle_t webTaskHandle;
TaskHandle_t mqttTaskHandle;
QueueHandle_t wifiQueue;
QueueHandle_t mqttQueue;

Preferences preferences; // ESP32's non-volatile storage

// Statistics (these are populated from non-volatile storage below)
uint32_t totalDistance = 0;
uint32_t totalTreatsDispensed = 0;

volatile uint32_t hallEffectCount = 0;
int hallEffectRunDistanceMultiplier = 22; // find the circumferance of your wheel in cm, then divide by the number of magnets you have installed.
uint32_t distanceThreshold = 100 * 100;   // 100 meters - we also populate this below, but just in case that doesn't work, we want to make sure its not zero cause it would potentially empty the hopper out.

// io flags
bool forceDispense = false;
bool outOfTreats = false;
bool outOfTreats_hopper = false;
volatile bool dispensingTreat = false;

// timer for empty hopper detection:
volatile bool ISR_GUARD = true; // only run ISR logic when our main code dictates. (because we turn the LED on and off only when needed, and the LED drives the ISR)
volatile unsigned long accumulatedDispensingTimeWithoutHopperTreat_ms = 0;

// pin deffinitions
//    inputs:
const int hallEffectSensorPin = 4;
const int hopperLightBreakSensorPin = 17;
const int dispenseLightBreakSensorPin = 16;
// const int resetErrorButtonPin = 18;
// const int resetWifiButtonPin = 19;
//    outputs:
const int hopperLightBreakSensorLEDPin = 18;
const int dispenseLightBreakSensorLEDPin = 19;
const int motorPin = 21;
const int errorLEDPin = 13;

ezButton hallEffect(hallEffectSensorPin);
Servo continuousServo;


// ISR handlers for light break sensors. We want to detect treats as fast as we can, so we use interrupts instead of checking in main runtime logic!
void IRAM_ATTR handleHopperPhotoDiodeISR()
{
  if(!ISR_GUARD)
  {
    accumulatedDispensingTimeWithoutHopperTreat_ms = 0;  // Reset when we see a treat
    outOfTreats_hopper = false;
  }
}

void IRAM_ATTR handleDispensePhotoDiodeISR()
{
  dispensingTreat = false;
}




// Memory check function for ESP32
int freeMemory()
{
  return ESP.getFreeHeap();
}

void setup()
{
  Serial.begin(115200);
  Serial.println("starting setup");


  // init physical stuff
  hallEffect.setDebounceTime(DEBOUNCE_TIME_HALL);
  continuousServo.setPeriodHertz(50); // Standard 50Hz servo
  continuousServo.attach(motorPin, 544, 2400);

  pinMode(dispenseLightBreakSensorLEDPin, OUTPUT);
  pinMode(hopperLightBreakSensorLEDPin, OUTPUT);
  pinMode(errorLEDPin, OUTPUT);
  //pinMode(resetWifiButtonPin, INPUT_PULLUP);
  //pinMode(resetErrorButtonPin, INPUT_PULLUP);
  pinMode(dispenseLightBreakSensorPin, INPUT_PULLUP);
  pinMode(hopperLightBreakSensorPin, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(dispenseLightBreakSensorPin), handleDispensePhotoDiodeISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(hopperLightBreakSensorPin), handleHopperPhotoDiodeISR, FALLING);



  // Initialize Preferences (replaces EEPROM)
  preferences.begin("conf", false);

  // First, see if we have done the initial settings write after a reset
  if (!preferences.getBool("initialSettings", false))
  {
    Serial.println("[main] - setting initial config");
    setInitialConfig();
  }
  else
  {
    size_t configSize = sizeof(WiFiConfig);
    if (preferences.getBytesLength("config") == configSize)
    {
      preferences.getBytes("config", &config, configSize);
    }
    
    distanceThreshold = preferences.getInt("dist");
    mqttConf.server = preferences.getString("mqttServer");
    mqttConf.username = preferences.getString("mqttUser");
    mqttConf.password = preferences.getString("mqttPass");
    mqttConf.topicPrefix = preferences.getString("mqttTopic");
    mqttConf.port = preferences.getInt("mqttPort");
    mqttConf.mqttEnabled = preferences.getBool("mqttEnable");
    preferences.end();


    distanceThreshold = preferences.getInt("dist", 100 * 100);
    totalDistance = preferences.getInt("totalDistance");
    totalTreatsDispensed = preferences.getInt("totalTreatsDispensed");
  }
  preferences.end();

  // Create FreeRTOS resources
  wifiQueue = xQueueCreate(1, sizeof(WiFiConfig));
  mqttQueue = xQueueCreate(1, sizeof(MQTTConfig));

  // Start tasks
  if (pdPASS != xTaskCreatePinnedToCore(wifiManagerTask, "WiFiManager", 4096, NULL, 1, &wifiTaskHandle, 1))
  {
    Serial.println("Failed to create WiFi task!");
    while (1)
      ;
  }

  if (pdPASS != xTaskCreatePinnedToCore(webServerTask, "WebServer", 4096, NULL, 1, &webTaskHandle, 1))
  {
    Serial.println("Failed to create webserver task!");
    while (1)
      ;
  }

  if (pdPASS != xTaskCreatePinnedToCore(mqttServerTask, "mqttServerTask", 8192, NULL, 1, &mqttTaskHandle, 1))
  {
    Serial.println("Failed to create webserver task!");
    while (1)
      ;
  }

  // if (pdPASS != xTaskCreatePinnedToCore(saveStatisticsTask, "saveStatistics", 1024, NULL, 1, NULL, 1))
  // {
  //   Serial.println("Failed to create statistics task!");
  //   while (1)
  //     ;
  // }

  //Run our main task on its own core to avoid timing issues with physical motion
  if (pdPASS != xTaskCreatePinnedToCore(mainTask, "led_blink", 2048, NULL, 1, NULL, 0))
  {
    Serial.println("Failed to create test led task!");
    while (1)
      ;
  }

  Serial.println("setup complete");
  // No need to call vTaskStartScheduler() - it's automatically called by ESP32 Arduino core

}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(50));
}

////////////////////////
///      TASKS      ///
//////////////////////

void mainTask(void *pvParameters)
{

  Serial.println("[main]: task starting...");

  continuousServo.writeMicroseconds(1500); // Write a stop command, since if the MCU resets during motor movement, we want to halt it!
  vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second before starting task loop to make sure everything is setup.

  while(1)
  {
    if(outOfTreats)
    {
      digitalWrite(errorLEDPin, HIGH);
    }
    else
    {
      digitalWrite(errorLEDPin, LOW);
    }


    hallEffect.loop();
    if(hallEffect.isPressed())
    {
      hallEffectCount++;
      totalDistance += hallEffectRunDistanceMultiplier;
      if(DEBUG_DIST)
      {
        Serial.println("distance:");
        Serial.print("\t"); Serial.println(hallEffectCount * hallEffectRunDistanceMultiplier);
        Serial.println("distance threshold:");
        Serial.print("\t"); Serial.println(distanceThreshold);
        Serial.println("");
      }
    }

    if ((!outOfTreats && hallEffectCount * hallEffectRunDistanceMultiplier >= distanceThreshold) || forceDispense) {
      hallEffectCount = 0;
      forceDispense = false;
      dispenseTreat();
    }
    vTaskDelay(5 / portTICK_PERIOD_MS); // Delay for 5ms
  }

}

void saveStatisticsTask(void *pvParameters)
{
  // This is for logging on device. in the future it would probably be good to see if theres a newer value we can read back off from mqtt, but eh - good enough for now.
  // because MCU's have limited write cycles to their memory, we want to limit how often we store our values. Assuming we write once an hour, and memory fails at 100k writes, this shouldn't be an issue for ~11.5 years.
  // We also only write if values change.

  uint32_t lastSavedTotalDistance = 0;
  uint32_t lastSavedTotalTreatsDispensed = 0;

  Serial.println("[stats saver]: task starting...");

  while (true)
  {
    if (totalDistance > lastSavedTotalDistance)
    {
      lastSavedTotalDistance = totalDistance;
      preferences.putInt("totalDistance", lastSavedTotalDistance);
    }
    if (totalTreatsDispensed > lastSavedTotalTreatsDispensed)
    {
      lastSavedTotalTreatsDispensed = totalTreatsDispensed;
      preferences.putInt("totalTreatsDispensed", lastSavedTotalTreatsDispensed);
    }
    vTaskDelay(pdMS_TO_TICKS(3600000)); // 1 hour
  }
}

void wifiManagerTask(void *pvParameters)
{
  Serial.println("[wifiManager]: task starting...");
  while (true)
  {
    if (strlen(config.ssid))
    {
      connectToWiFi();
      if (networkState != NetworkState::CONNECTED)
      {
        Serial.println("[wifiManager]: wifi not connected, starting config AP");
        startAPMode();
      }
    }
    else
    {
      Serial.println("[wifiManager]: wifi not configured, starting config AP");
      startAPMode();
    }

    // Maintain connection
    while (true)
    {
      uint8_t currentConnectionStatus = WiFi.status();
      if (networkState == NetworkState::CONNECTED && currentConnectionStatus != WL_CONNECTED)
      {
        Serial.println("Detected problem with wifi - reconnecting");
        networkState = NetworkState::DISCONNECTED;
      }

      WiFiConfig recv_msg;
      if (xQueueReceive(wifiQueue, &recv_msg, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        // Handle messages from web server
        Serial.println("[wifiManager]: new wifi credentials received, saving...");
        strncpy(config.ssid, recv_msg.ssid, sizeof(config.ssid));
        strncpy(config.password, recv_msg.password, sizeof(config.password));
        saveWifi();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_MODE_STA);
        networkState = NetworkState::DISCONNECTED;
      }

      // If the network state has been changed to DISCONNECTED, break out of our loop to reconnect
      if (networkState == NetworkState::DISCONNECTED)
      {
        Serial.println("WiFi set to disconnect");
        WiFi.disconnect(true);
        break;
      }

      vTaskDelay(pdMS_TO_TICKS(200));
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void webServerTask(void *pvParameters)
{
  Serial.println("[webServer]: task starting...");
  AsyncWebServer server(80);

  bool serverRunning = false;
  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Only start server if we're in the right state
    if ((networkState == NetworkState::AP_MODE || networkState == NetworkState::CONNECTED) && !serverRunning)
    {
      setupWebServerRoutes(server);
      serverRunning = true;
      server.begin();
      Serial.println("[webServer]: Server started");
    }
    else if (networkState == NetworkState::DISCONNECTED && serverRunning)
    {
      serverRunning = false;
      server.end();
      Serial.println("[webServer]: Server stopped");
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void mqttServerTask(void *parameter)
{
  uint32_t lastMqttPublishTime = 0;
  int mqttPublishInterval = (1000 * 60 * 5); // every 5 minutes

  mqttClient.setCallback(mqttCallback);

  while (1)
  {
    while (mqttConf.mqttEnabled && networkState == NetworkState::CONNECTED)
    {

      // Publish usage statistics via MQTT
      if (mqttClient.connected() && (millis() - lastMqttPublishTime >= mqttPublishInterval))
      {
        mqttPublishUsageStats();
        lastMqttPublishTime = millis();
      }

      // Reconnect to MQTT if disconnected
      if (!mqttClient.connected())
      {
        Serial.print("[mqtt] setting mqtt server host to: ");
        Serial.println(mqttConf.server);

        mqttClient.setServer(mqttConf.server.c_str(), mqttConf.port);
        mqttReconnect();
      }
      else
      {
        mqttClient.loop();
      }

    }
    Serial.println("[mqtt] - waiting to be enabled...");
    vTaskDelay(60000 / portTICK_PERIOD_MS); // Delay for 10 seconds
  }
}

////////////////////////
///   Meat Space    ///
//////////////////////
void dispenseTreat() {
  Serial.println("[main] - dispensing treat");

  digitalWrite(hopperLightBreakSensorLEDPin, HIGH);
  digitalWrite(dispenseLightBreakSensorLEDPin, HIGH);
  vTaskDelay(200 / portTICK_PERIOD_MS); // Delay for 200ms to make sure light break sensor is reading high

  ISR_GUARD = false;
  dispensingTreat = true;

  unsigned long looptime = millis();;
  unsigned long treatDispenseStartTime = millis();
  continuousServo.writeMicroseconds(1500 + 500);
  
  while (dispensingTreat == true) 
  {  
    vTaskDelay(1 / portTICK_PERIOD_MS); // Delay for 1ms, this is needed or the watchdog for the task will kill it!

    // yeah, this isn't the ideal way to do the timing, but it works. 
    //we want this so our hopper time carries over between dispense treat calls
    //  (ie, nothing detected for 4 of 5 seconds, treat leaves main body, dispenseTreat is called again, it should detect hopper empty after 1 more second.)
    accumulatedDispensingTimeWithoutHopperTreat_ms += millis() - looptime;
    looptime = millis();
    if (accumulatedDispensingTimeWithoutHopperTreat_ms  > 5000)
    {
      if(!outOfTreats_hopper)
      {
        Serial.println("[main] hopper out of treats!");
        outOfTreats_hopper = true;
      }
    }

    // If the threshold of 30 seconds for dispensing a treat is exceeded, set the error flag and give up.
    if (looptime - treatDispenseStartTime > 30000)
    {
      outOfTreats = true;
      dispensingTreat = false;
      Serial.println("[main] fully out of treats!");
      break;
    }
  }
  continuousServo.writeMicroseconds(1500);

  ISR_GUARD = true;
  vTaskDelay(200 / portTICK_PERIOD_MS); // Delay for 200ms to make sure ISR logic is guarded
  digitalWrite(hopperLightBreakSensorLEDPin, LOW);
  digitalWrite(dispenseLightBreakSensorLEDPin, LOW);

  if(!outOfTreats) // We just dispensed one
  {
    totalTreatsDispensed++;
  }

  return;
}



////////////////////////
///   MQTT Logic    ///
//////////////////////

void mqttReconnect()
{
  Serial.print("[mqtt] Attempting MQTT connection...");

  if (mqttClient.connect("Cat_wheel", mqttConf.username.c_str(), mqttConf.password.c_str()))
  {
    Serial.println("\tconnected");
  }
  else
  {
    Serial.print("\tfailed, rc=");
    Serial.println(mqttClient.state());
  }
}

void mqttPublishUsageStats()
{
  mqttClient.publish((mqttConf.topicPrefix + "/totalDistance").c_str(), String(totalDistance / 100).c_str());
  mqttClient.publish((mqttConf.topicPrefix + "/totalTreatsDispensed").c_str(), String(totalTreatsDispensed).c_str());
  mqttClient.publish((mqttConf.topicPrefix + "/isOutOfTreats").c_str(), String(outOfTreats ? "True" : "False").c_str());
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String message;
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  Serial.println("Got message: " + message);

  if (String(topic) == (mqttConf.topicPrefix + "/manualDispense") && message == "1")
  {
    forceDispense = true;
  }
}

////////////////////////
///   Networking    ///
//////////////////////

void connectToWiFi()
{
  networkState = NetworkState::CONNECTING;
  Serial.printf("\t[wifiManager]: Attempting to connect to ssid %s\n", config.ssid);

  vTaskDelay(pdMS_TO_TICKS(100));

  unsigned long start = millis();
  WiFi.begin(config.ssid, config.password);
  WiFi.setSleep(false);

  while (millis() - start < 15000)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      networkState = NetworkState::CONNECTED;
      Serial.println("\t[wifiManager]: Connected to existing network!");
      vTaskDelay(pdMS_TO_TICKS(3000)); // wait for dhcp and stuff
      Serial.printf("\t[wifiManager]: ip addr: %s\n", WiFi.localIP().toString().c_str());
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  networkState = NetworkState::DISCONNECTED;
  Serial.println("\t[wifiManager]: Connection failed");
}

void startAPMode()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("CAT_WHEEL_SETUP", "");

  networkState = NetworkState::AP_MODE;
  Serial.printf("\t[wifiManager]: AP Mode enabled\n\t\tSSID: CAT_WHEEL_SETUP\n\t\tip addr: %s\n", WiFi.softAPIP().toString().c_str());
}

////////////////////////
///     Config      ///
//////////////////////

void saveConfig()
{
  preferences.begin("conf", false);
  preferences.putInt("dist", distanceThreshold);

  preferences.putString("mqttServer", mqttConf.server);
  preferences.putString("mqttUser", mqttConf.username);
  preferences.putString("mqttPass", mqttConf.password);
  preferences.putString("mqttTopic", mqttConf.topicPrefix);
  preferences.putInt("mqttPort", mqttConf.port);
  preferences.putBool("mqttEnable", mqttConf.mqttEnabled);
  preferences.end();

  Serial.println("Configuration saved");

}

void clearConfig()
{
  preferences.begin("conf", false);
  preferences.remove("mqttconfig");
  preferences.end();
  setInitialConfig();
  Serial.println("Configuration cleared");
}

void saveWifi()
{
  preferences.begin("conf", false);
  preferences.putBytes("config", &config, sizeof(config));
  Serial.println("WiFi Configuration saved");
  preferences.end();
}

void clearWifi()
{
  preferences.begin("conf", false);
  preferences.remove("config");
  preferences.end();
  Serial.println("Configuration cleared");
}

void setInitialConfig()
{
  distanceThreshold = 100 * 100; // 100 meters
  preferences.putInt("dist", distanceThreshold);

  mqttConf.password ="xxxxx";
  mqttConf.port = 1883;
  mqttConf.server = "10.4.0.4";
  mqttConf.topicPrefix = "/iot/device/catwheel/";
  mqttConf.username = "cat_wheel";
  mqttConf.mqttEnabled = false;

  size_t configSize = sizeof(WiFiConfig);
  memset(&config, 0, configSize);
  saveConfig();
  saveWifi();
  preferences.putBool("initialSettings", true);
}

////////////////////////
///   Web Server    ///
//////////////////////

void setupWebServerRoutes(AsyncWebServer &server)
{
  // Handle AP configuration mode routes
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    int numNetworks = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < numNetworks; i++) {
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + WiFi.RSSI(i) + ",\"encryption\":\"";
        switch (WiFi.encryptionType(i)) {
            case WIFI_AUTH_OPEN: json += "Open"; break;
            case WIFI_AUTH_WEP: json += "WEP"; break;
            case WIFI_AUTH_WPA_PSK: json += "WPA"; break;
            case WIFI_AUTH_WPA2_PSK: json += "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: json += "WPA/WPA2"; break;
            case WIFI_AUTH_WPA2_ENTERPRISE: json += "802.11x"; break;
            case WIFI_AUTH_WPA3_PSK: json += "WPA3"; break;
            case WIFI_AUTH_WPA2_WPA3_PSK: json += "WPA2/WPA3"; break;
            default: json += "Unknown"; break;
        }
        json += "\"}";
        if (i < numNetworks - 1) json += ",";
    }
    json += "]";
    request->send(200, "application/json", json); });

  server.on("/connect", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("ssid") && request->hasParam("pass")) {
        String ssid = request->getParam("ssid")->value();
        String pass = request->getParam("pass")->value();

        // Send to WiFi task
        WiFiConfig send_msg;
        strncpy(send_msg.ssid, ssid.c_str(), sizeof(config.ssid));
        strncpy(send_msg.password, pass.c_str(), sizeof(config.password));
        xQueueSend(wifiQueue, &send_msg, portMAX_DELAY);

        String response = String(AP_CONFIG_PAGE_HEADER) + 
                        COMMON_HEADER +
                        R"(
                        <h1>WiFi Configuration</h1>
                        <div class="status-message status-info">
                            <p>Attempting to connect to the network. See you there!</p>
                        </div>
                        <meta http-equiv="refresh" content="3;url=/">
                        )" + 
                        COMMON_FOOTER;
        request->send(200, "text/html", response);
    } else {
        request->send(400, "text/plain", "Missing parameters");
    } });

  server.on("/trial_mode", HTTP_GET, [](AsyncWebServerRequest *request)
            {
      if (networkState == NetworkState::AP_MODE)
      {
        networkState = NetworkState::TRIAL_MODE;
        String response = String(MAIN_PAGE_HEADER) + 
        COMMON_HEADER + 
      R"(
        <h1>Trial mode activated!</h1>
        <div class="status-message status-success">
            <p>You can try out the web UI before connecting it to your network. MQTT will be disabled. To reconnect, clear wifi settings or reboot the microcontroller!</p>
            <p>This page will automatically refresh...</p>
        </div>
        <meta http-equiv="refresh" content="5;url=/">
        )" +
      COMMON_FOOTER;
      request->send(200, "text/html", response);
      }
      else
      {
        String response = String(AP_CONFIG_PAGE_HEADER) + 
                    COMMON_HEADER + 
                    R"(
                      <h1>Wait, why are you here?</h1>
                      <div class="status-message status-success">
                          <p>You already configured your network, so you can't enable trial mode.</p>
                          <p>This page will automatically refresh...</p>
                      </div>
                      <meta http-equiv="refresh" content="5;url=/">
                      )" + 
                    COMMON_FOOTER;
        request->send(200, "text/html", response);
      } });

  // Main mode routes
  server.on("/reset_wifi", HTTP_GET, [](AsyncWebServerRequest *request)
            {
        String response = String(MAIN_PAGE_HEADER) + 
                        COMMON_HEADER +
                        R"(
                        <h1>WiFi Configuration Cleared</h1>
                        <div class="status-message status-success">
                            <p>WiFi network has been cleared. Please look for the CAT_WHEEL_SETUP network to reconfigure WiFi!</p>
                            <p>The microcontroller will restart in a few seconds...</p>
                        </div>
                        <meta http-equiv="refresh" content="5;url=/">
                        )" + 
                        COMMON_FOOTER;
        request->send(200, "text/html", response); 
        request->onDisconnect([]() {
          clearWifi();
          vTaskDelay(1500 / portTICK_PERIOD_MS);
          ESP.restart();
          });
        });

  server.on("/reset_config", HTTP_GET, [](AsyncWebServerRequest *request)
        {
    String response = String(MAIN_PAGE_HEADER) + 
                    COMMON_HEADER +
                    R"(
                    <h1>Configuration Cleared</h1>
                    <div class="status-message status-success">
                        <p>Settings have been cleared.</p>
                    </div>
                    <meta http-equiv="refresh" content="2;url=/">
                    )" + 
                    COMMON_FOOTER;
    request->send(200, "text/html", response); 
    request->onDisconnect([]() {
      clearConfig();
      });
    });

  server.on("/resetErrorStates", HTTP_POST, [](AsyncWebServerRequest *request)
            {
        outOfTreats = false;
        outOfTreats_hopper = false;

        String response = String(MAIN_PAGE_HEADER) + 
                        COMMON_HEADER +
                        R"(
                        <h1>Error States Reset</h1>
                        <div class="status-message status-success">
                            <p>Error states have been cleared!</p>
                        </div>
                        <meta http-equiv="refresh" content="2;url=/">
                        )" + 
                        COMMON_FOOTER;
        request->send(200, "text/html", response); });

  server.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request)
            {
        String response = String(MAIN_PAGE_HEADER) + 
                        COMMON_HEADER +
                        R"(
                        <h1>Restarting</h1>
                        <div class="status-message status-info">
                            <p>Device is restarting...</p>
                        </div>
                        )" + 
                        COMMON_FOOTER;
        request->send(200, "text/html", response);
        request->onDisconnect([]() {
            ESP.restart();
        }); });

  server.on("/resetStats", HTTP_POST, [](AsyncWebServerRequest *request)
            {

        totalDistance = 0;
        totalTreatsDispensed = 0;
        hallEffectCount = 0;

        preferences.begin("conf", false);
        preferences.putInt("totalDistance", 0);
        preferences.putInt("totalTreatsDispensed", 0);
        preferences.end();

        String response = String(MAIN_PAGE_HEADER) + 
                        COMMON_HEADER +
                        R"(
                        <h1>Statistics Reset</h1>
                        <div class="status-message status-success">
                            <p>All statistics have been reset to zero!</p>
                        </div>
                        <meta http-equiv="refresh" content="2;url=/">
                        )" + 
                        COMMON_FOOTER;
        request->send(200, "text/html", response); });

  server.on("/dispenseTreat", HTTP_POST, [](AsyncWebServerRequest *request)
            {
        forceDispense = true;
        String response = String(MAIN_PAGE_HEADER) + 
                        COMMON_HEADER +
                        R"(
                        <h1>Treat Dispensed</h1>
                        <div class="status-message status-success">
                            <p>A treat has been dispensed!</p>
                        </div>
                        <meta http-equiv="refresh" content="2;url=/">
                        )" + 
                        COMMON_FOOTER;
        request->send(200, "text/html", response); });

  server.on("/settings", HTTP_POST, [](AsyncWebServerRequest *request)
            {
              // Process MQTT settings
              if (request->hasParam("mqttServer", true))
              {
                mqttConf.server = request->getParam("mqttServer", true)->value();
              }

              if (request->hasParam("mqttPort", true))
              {
                mqttConf.port = request->getParam("mqttPort", true)->value().toInt();
              }

              if (request->hasParam("mqttUsername", true))
              {
                mqttConf.username = request->getParam("mqttUsername", true)->value();
              }

              if (request->hasParam("mqttPassword", true))
              {
                mqttConf.password = request->getParam("mqttPassword", true)->value();
              }

              if (request->hasParam("mqttTopicPrefix", true))
              {
                mqttConf.topicPrefix = request->getParam("mqttTopicPrefix", true)->value();
              }

              if (request->hasParam("mqttEnabled", true))
              {
                // the param comes back with the value of "on" if its enabled, and just doesn't exist as a param if its off... so this is an easy way to do it without actually checking the value.
                Serial.println("Enabling MQTT");
                mqttConf.mqttEnabled = true;
              }
              else
              {
                Serial.println("Disabling MQTT");
                mqttConf.mqttEnabled = false;
              }
              saveConfig();

              mqttClient.disconnect();

              String response = String(MAIN_PAGE_HEADER) +
                                COMMON_HEADER +
                                R"(
                <h1>Settings Updated</h1>
                <div class="status-message status-success">
                    <p>Settings have been saved successfully!</p>
                </div>
                <meta http-equiv="refresh" content="2;url=/">
                )" +
                                COMMON_FOOTER;
              request->send(200, "text/html", response); });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (networkState == NetworkState::CONNECTED || networkState == NetworkState::TRIAL_MODE)
    {
      request->send(200, "text/html", build_main_page_body(mqttClient.connected(), hallEffectCount, hallEffectRunDistanceMultiplier, distanceThreshold, totalDistance, totalTreatsDispensed, outOfTreats_hopper, mqttConf.server, mqttConf.port, mqttConf.username, mqttConf.password, mqttConf.topicPrefix, mqttConf.mqttEnabled));
    }
    else
    {
      String response = String(AP_CONFIG_PAGE_HEADER) + 
                      COMMON_HEADER + 
                      AP_CONFIG_PAGE + 
                      COMMON_FOOTER;
      request->send(200, "text/html", response);
    } });
}
