#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "moisture.h"
#include "pages.h"

namespace {
constexpr uint8_t SOIL_SENSOR_PIN = GPIO_NUM_4;
constexpr uint8_t READING_LED_PIN = GPIO_NUM_8;
constexpr uint32_t SERIAL_BAUD_RATE = 115200;
constexpr uint32_t SAMPLE_INTERVAL_MS = 1050;
constexpr uint32_t LED_FLASH_DURATION_MS = 50;
constexpr uint32_t CONNECTION_TIMEOUT_MS = 20000;
constexpr uint32_t RESPONSE_GRACE_MS = 500;
constexpr char AP_NAME[] = "rootlet";
constexpr char CREDENTIALS_KEY[] = "credentials";

struct Credentials {
  char ssid[33];
  char password[65];
};

enum class ConnectionState { Setup, Connecting, Connected };
enum class DeferredAction { None, Finish, Reset };

WebServer server(80);
Preferences preferences;
Credentials savedCredentials{};
Credentials pendingCredentials{};
ConnectionState state = ConnectionState::Setup;
DeferredAction deferredAction = DeferredAction::None;
bool storageReady = false;
bool hasSavedCredentials = false;
bool savePending = false;
bool apActive = false;
bool mdnsActive = false;
bool scanStarted = false;
uint32_t connectionStart = 0;
uint32_t actionStart = 0;
uint32_t lastSample = 0;
uint32_t ledStart = 0;
bool ledOn = false;
int sensorReading = 0;
String statusMessage;

String jsonString(const String& value) {
  String result = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    const unsigned char c = value[i];
    if (c == '"' || c == '\\') {
      result += '\\';
      result += static_cast<char>(c);
    } else if (c < 0x20) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", c);
      result += escaped;
    } else {
      result += static_cast<char>(c);
    }
  }
  return result + '"';
}

void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void sendError(int code, const String& message) {
  sendJson(code, "{\"error\":" + jsonString(message) + "}");
}

bool onSetupNetwork() {
  return apActive && server.client().localIP() == WiFi.softAPIP();
}

bool startSetupAP() {
  if (apActive) return true;
  if (!WiFi.mode(WIFI_AP_STA) || !WiFi.softAP(AP_NAME)) {
    Serial.println("Error: could not start the rootlet setup network.");
    statusMessage = "Could not start the setup network. Restart Rootlet and check the serial monitor.";
    return false;
  }
  apActive = true;
  Serial.print("Setup network rootlet: http://");
  Serial.println(WiFi.softAPIP());
  return true;
}

void stopMdns() {
  if (mdnsActive) MDNS.end();
  mdnsActive = false;
}

void enterSetup(const String& message) {
  stopMdns();
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);
  state = ConnectionState::Setup;
  savePending = false;
  pendingCredentials = {};
  statusMessage = message;
  startSetupAP();
}

bool loadCredentials() {
  if (!preferences.isKey(CREDENTIALS_KEY)) return false;
  if (preferences.getBytesLength(CREDENTIALS_KEY) != sizeof(savedCredentials) ||
      preferences.getBytes(CREDENTIALS_KEY, &savedCredentials, sizeof(savedCredentials)) != sizeof(savedCredentials) ||
      savedCredentials.ssid[0] == '\0' ||
      savedCredentials.ssid[sizeof(savedCredentials.ssid) - 1] != '\0' ||
      savedCredentials.password[sizeof(savedCredentials.password) - 1] != '\0') {
    statusMessage = "Saved Wi-Fi credentials could not be read. Choose a network again.";
    Serial.println("Error: saved Wi-Fi credentials could not be read.");
    return false;
  }
  return true;
}

bool saveCredentials() {
  if (!storageReady || preferences.putBytes(CREDENTIALS_KEY, &pendingCredentials, sizeof(pendingCredentials)) != sizeof(pendingCredentials)) {
    Serial.println("Error: could not save Wi-Fi credentials.");
    statusMessage = "Connected, but could not save Wi-Fi credentials. Try connecting again.";
    return false;
  }
  savedCredentials = pendingCredentials;
  hasSavedCredentials = true;
  return true;
}

void beginConnection(const Credentials& credentials, bool persistOnSuccess) {
  pendingCredentials = credentials;
  savePending = persistOnSuccess;
  stopMdns();
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);
  WiFi.setAutoReconnect(true);
  state = ConnectionState::Connecting;
  connectionStart = millis();
  statusMessage = "Connecting to Wi-Fi...";
  if (WiFi.begin(credentials.ssid, credentials.password) == WL_CONNECT_FAILED) {
    enterSetup("Could not start the Wi-Fi connection. Please try again.");
  }
}

void connectionEstablished() {
  if (savePending && !saveCredentials()) {
    enterSetup("Connected, but could not save Wi-Fi credentials. Please try again.");
    return;
  }
  savePending = false;
  pendingCredentials = {};
  state = ConnectionState::Connected;
  mdnsActive = MDNS.begin("rootlet");
  if (!mdnsActive) Serial.println("Error: mDNS failed to start; use the IP address.");
  statusMessage = "Connected to Wi-Fi.";
  Serial.print("Home network address: http://");
  Serial.println(WiFi.localIP());
  if (mdnsActive) Serial.println("mDNS address: http://rootlet.local/");
}

