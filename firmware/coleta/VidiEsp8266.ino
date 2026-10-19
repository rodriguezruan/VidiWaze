#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>

const char* ssid     = "WLL-Inatel";
const char* password = "inatelsemfio";
const char* googleScriptUrl = "https://script.google.com/macros/s/AKfycbyxF1hc8aXIXojRj49McqN21UyscO-y_8C1na94D8BhOUTSYUoy8TubGdj1meEisce1YA/exec";

// ── Botoes (INPUT_PULLUP: conecte entre o pino e GND) ────────────────────────
const int NEXT_BTN    = 14;
const int CONFIRM_BTN = 27;

// ── Lista de pontos ───────────────────────────────────────────────────────────
const char* POINTS[] = {
  "eHealth_p01",
  "eHealth_p02",
  "eHealth_p03",
};
const int NUM_POINTS = sizeof(POINTS) / sizeof(POINTS[0]);

// ── Estado ────────────────────────────────────────────────────────────────────
int  currentPointIndex = 0;
char currentLabel[64];
bool collectingEnabled = false;

unsigned long lastNextPress    = 0;
unsigned long lastConfirmPress = 0;
const unsigned long DEBOUNCE_MS = 300;

// ── Configuracoes ─────────────────────────────────────────────────────────────
const unsigned long CONNECT_TIMEOUT    = 15000;
const unsigned long RECONNECT_INTERVAL = 5000;
const unsigned long SCAN_LOG_INTERVAL  = 5000;

unsigned long lastReconnectAttempt = 0;
unsigned long lastScanLog          = 0;
unsigned long sampleId             = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Wi-Fi
// ─────────────────────────────────────────────────────────────────────────────
bool connectWiFi() {
  Serial.print("Conectando");
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECT_TIMEOUT) {
    delay(300);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado! IP: " + WiFi.localIP().toString());
    return true;
  }
  Serial.println("\nFalha ao conectar.");
  return false;
}

void maybeReconnect() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastReconnectAttempt < RECONNECT_INTERVAL) return;
  lastReconnectAttempt = now;
  Serial.println("Reconectando...");
  WiFi.disconnect();
  connectWiFi();
}

// ─────────────────────────────────────────────────────────────────────────────
// Gerenciamento de label
// ─────────────────────────────────────────────────────────────────────────────
void applyLabel(const char* label) {
  strncpy(currentLabel, label, sizeof(currentLabel) - 1);
  currentLabel[sizeof(currentLabel) - 1] = '\0';
  collectingEnabled = false;
  Serial.println("[LABEL] " + String(currentLabel) + " - envie OK para iniciar coleta");
}

void confirmAndStartCollecting() {
  collectingEnabled = true;
  Serial.println("[COLETANDO] " + String(currentLabel));
}

// ─────────────────────────────────────────────────────────────────────────────
// Botoes
// ─────────────────────────────────────────────────────────────────────────────
void handleButtons() {
  unsigned long now = millis();

  if (digitalRead(NEXT_BTN) == LOW && now - lastNextPress > DEBOUNCE_MS) {
    lastNextPress = now;
    currentPointIndex = (currentPointIndex + 1) % NUM_POINTS;
    applyLabel(POINTS[currentPointIndex]);
  }

  if (digitalRead(CONFIRM_BTN) == LOW && now - lastConfirmPress > DEBOUNCE_MS) {
    lastConfirmPress = now;
    confirmAndStartCollecting();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Comandos seriais
// Comandos: LABEL <nome> | NEXT | OK | STATUS
// ─────────────────────────────────────────────────────────────────────────────
void handleSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd.startsWith("LABEL ")) {
    String newLabel = cmd.substring(6);
    newLabel.trim();
    if (newLabel.length() > 0) applyLabel(newLabel.c_str());
    else Serial.println("Uso: LABEL <nome>");

  } else if (cmd == "NEXT") {
    currentPointIndex = (currentPointIndex + 1) % NUM_POINTS;
    applyLabel(POINTS[currentPointIndex]);

  } else if (cmd == "OK") {
    confirmAndStartCollecting();

  } else if (cmd == "STATUS") {
    Serial.println("[STATUS] Label: " + String(currentLabel));
    Serial.println("[STATUS] Coletando: " + String(collectingEnabled ? "SIM" : "NAO"));
    Serial.println("[STATUS] Sample ID: " + String(sampleId));

  } else if (cmd.length() > 0) {
    Serial.println("Comandos: LABEL <nome> | NEXT | OK | STATUS");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Google Sheets
// ─────────────────────────────────────────────────────────────────────────────
String csvLineToJsonArray(const String& csvLine) {
  String json = "[";
  String line = csvLine;
  int pos;
  bool first = true;
  while ((pos = line.indexOf(',')) != -1) {
    String val = line.substring(0, pos);
    if (!first) json += ",";
    json += "\"" + val + "\"";
    first = false;
    line = line.substring(pos + 1);
  }
  if (!first) json += ",";
  json += "\"" + line + "\"";
  json += "]";
  return json;
}

void sendRowsToGoogleSheets(const std::vector<String>& rows) {
  if (rows.empty() || WiFi.status() != WL_CONNECTED) return;

  String jsonPayload = "{\"data\":[";
  for (size_t i = 0; i < rows.size(); i++) {
    if (i > 0) jsonPayload += ",";
    jsonPayload += csvLineToJsonArray(rows[i]);
  }
  jsonPayload += "]}";

  // No ESP32 o HTTPClient nao precisa de WiFiClientSecure separado
  // para HTTPS — ele resolve internamente com o stack TLS embutido.
  HTTPClient http;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.begin(googleScriptUrl);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(jsonPayload);
  Serial.println("[SHEETS] HTTP " + String(code));
  http.end();
}

// ─────────────────────────────────────────────────────────────────────────────
// Scan e log
// ─────────────────────────────────────────────────────────────────────────────
void scanAndLogAllAPs() {
  if (!collectingEnabled) return;
  if (WiFi.status() != WL_CONNECTED) return;

  int n = WiFi.scanNetworks(false, true);

  std::vector<String> rowsForSheets;
  int found = 0;

  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != ssid) continue;
    found++;
    int rssi    = WiFi.RSSI(i);
    String line = String(currentLabel) + "," + ssid         + "," +
                  WiFi.BSSIDstr(i)     + "," + String(rssi) + "," +
                  String(sampleId);
    Serial.println(line);
    rowsForSheets.push_back(line);
  }

  Serial.println("[SCAN] APs encontrados com SSID alvo: " + String(found));
  sendRowsToGoogleSheets(rowsForSheets);
  sampleId++;
  WiFi.scanDelete();
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(NEXT_BTN,    INPUT_PULLUP);
  pinMode(CONFIRM_BTN, INPUT_PULLUP);

  applyLabel(POINTS[0]);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  connectWiFi();

  Serial.println("Pronto. Comandos: LABEL <nome> | NEXT | OK | STATUS");
}

void loop() {
  handleButtons();
  handleSerialCommands();
  maybeReconnect();

  unsigned long now = millis();
  if (now - lastScanLog >= SCAN_LOG_INTERVAL) {
    lastScanLog = now;
    scanAndLogAllAPs();
  }
}
