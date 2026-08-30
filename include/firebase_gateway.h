#pragma once

#include <Arduino.h>
#include "http_gateway.h"

struct SimulationState {
  bool active = false;
  String scenario = "";
  ServerAnalysis analysis;
};

class FirebaseGateway {
 public:
  FirebaseGateway(const char *firebaseUrl, const char *firebaseAuth, const char *deviceId);

  bool pushTelemetry(const String &telemetryJson, String &error) const;
  bool fetchAnalysis(ServerAnalysis &analysis, String &error) const;
  bool fetchSimulation(SimulationState &simState, String &error) const;

 private:
  const char *_firebaseUrl;
  const char *_firebaseAuth;
  const char *_deviceId;
};
