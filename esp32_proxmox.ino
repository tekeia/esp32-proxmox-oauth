#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <ArduinoOTA.h>
#include "qrcode.h"
#include "proxmox_logo.h"

const char* ssid     = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";

String kcHost   = "http://192.168.4.104:8080";
String realm    = "homelab";
String clientId = "esp32-display";
String apiUrl   = "http://192.168.4.104:5000/stats";

TFT_eSPI tft = TFT_eSPI();

String accessToken = "";
String refreshToken = "";
bool authenticated = false;
unsigned long lastUpdate = 0;

void drawText(int x, int y, String txt, uint16_t color, int size) {
  tft.setTextSize(size);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(x, y);
  tft.print(txt);
}

void drawQrCallback(esp_qrcode_handle_t qr) {
  int size = esp_qrcode_get_size(qr);
  int scale = 170 / size;
  if (scale < 1) scale = 1;
  int dim = size * scale;
  int offX = 10;
  int offY = (240 - dim) / 2;
  tft.fillRect(offX - 8, offY - 8, dim + 16, dim + 16, TFT_WHITE);
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      uint16_t color = esp_qrcode_get_module(qr, x, y) ? TFT_BLACK : TFT_WHITE;
      tft.fillRect(offX + x * scale, offY + y * scale, scale, scale, color);
    }
  }
}

void drawQR(String text) {
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = drawQrCallback;
  cfg.max_qrcode_version = 10;
  esp_qrcode_generate(&cfg, text.c_str());
}

bool refreshAccessToken() {
  HTTPClient http;
  http.begin(kcHost + "/realms/" + realm + "/protocol/openid-connect/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "grant_type=refresh_token";
  body += "&refresh_token=" + refreshToken;
  body += "&client_id=" + clientId;
  int status = http.POST(body);
  String resp = http.getString();
  http.end();
  if (status != 200) return false;
  JsonDocument tok;
  deserializeJson(tok, resp);
  accessToken  = tok["access_token"].as<String>();
  refreshToken = tok["refresh_token"].as<String>();
  return true;
}

void showStats() {
  HTTPClient http;
  http.begin(apiUrl);
  http.addHeader("Authorization", "Bearer " + accessToken);
  int status = http.GET();
  String resp = http.getString();
  http.end();

  if (status == 401) {
    if (refreshAccessToken()) {
      HTTPClient http2;
      http2.begin(apiUrl);
      http2.addHeader("Authorization", "Bearer " + accessToken);
      status = http2.GET();
      resp = http2.getString();
      http2.end();
    }
  }

  if (status != 200) {
    tft.fillScreen(TFT_BLACK);
    drawText(10, 100, "API error: " + String(status), TFT_RED, 2);
    return;
  }

  JsonDocument doc;
  deserializeJson(doc, resp);

  String user = doc["user"].as<String>();
  float cpu = doc["cpu"];
  float ram = doc["ram"];
  int vmsRunning = doc["vms_running"];
  int vmsTotal = doc["vms_total"];
  String backup = doc["last_backup"].as<String>();

  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);
  tft.pushImage((320 - LOGO_W) / 2, 5, LOGO_W, LOGO_H, proxmoxLogo);

  int y = 55;
  drawText(5, y, "CPU " + String(cpu, 1) + "%  RAM " + String(ram, 1) + "%", TFT_WHITE, 2);
  y += 25;

  JsonArray storages = doc["storages"].as<JsonArray>();
  for (JsonObject s : storages) {
    drawText(5, y, s["name"].as<String>() + ": " + String(s["used_pct"].as<float>(), 0) + "%", TFT_YELLOW, 2);
    y += 25;
  }

  drawText(5, y, "VMs: " + String(vmsRunning) + "/" + String(vmsTotal) + " on", TFT_GREEN, 2);
  y += 25;

  uint16_t backupColor = (backup == "OK") ? TFT_GREEN : TFT_RED;
  drawText(5, y, "Backup: " + backup, backupColor, 2);
  y += 25;

  drawText(5, y, "User: " + user, TFT_WHITE, 2);
}

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  drawText(10, 10, "Connecting WiFi...", TFT_WHITE, 2);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  ArduinoOTA.begin();

  HTTPClient http;
  http.begin(kcHost + "/realms/" + realm + "/protocol/openid-connect/auth/device");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.POST("client_id=" + clientId + "&scope=openid");
  String resp = http.getString();
  http.end();

  JsonDocument doc;
  deserializeJson(doc, resp);
  String verComplete = doc["verification_uri_complete"].as<String>();
  String userCode    = doc["user_code"].as<String>();
  String deviceCode  = doc["device_code"].as<String>();
  int interval       = doc["interval"];

  tft.fillScreen(TFT_BLACK);
  drawQR(verComplete);
  drawText(200, 60,  "Scan", TFT_WHITE, 2);
  drawText(200, 90,  "to approve", TFT_WHITE, 2);
  drawText(200, 140, userCode, TFT_GREEN, 2);

  while (true) {
    delay(interval * 1000);
    HTTPClient http2;
    http2.begin(kcHost + "/realms/" + realm + "/protocol/openid-connect/token");
    http2.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String body = "grant_type=urn:ietf:params:oauth:grant-type:device_code";
    body += "&device_code=" + deviceCode;
    body += "&client_id=" + clientId;
    int status = http2.POST(body);
    String tresp = http2.getString();
    http2.end();

    if (status == 200) {
      JsonDocument tok;
      deserializeJson(tok, tresp);
      accessToken  = tok["access_token"].as<String>();
      refreshToken = tok["refresh_token"].as<String>();
      authenticated = true;
      break;
    }
  }
}

void loop() {
  ArduinoOTA.handle();
  if (authenticated && millis() - lastUpdate > 5000) {
    lastUpdate = millis();
    showStats();
  }
}
