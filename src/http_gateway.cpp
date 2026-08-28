#include "http_gateway.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

namespace {
constexpr uint32_t HTTP_TIMEOUT_MS = 15000;
}

HttpGateway::HttpGateway(const char *serverUrl, const char *deviceApiKey)
    : _serverUrl(serverUrl), _deviceApiKey(deviceApiKey) {}

bool HttpGateway::analyze(const String &telemetryJson, ServerAnalysis &analysis,
                          String &error) const {
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected";
    return false;
  }

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(_serverUrl)) {
    error = "Cannot initialize HTTP client";
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Key", _deviceApiKey);
  const int statusCode = http.POST(telemetryJson);
  const String responseBody = statusCode > 0 ? http.getString() : String();
  http.end();

  if (statusCode < 200 || statusCode >= 300) {
    error = "HTTP " + String(statusCode) + ": " + responseBody;
    return false;
  }

  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(document, responseBody);
  if (jsonError) {
    error = "Invalid server JSON: " + String(jsonError.c_str());
    return false;
  }

  if (!document["outputs"]["led_color"].is<const char *>() ||
      !document["outputs"]["buzzer_mode"].is<const char *>() ||
      !document["advice"].is<const char *>()) {
    error = "Server response is missing outputs or advice";
    return false;
  }

  analysis.riskLevel = document["risk_level"] | "UNKNOWN";
  analysis.hazard = document["hazard"] | "UNKNOWN";
  analysis.confidencePercent = document["confidence_percent"] | 0;
  analysis.advice = document["advice"].as<String>();
  analysis.reason = document["reason"] | "";
  analysis.ledColor = document["outputs"]["led_color"].as<String>();
  analysis.buzzerMode = document["outputs"]["buzzer_mode"].as<String>();
  return true;
}