void updateConnection() {
  const wl_status_t wifiStatus = WiFi.status();
  if (state == ConnectionState::Connecting) {
    if (wifiStatus == WL_CONNECTED && WiFi.SSID() == pendingCredentials.ssid &&
        WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      connectionEstablished();
    } else if (millis() - connectionStart >= CONNECTION_TIMEOUT_MS) {
      enterSetup("Could not connect within 20 seconds. Choose a network and try again.");
    }
  } else if (state == ConnectionState::Connected && wifiStatus != WL_CONNECTED) {
    stopMdns();
    state = ConnectionState::Connecting;
    pendingCredentials = savedCredentials;
    connectionStart = millis();
    statusMessage = "Wi-Fi connection lost. Trying to reconnect...";
    Serial.println("Wi-Fi connection lost; trying to reconnect.");
  }
}

bool supportedSecurity(wifi_auth_mode_t security) {
  return security == WIFI_AUTH_OPEN || security == WIFI_AUTH_WPA2_PSK ||
      security == WIFI_AUTH_WPA_WPA2_PSK || security == WIFI_AUTH_WPA3_PSK ||
      security == WIFI_AUTH_WPA2_WPA3_PSK;
}

const char* securityName(wifi_auth_mode_t security) {
  switch (security) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "unsupported security";
  }
}

bool validPassword(const String& password) {
  if (password.length() == 0) return true;
  if (password.length() < 8 || password.length() > 64) return false;
  for (size_t i = 0; i < password.length(); ++i) {
    const char c = password[i];
    if (password.length() == 64) {
      if (!isxdigit(static_cast<unsigned char>(c))) return false;
    } else if (c < 32 || c > 126) {
      return false;
    }
  }
  return true;
}

void setupRoutes() {
  server.on("/", HTTP_GET, [] {
    if (onSetupNetwork()) {
      server.send_P(200, "text/html; charset=utf-8", SETUP_PAGE);
    } else if (state == ConnectionState::Connected) {
      server.send_P(200, "text/html; charset=utf-8", MOISTURE_PAGE);
    } else {
      server.send(503, "text/plain", "Rootlet is not connected.");
    }
  });
  server.on("/api/status", HTTP_GET, [] {
    const bool ready = state == ConnectionState::Connected;
    String body = "{\"ready\":";
    body += ready ? "true" : "false";
    body += ",\"connecting\":";
    body += state == ConnectionState::Connecting ? "true" : "false";
    body += ",\"mdns\":";
    body += mdnsActive ? "true" : "false";
    body += ",\"ip\":" + jsonString(ready ? WiFi.localIP().toString() : "");
    body += ",\"message\":" + jsonString(statusMessage) + "}";
    sendJson(200, body);
  });
  server.on("/api/scan", HTTP_POST, [] {
    if (!onSetupNetwork()) return sendError(403, "Scanning is only available on the setup network.");
    if (state != ConnectionState::Setup) return sendError(409, "Wait until the current connection attempt finishes.");
    if (scanStarted && WiFi.scanComplete() == WIFI_SCAN_RUNNING) return sendJson(200, "{\"scanning\":true}");
    WiFi.scanDelete();
    const int result = WiFi.scanNetworks(true);
    if (result != WIFI_SCAN_RUNNING && result < 0) return sendError(500, "Could not start the Wi-Fi scan.");
    scanStarted = true;
    sendJson(200, "{\"scanning\":true}");
  });
  server.on("/api/scan", HTTP_GET, [] {
    if (!onSetupNetwork()) return sendError(403, "Scanning is only available on the setup network.");
    if (!scanStarted) return sendError(409, "Start a scan first.");
    const int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return sendJson(200, "{\"scanning\":true}");
    if (count < 0) {
      scanStarted = false;
      return sendError(500, "Wi-Fi scan failed. Try again.");
    }
    String body = "{\"scanning\":false,\"networks\":[";
    bool first = true;
    for (int i = 0; i < count; ++i) {
      const String ssid = WiFi.SSID(i);
      if (ssid.isEmpty()) continue;
      const wifi_auth_mode_t security = WiFi.encryptionType(i);
      if (!first) body += ',';
      first = false;
      body += "{\"ssid\":" + jsonString(ssid) + ",\"signal\":" + String(WiFi.RSSI(i));
      body += ",\"security\":" + jsonString(securityName(security)) + ",\"supported\":";
      body += supportedSecurity(security) ? "true}" : "false}";
    }
    body += "]}";
    sendJson(200, body);
    WiFi.scanDelete();
    scanStarted = false;
  });
  server.on("/api/connect", HTTP_POST, [] {
    if (!onSetupNetwork()) return sendError(403, "Connect using the setup network.");
    if (state != ConnectionState::Setup) return sendError(409, "A connection attempt is already in progress.");
    if (scanStarted && WiFi.scanComplete() == WIFI_SCAN_RUNNING) return sendError(409, "Wait for the Wi-Fi scan to finish.");
    if (!storageReady) return sendError(500, "Credential storage is unavailable. Restart Rootlet.");
    if (!server.hasArg("ssid") || !server.hasArg("password")) return sendError(400, "Network and password are required.");
    const String ssid = server.arg("ssid");
    const String password = server.arg("password");
    if (ssid.isEmpty() || ssid.length() > 32 || !validPassword(password)) {
      return sendError(400, "Choose a network and enter a valid Wi-Fi password.");
    }
    Credentials credentials{};
    ssid.toCharArray(credentials.ssid, sizeof(credentials.ssid));
    password.toCharArray(credentials.password, sizeof(credentials.password));
    WiFi.scanDelete();
    scanStarted = false;
    beginConnection(credentials, true);
    if (state != ConnectionState::Connecting) return sendError(500, statusMessage);
    sendJson(200, "{\"connecting\":true}");
  });
  server.on("/api/finish", HTTP_POST, [] {
    if (!onSetupNetwork() || state != ConnectionState::Connected) return sendError(409, "Connect to home Wi-Fi before finishing setup.");
    sendJson(200, "{\"ok\":true}");
    deferredAction = DeferredAction::Finish;
    actionStart = millis();
  });
  server.on("/api/moisture", HTTP_GET, [] {
    if (state != ConnectionState::Connected) return sendError(503, "Wi-Fi is not connected.");
    sendJson(200, "{\"raw\":" + String(sensorReading) + ",\"percent\":" + String(moisturePercent(sensorReading)) + "}");
  });
  server.on("/api/reset", HTTP_POST, [] {
    if (state != ConnectionState::Connected || onSetupNetwork()) return sendError(403, "Reset Wi-Fi from the home network.");
    if (!storageReady || (preferences.isKey(CREDENTIALS_KEY) && !preferences.remove(CREDENTIALS_KEY))) {
      Serial.println("Error: could not remove saved Wi-Fi credentials.");
      return sendError(500, "Could not remove saved Wi-Fi credentials.");
    }
    savedCredentials = {};
    hasSavedCredentials = false;
    sendJson(200, "{\"ok\":true}");
    deferredAction = DeferredAction::Reset;
    actionStart = millis();
  });
  server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
}

