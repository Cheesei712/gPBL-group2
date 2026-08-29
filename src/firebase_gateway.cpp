#include "firebase_gateway.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace {
constexpr uint32_t FIREBASE_TIMEOUT_MS = 10000;
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

  if (statusCode < 200 || statusCode >= 300) {
    error = "HTTP " + String(statusCode) + ": " + responseBody;
    return false;
  }

  return true;
}
