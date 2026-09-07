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

  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, _serverUrl)) {
    error = "Cannot initialize HTTP client";
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Key", _deviceApiKey);
  http.addHeader("Connection", "close");
  const int statusCode = http.POST(telemetryJson);
  const String responseBody = statusCode > 0 ? http.getString() : String();
  http.end();
  client.stop();

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

bool HttpGateway::fetchSimulation(SimulationState &simState, String &error) const {
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected";
    return false;
  }

  String simUrl = String(_serverUrl);
  const int idx = simUrl.lastIndexOf("/analyze");
  if (idx > 0) {
    simUrl = simUrl.substring(0, idx) + "/simulation";
  } else {
    simUrl += "/simulation";
  }

  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(3000);
  if (!http.begin(client, simUrl)) {
    error = "Cannot initialize HTTP client for simulation";
    return false;
  }

  http.addHeader("Connection", "close");
  const int statusCode = http.GET();
  const String responseBody = statusCode > 0 ? http.getString() : String();
  http.end();
  client.stop();

  if (statusCode < 200 || statusCode >= 300) {
    error = "HTTP " + String(statusCode) + ": " + responseBody;
    return false;
  }

  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(document, responseBody);
  if (jsonError) {
    error = "Invalid simulation JSON: " + String(jsonError.c_str());
    return false;
  }

  simState.active = document["active"] | false;
  simState.scenario = document["scenario"] | "normal";
  if (simState.active && document["analysis"].is<JsonObject>()) {
    JsonObject a = document["analysis"];
    simState.analysis.riskLevel = a["risk_level"] | "UNKNOWN";
    simState.analysis.hazard = a["hazard"] | "UNKNOWN";
    simState.analysis.confidencePercent = a["confidence_percent"] | 0;
    simState.analysis.advice = a["advice"] | "";
    simState.analysis.reason = a["reason"] | "";
    if (a["outputs"].is<JsonObject>()) {
      simState.analysis.ledColor = a["outputs"]["led_color"] | "GREEN";
      simState.analysis.buzzerMode = a["outputs"]["buzzer_mode"] | "OFF";
    }
  }
  return true;
}