void updateDeferredAction() {
  if (deferredAction == DeferredAction::None || millis() - actionStart < RESPONSE_GRACE_MS) return;
  const DeferredAction action = deferredAction;
  deferredAction = DeferredAction::None;
  if (action == DeferredAction::Reset) {
    enterSetup("Wi-Fi reset. Choose a network.");
  } else if (state == ConnectionState::Connected && apActive) {
    if (WiFi.mode(WIFI_STA)) {
      apActive = false;
      Serial.println("Setup network turned off.");
    } else {
      statusMessage = "Could not turn off the setup network. Please try Finish setup again.";
      Serial.println("Error: could not turn off the setup network.");
    }
  }
}

void updateSensor() {
  const uint32_t now = millis();
  if (ledOn && now - ledStart >= LED_FLASH_DURATION_MS) {
    digitalWrite(READING_LED_PIN, LOW);
    ledOn = false;
  }
  if (now - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample = now;
    sensorReading = analogRead(SOIL_SENSOR_PIN);
   // Serial.print("Raw ADC reading: ");
   // Serial.println(sensorReading);
   //
    digitalWrite(READING_LED_PIN, HIGH);
    ledStart = now;
    ledOn = true;
  }
}
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  const uint32_t serialWaitStart = millis();
// while (!Serial && millis() - serialWaitStart < 2000) delay(10);
  analogReadResolution(12);
  pinMode(SOIL_SENSOR_PIN, INPUT);
  pinMode(READING_LED_PIN, OUTPUT);
  digitalWrite(READING_LED_PIN, LOW);
  Serial.println("Capacitive soil moisture sensor");
  Serial.println("AOUT -> GPIO4, VCC -> 3V3, GND -> GND");

  sensorReading = analogRead(SOIL_SENSOR_PIN);
  lastSample = millis() - SAMPLE_INTERVAL_MS;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);

  storageReady = preferences.begin("rootlet", false);
  if (!storageReady) {
    Serial.println("Error: could not open credential storage.");
    statusMessage = "Credential storage is unavailable. Restart Rootlet.";
  } else {
    hasSavedCredentials = loadCredentials();
  }

  setupRoutes();
  server.begin();
  if (hasSavedCredentials) {
    beginConnection(savedCredentials, false);
  } else {
    enterSetup(statusMessage.length() ? statusMessage : "Choose a Wi-Fi network.");
  }
}

void loop() {
  updateSensor();
  updateConnection();
  server.handleClient();
  updateDeferredAction();
}
