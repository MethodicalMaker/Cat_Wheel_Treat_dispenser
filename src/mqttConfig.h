#ifndef mqttconf_h
#define mqttconf_h
#include <Arduino.h>
struct MQTTConfig
{
  String server;
  int port;
  String username;
  String password;
  String topicPrefix;
  bool mqttEnabled;
};
#endif