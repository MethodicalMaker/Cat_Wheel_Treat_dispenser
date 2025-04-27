// functions.h
#ifndef FUNCTIONS_H
#define FUNCTIONS_H

void wifiManagerTask(void *pvParameters);
void mqttServerTask(void *pvParameters);
void mainTask(void *pvParameters);
void webServerTask(void *pvParameters);
String URLDecode(String input);
void startAPMode();
void connectToWiFi();
void saveConfig();
void clearConfig();
void handleWebClientMainMode(WiFiClient &client, const String &request);
void handleWebClientAPConfig(WiFiClient &client, const String &request);
String getFormValue(String request, String key);
void setupWebServerRoutes(AsyncWebServer &server);
void streamFromProgmem(WiFiClient &client, const char* pgmContent, ...);
void mqttPublishUsageStats();
void mqttReconnect();
void saveStatisticsTask(void* pvParameters);
void mqttCallback(char *topic, byte *payload, unsigned int length);
void setInitialConfig();
void saveWifi();
void dispenseTreat();
void clearWifi();

#endif // FUNCTIONS_H
