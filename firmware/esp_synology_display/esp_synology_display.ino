#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>
#include <TFT_eSPI.h>
#include "ota_credentials.h"
#include "nas_logic.h"
#include "web_pages.h"

constexpr uint32_t STALE_MS = 180000;
constexpr uint16_t BG = 0x08A2, FG = 0xEF9D, MUTED = 0x9DB6, ACCENT = 0x4F17, TRACK = 0x2186;
constexpr size_t MAX_WARNINGS = 1024;
constexpr unsigned EEPROM_SIZE = 128;
constexpr unsigned BRIGHTNESS_MARKER_ADDR = 96, BRIGHTNESS_VALUE_ADDR = 97;
constexpr uint8_t BRIGHTNESS_MARKER = 0xB4;
const char* AP_SSID = "MiniScreen-Setup";
const char* AP_PASS = "12345678";
TFT_eSPI tft;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

struct Volume { double used = 0, total = 0; bool valid = false; String status = "unknown"; };
struct Snapshot {
  Volume volume[2];
  String name = "SYNOLOGY", warnings, temperatures;
  double rx = 0, tx = 0;
  bool rxValid = false, txValid = false;
  NasLogic::Temperature average = {0, 0};
};
Snapshot data;
bool hasData = false, apMode = false, yellowPhase = false;
uint32_t receivedAt = 0, updates = 0, lastRender = 0;
String lastDisplayWarnings;
unsigned warningPage = 0;
bool overviewInitialized = false;
String overviewFields[8];
unsigned brightnessPercent = 100, savedBrightness = 100;
bool brightnessPending = false;
uint32_t brightnessChangedAt = 0;

void applyBrightness(unsigned value) {
  brightnessPercent = value;
  analogWrite(TFT_BL, NasLogic::backlightDuty(value));
}

void loadBrightness() {
  EEPROM.begin(EEPROM_SIZE);
  unsigned value = EEPROM.read(BRIGHTNESS_VALUE_ADDR);
  if (EEPROM.read(BRIGHTNESS_MARKER_ADDR) != BRIGHTNESS_MARKER || value > 100) value = 100;
  EEPROM.end(); savedBrightness = value;
  pinMode(TFT_BL, OUTPUT); analogWriteRange(1023); analogWriteFreq(1000);
  applyBrightness(value);
}

void saveBrightnessIfReady() {
  if (!brightnessPending || millis() - brightnessChangedAt < 1200) return;
  brightnessPending = false;
  if (savedBrightness == brightnessPercent) return;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(BRIGHTNESS_MARKER_ADDR, BRIGHTNESS_MARKER);
  EEPROM.write(BRIGHTNESS_VALUE_ADDR, brightnessPercent);
  if (EEPROM.commit()) savedBrightness = brightnessPercent;
  else { brightnessPending = true; brightnessChangedAt = millis(); }
  EEPROM.end();
}

String asciiText(String text, size_t maxLength) {
  String result; result.reserve(min(text.length(), maxLength));
  for (size_t i = 0; i < text.length() && result.length() < maxLength; ++i) {
    unsigned char c = text[i];
    if (c >= 32 && c <= 126) result += char(c);
    else if (c == '\n' || c == '\r' || c == '\t') result += ' ';
  }
  result.trim(); return result;
}

void appendWarning(String& warnings, const String& text) {
  if (!text.length() || warnings.indexOf(text) >= 0) return;
  if (warnings.length()) warnings += '|';
  warnings += text;
}

String activeWarnings() {
  String warnings = data.warnings;
  if (hasData && NasLogic::stale(millis(), receivedAt, STALE_MS)) appendWarning(warnings, "Updates stopped - last storage readings are stale");
  if (hasData && WiFi.status() != WL_CONNECTED) appendWarning(warnings, "Display WiFi disconnected");
  return warnings;
}

void text(const String& value, int x, int y, int size = 1, uint16_t color = FG, uint16_t background = BG) {
  tft.setTextFont(1); tft.setTextSize(size); tft.setTextColor(color, background);
  tft.setCursor(x, y); tft.print(value);
}

void rightText(const String& value, int right, int y, int size = 1, uint16_t color = FG, uint16_t background = BG) {
  tft.setTextFont(1); tft.setTextSize(size);
  text(value, right - tft.textWidth(value), y, size, color, background);
}

String compactNumber(double value) {
  return String(value, value < 10 ? 2 : value < 100 ? 1 : 0);
}

String rate(double value, bool valid) {
  if (!valid) return "--";
  if (value >= 1000) return compactNumber(value / 1000) + " GB/s";
  if (value > 0 && value < 1) return compactNumber(value * 1000) + " KB/s";
  return compactNumber(value) + " MB/s";
}

