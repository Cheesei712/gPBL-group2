#pragma once

#include <Arduino.h>

struct ServerAnalysis {
  String riskLevel;
  String hazard;
  String advice;
  String reason;
  String ledColor;
  String buzzerMode;
  int confidencePercent = 0;
};

class HttpGateway {
 public:
  HttpGateway(const char *serverUrl, const char *deviceApiKey);

  bool analyze(const String &telemetryJson, ServerAnalysis &analysis, String &error) const;

 private:
  const char *_serverUrl;
  const char *_deviceApiKey;
};

