#include "firebase_gateway.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace {
constexpr uint32_t FIREBASE_TIMEOUT_MS = 3500;
}

FirebaseGateway::FirebaseGateway(const char *firebaseUrl, const char *firebaseAuth,
                                 const char *deviceId)
    : _firebaseUrl(firebaseUrl), _firebaseAuth(firebaseAuth), _deviceId(deviceId) {}

bool FirebaseGateway::pushTelemetry(const String &telemetryJson, String &error) const {
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected";
    return false;
  }

  if (_firebaseUrl == nullptr || strlen(_firebaseUrl) == 0) {
    error = "FIREBASE_URL is missing";
    return false;
  }

  String url = String(_firebaseUrl);
  if (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }

  // HTTP PUT onto /devices/<DEVICE_ID>/telemetry.json completely replaces (overwrites)
  // the node content with the newly read telemetry data.
  url += "/devices/";
  url += (_deviceId != nullptr && strlen(_deviceId) > 0) ? _deviceId : "esp32-node-01";
  url += "/telemetry.json";

  if (_firebaseAuth != nullptr && strlen(_firebaseAuth) > 0) {
    url += "?auth=";
    url += _firebaseAuth;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(3);

  HTTPClient http;
  http.setTimeout(FIREBASE_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    error = "Cannot initialize WiFiClientSecure for Firebase URL: " + url;
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  // HTTP PUT replaces (overwrites) existing data at the specified path in Firebase Realtime Database
  const int statusCode = http.PUT(telemetryJson);
  const String responseBody = statusCode > 0 ? http.getString() : String();
  http.end();
  client.stop();


  if (statusCode < 200 || statusCode >= 300) {
    error = "HTTP " + String(statusCode) + ": " + responseBody;
    return false;
  }

  return true;
}

bool FirebaseGateway::fetchAnalysis(ServerAnalysis &analysis, String &error) const {
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected";
    return false;
  }

  if (_firebaseUrl == nullptr || strlen(_firebaseUrl) == 0) {
    error = "FIREBASE_URL is missing";
    return false;
  }

  String url = String(_firebaseUrl);
  if (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }

  url += "/devices/";
  url += (_deviceId != nullptr && strlen(_deviceId) > 0) ? _deviceId : "esp32-node-01";
  url += "/analysis.json";

  if (_firebaseAuth != nullptr && strlen(_firebaseAuth) > 0) {
    url += "?auth=";
    url += _firebaseAuth;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(3);

  HTTPClient http;
  http.setTimeout(FIREBASE_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    error = "Cannot initialize WiFiClientSecure for Firebase URL: " + url;
    return false;
  }

  const int statusCode = http.GET();
  const String responseBody = statusCode > 0 ? http.getString() : String();
  http.end();
  client.stop();


  if (statusCode < 200 || statusCode >= 300) {
    error = "Firebase GET HTTP " + String(statusCode) + ": " + responseBody;
    return false;
  }

  if (responseBody == "null" || responseBody.length() < 10) {
    error = "No analysis data on Firebase";
    return false;
  }

  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(document, responseBody);
  if (jsonError) {
    error = "Invalid Firebase JSON: " + String(jsonError.c_str());
    return false;
  }

  if (!document["outputs"]["led_color"].is<const char *>() ||
      !document["outputs"]["buzzer_mode"].is<const char *>() ||
      !document["advice"].is<const char *>()) {
    error = "Firebase response is missing outputs or advice";
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

bool FirebaseGateway::fetchSimulation(SimulationState &simState, String &error) const {
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected";
    return false;
  }

  if (_firebaseUrl == nullptr || strlen(_firebaseUrl) == 0) {
    error = "FIREBASE_URL is missing";
    return false;
  }

  String url = String(_firebaseUrl);
  if (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }

  url += "/devices/";
  url += (_deviceId != nullptr && strlen(_deviceId) > 0) ? _deviceId : "esp32-node-01";
  url += "/simulation.json";

  if (_firebaseAuth != nullptr && strlen(_firebaseAuth) > 0) {
    url += "?auth=";
    url += _firebaseAuth;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(3);

  HTTPClient http;
  http.setTimeout(FIREBASE_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    error = "Cannot initialize WiFiClientSecure for simulation URL";
    return false;
  }

  const int statusCode = http.GET();
  const String responseBody = statusCode > 0 ? http.getString() : String();
  http.end();
  client.stop();


  if (statusCode < 200 || statusCode >= 300) {
    error = "Firebase simulation GET HTTP " + String(statusCode) + ": " + responseBody;
    return false;
  }

  if (responseBody == "null" || responseBody.length() < 4) {
    simState.active = false;
    return true;
  }

  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(document, responseBody);
  if (jsonError) {
    error = "Invalid simulation JSON: " + String(jsonError.c_str());
    return false;
  }

  simState.active = document["active"] | false;
  simState.scenario = document["scenario"] | "";

  if (simState.active && document["analysis"].is<JsonObject>()) {
    JsonObject a = document["analysis"].as<JsonObject>();
    simState.analysis.riskLevel = a["risk_level"] | "UNKNOWN";
    simState.analysis.hazard = a["hazard"] | "UNKNOWN";
    simState.analysis.confidencePercent = a["confidence_percent"] | 0;
    simState.analysis.advice = a["advice"] | "";
    simState.analysis.reason = a["reason"] | "";
    if (a["outputs"].is<JsonObject>()) {
      simState.analysis.ledColor = a["outputs"]["led_color"] | "BLUE";
      simState.analysis.buzzerMode = a["outputs"]["buzzer_mode"] | "OFF";
    }
  }

  return true;
}