void drawRate(double value, bool valid, int x, int y) {
  String formatted = rate(value, valid);
  int separator = formatted.indexOf(' ');
  String amount = separator < 0 ? formatted : formatted.substring(0, separator);
  text(amount, x, y, 2);
  if (separator >= 0) text(formatted.substring(separator + 1), x + amount.length() * 12 + 4, y + 7, 1, MUTED);
}

void drawVolume(int index, int y) {
  const Volume& volume = data.volume[index];
  text("VOLUME " + String(index + 1), 12, y + 4, 1, MUTED);
  rightText(volume.valid ? String(NasLogic::percent(volume.used, volume.total)) + "%" : "--%", 228, y, 2);
  tft.fillRoundRect(12, y + 22, 216, 7, 2, TRACK);
  int width = volume.valid ? 216 * NasLogic::percent(volume.used, volume.total) / 100 : 0;
  if (width > 0) tft.fillRect(12, y + 22, width, 7, ACCENT);
  text(volume.valid ? compactNumber(volume.used) + " / " + compactNumber(volume.total) + " TB" : "Capacity unavailable", 12, y + 36, 1, MUTED);
  if (volume.valid) rightText(compactNumber(volume.total - volume.used) + " TB free", 228, y + 36, 1, MUTED);
}

void drawOverview() {
  // Clear the full panel only on entry, never on its one-second refresh tick.
  // All routine paints are restricted to fields whose displayed value changed.
  if (!overviewInitialized) {
    tft.fillScreen(BG);
    for (String& field : overviewFields) field = "";
    text("RX", 12, 168, 1, MUTED); text("TX", 126, 168, 1, MUTED);
    tft.drawFastHLine(12, 205, 216, TRACK);
    text("DRIVES AVG", 12, 216, 1, MUTED);
    overviewInitialized = true;
  }
  String name = data.name.substring(0, 18);
  if (overviewFields[0] != name) {
    overviewFields[0] = name; tft.fillRect(12, 12, 108, 8, BG); text(name, 12, 12);
  }
  bool busy = hasData && (data.volume[0].status != "normal" || data.volume[1].status != "normal");
  String status = hasData ? (busy ? "Storage maintenance" : "Storage healthy") : "Waiting for NAS";
  if (overviewFields[1] != status) {
    overviewFields[1] = status; tft.fillRect(12, 31, 216, 8, BG);
    tft.fillCircle(15, 35, 2, hasData ? ACCENT : MUTED); text(status, 23, 31, 1, MUTED);
  }
  for (int i = 0; i < 2; ++i) {
    const Volume& volume = data.volume[i];
    String signature = volume.valid ? String(NasLogic::percent(volume.used, volume.total)) + ":" + compactNumber(volume.used) + ":" + compactNumber(volume.total) + ":" + compactNumber(volume.total - volume.used) : "unavailable";
    if (overviewFields[2 + i] != signature) {
      overviewFields[2 + i] = signature; int y = i ? 108 : 51;
      tft.fillRect(12, y, 216, 48, BG); drawVolume(i, y);
    }
  }
  String rx = rate(data.rx, data.rxValid), tx = rate(data.tx, data.txValid);
  if (overviewFields[4] != rx) {
    overviewFields[4] = rx; tft.fillRect(12, 183, 102, 16, BG); drawRate(data.rx, data.rxValid, 12, 183);
  }
  if (overviewFields[5] != tx) {
    overviewFields[5] = tx; tft.fillRect(126, 183, 102, 16, BG); drawRate(data.tx, data.txValid, 126, 183);
  }
  String temperature = data.average.count ? String(data.average.mean, 1) : "--";
  if (overviewFields[6] != temperature) {
    overviewFields[6] = temperature; tft.fillRect(78, 214, 50, 13, BG); text(temperature, 78, 216);
    if (data.average.count) { int offset = 78 + temperature.length() * 6; tft.drawCircle(offset + 2, 216, 1, FG); text("C", offset + 6, 216); }
  }
  String age = hasData ? String((millis() - receivedAt) / 1000) + "s ago" : "No data";
  if (overviewFields[7] != age) {
    overviewFields[7] = age; tft.fillRect(156, 216, 72, 8, BG); rightText(age, 228, 216, 1, MUTED);
  }
}

// Convert every complete warning into readable 27-character lines. Long messages
// continue on subsequent pages; none are silently hidden by the 240px screen.
String warningLines[7];
unsigned wrapWarnings(const String& warnings, unsigned skip = 0) {
  for (String& line : warningLines) line = "";
  unsigned count = 0; int start = 0;
  bool lastBlank = false;
  while (start < (int)warnings.length()) {
    int separator = warnings.indexOf('|', start);
    if (separator < 0) separator = warnings.length();
    String remaining = warnings.substring(start, separator); remaining.trim();
    while (remaining.length()) {
      int end = min((int)remaining.length(), 27);
      if (end < (int)remaining.length()) {
        int space = remaining.lastIndexOf(' ', end);
        if (space > 0) end = space;
      }
      if (count >= skip && count < skip + 7) warningLines[count - skip] = remaining.substring(0, end);
      ++count; lastBlank = false;
      remaining.remove(0, end); remaining.trim();
    }
    if (!lastBlank && count) { ++count; lastBlank = true; }
    start = separator + 1;
  }
  if (lastBlank && count) --count;
  return count;
}

