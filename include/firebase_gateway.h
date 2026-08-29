#pragma once

#include <Arduino.h>

class FirebaseGateway {
 public:
  FirebaseGateway(const char *firebaseUrl, const char *firebaseAuth, const char *deviceId);

  bool pushTelemetry(const String &telemetryJson, String &error) const;

 private:
  const char *_firebaseUrl;
  const char *_firebaseAuth;
  const char *_deviceId;
};