void drawWarning(const String& warnings) {
  overviewInitialized = false;
  const uint16_t background = yellowPhase ? TFT_YELLOW : BG;
  const uint16_t foreground = yellowPhase ? TFT_BLACK : TFT_YELLOW;
  tft.fillScreen(background);
  tft.drawRect(2, 2, 236, 236, foreground);
  text("WARNING", 12, 18, 3, foreground, background);
  text("STORAGE / MONITOR ALERT", 12, 51, 1, foreground, background);
  tft.drawFastHLine(12, 70, 216, foreground);
  unsigned lineCount = wrapWarnings(warnings);
  unsigned pages = max(1u, (lineCount + 6) / 7);
  unsigned current = warningPage % pages;
  wrapWarnings(warnings, current * 7);
  for (unsigned row = 0; row < 7; ++row) {
    unsigned index = current * 7 + row;
    if (index < lineCount) text(warningLines[row], 12, 85 + row * 16, 1, foreground, background);
  }
  text("Check DSM Storage Manager", 12, 209, 1, foreground, background);
  rightText(String(current + 1) + "/" + String(pages), 228, 226, 1, foreground, background);
}

void render() {
  if (apMode) return;
  String warnings = activeWarnings();
  if (warnings != lastDisplayWarnings) { warningPage = 0; lastDisplayWarnings = warnings; }
  yellowPhase = ((millis() / 1000) % 2) != 0;
  if (warnings.length()) drawWarning(warnings); else drawOverview();
  lastRender = millis();
}

String jsonQuote(const String& value) {
  String escaped = "\"";
  for (unsigned i = 0; i < value.length(); ++i) { char c = value[i]; if (c == '"' || c == '\\') escaped += '\\'; escaped += c; }
  return escaped + "\"";
}

String stateJson() {
  String warnings = activeWarnings();
  String json = "{\"firmware\":\"synology-two-volumes-1.1\",\"brightness\":" + String(brightnessPercent) + ",\"hasData\":" + String(hasData ? "true" : "false");
  json += ",\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += ",\"updates\":" + String(updates) + ",\"ageSeconds\":" + (hasData ? String((millis() - receivedAt) / 1000) : "null");
  json += ",\"warning\":" + String(warnings.length() ? "true" : "false") + ",\"warnings\":" + jsonQuote(warnings);
  json += ",\"averageDriveTemperatureC\":" + (data.average.count ? String(data.average.mean, 2) : "null");
  json += ",\"temperatureDriveCount\":" + String(data.average.count) + ",\"nasName\":" + jsonQuote(data.name);
  for (int i = 0; i < 2; ++i) {
    json += ",\"volume" + String(i + 1) + "\":{\"status\":" + jsonQuote(data.volume[i].status);
    json += ",\"usedTB\":" + (data.volume[i].valid ? String(data.volume[i].used, 4) : "null");
    json += ",\"totalTB\":" + (data.volume[i].valid ? String(data.volume[i].total, 4) : "null") + "}";
  }
  json += ",\"rxMBps\":" + (data.rxValid ? String(data.rx, 4) : "null");
  json += ",\"txMBps\":" + (data.txValid ? String(data.tx, 4) : "null") + "}";
  return json;
}

bool argNumber(const String& key, double& value) { return NasLogic::number(server.arg(key).c_str(), value); }

void handleNas() {
  // Each POST replaces a full snapshot, so absent/broken sensors never retain
  // plausible values from an older upload or clear a fault accidentally.
  const char* required[] = {"volume1UsedTB", "volume1TotalTB", "volume1Status", "volume2UsedTB", "volume2TotalTB", "volume2Status", "rxMBps", "txMBps", "driveTemps", "storageWarnings"};
  for (const char* key : required) if (!server.hasArg(key)) {
    server.send(400, "application/json", "{\"error\":\"incomplete_snapshot\"}"); return;
  }
  if (server.arg("storageWarnings").length() > MAX_WARNINGS || server.arg("driveTemps").length() > 512) {
    server.send(413, "application/json", "{\"error\":\"payload_too_large\"}"); return;
  }
  Snapshot next;
  next.name = asciiText(server.arg("nasName"), 18);
  if (!next.name.length()) next.name = "SYNOLOGY";
  next.warnings = asciiText(server.arg("storageWarnings"), MAX_WARNINGS);
  for (int i = 0; i < 2; ++i) {
    String prefix = "volume" + String(i + 1);
    Volume& volume = next.volume[i];
    volume.status = asciiText(server.arg(prefix + "Status"), 47);
    volume.valid = argNumber(prefix + "UsedTB", volume.used) && argNumber(prefix + "TotalTB", volume.total) && NasLogic::capacity(volume.used, volume.total);
    if (!volume.valid) appendWarning(next.warnings, "Volume " + String(i + 1) + ": capacity unavailable");
    if (!NasLogic::normalOrMaintenance(volume.status.c_str())) appendWarning(next.warnings, "Volume " + String(i + 1) + ": " + (volume.status.length() ? volume.status : "status unavailable"));
  }
  next.rxValid = argNumber("rxMBps", next.rx) && next.rx >= 0 && next.rx <= 1000000;
  next.txValid = argNumber("txMBps", next.tx) && next.tx >= 0 && next.tx <= 1000000;
  next.temperatures = asciiText(server.arg("driveTemps"), 512);
  next.average = NasLogic::average(next.temperatures.c_str());
  data = next; receivedAt = millis(); hasData = true; ++updates;
  render(); server.send(200, "application/json", stateJson());
}

bool loadCredentials(String& ssid, String& pass) {
  EEPROM.begin(EEPROM_SIZE); char buffer[65];
  for (int i = 0; i < 32; ++i) buffer[i] = EEPROM.read(i);
  buffer[32] = 0; ssid = buffer;
  for (int i = 0; i < 64; ++i) buffer[i] = EEPROM.read(32 + i);
  buffer[64] = 0; pass = buffer; EEPROM.end();
  return ssid.length() && (unsigned char)ssid[0] != 255;
}

void saveCredentials(const String& ssid, const String& pass) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 32; ++i) EEPROM.write(i, i < (int)ssid.length() ? ssid[i] : 0);
  for (int i = 0; i < 64; ++i) EEPROM.write(32 + i, i < (int)pass.length() ? pass[i] : 0);
  EEPROM.commit(); EEPROM.end();
}

void setupScreen() {
  tft.fillScreen(BG); text("WiFi setup", 12, 20, 2);
  text("Connect to:", 12, 65); text(AP_SSID, 12, 90, 2, ACCENT);
  text("Password: 12345678", 12, 130);
  text("Open 192.168.4.1", 12, 165);
}

void setup() {
  tft.init(); tft.setRotation(0); tft.fillScreen(BG);
  loadBrightness();
  WiFi.persistent(false);
  String ssid, pass;
  if (loadCredentials(ssid, pass)) {
    text("Connecting WiFi...", 12, 105, 2);
    WiFi.mode(WIFI_STA); WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) delay(50);
  }
  apMode = WiFi.status() != WL_CONNECTED;
  if (apMode) { WiFi.mode(WIFI_AP); WiFi.softAP(AP_SSID, AP_PASS); setupScreen(); }
  else { WiFi.setAutoReconnect(true); render(); }
  updater.setup(&server, "/update", OTA_USERNAME, OTA_PASSWORD);
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", apMode ? WIFI_HTML : NAS_HTML); });
  server.on("/state", HTTP_GET, []() { server.send(200, "application/json", stateJson()); });
  server.on("/nas", HTTP_POST, handleNas);
  server.on("/brightness", HTTP_POST, []() {
    unsigned value;
    if (!server.hasArg("value") || !NasLogic::brightness(server.arg("value").c_str(), value)) {
      server.send(400, "application/json", "{\"error\":\"brightness_must_be_integer_0_to_100\"}"); return;
    }
    if (value != brightnessPercent) {
      applyBrightness(value); brightnessPending = true; brightnessChangedAt = millis();
    }
    server.send(200, "application/json", "{\"brightness\":" + String(brightnessPercent) + "}");
  });
  server.on("/connect", HTTP_POST, []() {
    if (!apMode) { server.send(409, "text/plain", "WiFi is already configured"); return; }
    String ssid = server.arg("ssid"), pass = server.arg("pass");
    if (!ssid.length() || ssid.length() > 32 || pass.length() > 63) { server.send(400, "text/plain", "Invalid WiFi credentials"); return; }
    saveCredentials(ssid, pass); server.send(200, "text/plain", "Saved. Rebooting; reconnect to your normal WiFi.");
    delay(500); ESP.restart();
  });
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void loop() {
  server.handleClient();
  saveBrightnessIfReady();
  if (!apMode && millis() - lastRender >= 1000) {
    static uint32_t lastPage = 0;
    if (millis() - lastPage >= 6000) { ++warningPage; lastPage = millis(); }
    render();
  }
  yield();
}
