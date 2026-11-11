#include <WiFi.h>
#include <HTTPClient.h>
#include <LiquidCrystal.h>

// ── Rede usada para FALAR com o servidor (WLL-Inatel ou hotspot do celular) ──
const char* NET_SSID     = "WLL-Inatel";
const char* NET_PASSWORD = "SUA_SENHA_AQUI";

// ── Rede cujos APs são ESCANEADOS (tem que ser a mesma do treino) ────────────
const char* SCAN_SSID = "WLL-Inatel";

// ── Servidor Flask: IP do notebook (veja com ipconfig) ───────────────────────
const char* PREDICT_URL = "http://192.168.0.10:5000/predict";

// ── LCD em modo paralelo: RS, E, D4, D5, D6, D7 ──────────────────────────────
LiquidCrystal lcd(19, 23, 18, 17, 16, 15);

// ── Botão CONFIRM pausa/retoma (INPUT_PULLUP, ligar entre o pino e o GND) ────
const int CONFIRM_BTN = 27;

// ── Parâmetros ───────────────────────────────────────────────────────────────
const unsigned long SCAN_INTERVAL      = 2000;  // ms entre ciclos
const unsigned long RECONNECT_INTERVAL = 5000;
const unsigned long CONNECT_TIMEOUT    = 15000;
const float  MIN_CONFIDENCE = 60.0;             // abaixo disso mostra "incerto"
const int    VOTE_WINDOW    = 3;                // votação nas últimas N predições

// ── Estado ───────────────────────────────────────────────────────────────────
bool running = true;
String votes[VOTE_WINDOW];
int voteIdx = 0;
unsigned long lastCycle = 0, lastReconnect = 0;
int btnState = HIGH;
unsigned long btnChange = 0;

// ─────────────────────────────────────────────────────────────────────────────
void lcdLine(int row, String text) {
  while (text.length() < 16) text += ' ';
  lcd.setCursor(0, row);
  lcd.print(text.substring(0, 16));
}

bool connectWiFi() {
  lcdLine(0, "VidiWaze");
  lcdLine(1, "Conectando...");
  WiFi.begin(NET_SSID, NET_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECT_TIMEOUT) delay(300);
  bool ok = WiFi.status() == WL_CONNECTED;
  Serial.println(ok ? "[WIFI] Conectado, IP " + WiFi.localIP().toString() : "[WIFI] Falha");
  lcdLine(1, ok ? "Wi-Fi OK" : "Sem Wi-Fi");
  return ok;
}

void maybeReconnect() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastReconnect < RECONNECT_INTERVAL) return;
  lastReconnect = millis();
  WiFi.disconnect();
  connectWiFi();
}

// ── Leitura simples do JSON de resposta (sem biblioteca extra) ───────────────
// Lê só a parte antes de "top3", para pegar a posição principal.
String mainPart(const String& json) {
  int t = json.indexOf("\"top3\"");
  return t >= 0 ? json.substring(0, t) : json;
}

int valueStart(const String& json, const char* key) {
  int k = json.indexOf("\"" + String(key) + "\"");
  if (k < 0) return -1;
  int c = json.indexOf(':', k);
  if (c < 0) return -1;
  c++;
  while (c < (int)json.length() && (json[c] == ' ' || json[c] == '\n' || json[c] == '\r')) c++;
  return c;
}

String jsonString(const String& json, const char* key) {
  int s = valueStart(json, key);
  if (s < 0 || json[s] != '"') return "";
  int e = json.indexOf('"', s + 1);
  return e > s ? json.substring(s + 1, e) : "";
}

float jsonNumber(const String& json, const char* key) {
  int s = valueStart(json, key);
  if (s < 0) return -1;
  int e = s;
  while (e < (int)json.length() && json[e] != ',' && json[e] != '}' && json[e] != '\n') e++;
  return json.substring(s, e).toFloat();
}

// ── Votação: devolve a posição que aparece 2+ vezes, senão a mais recente ────
String vote(const String& latest) {
  votes[voteIdx] = latest;
  voteIdx = (voteIdx + 1) % VOTE_WINDOW;
  for (int i = 0; i < VOTE_WINDOW; i++) {
    int count = 0;
    for (int j = 0; j < VOTE_WINDOW; j++) if (votes[j].length() && votes[j] == votes[i]) count++;
    if (count >= 2) return votes[i];
  }
  return latest;
}

// ─────────────────────────────────────────────────────────────────────────────
void cycle() {
  // 1. Scan e montagem do JSON {"bssid": {"MAC": rssi, ...}}
  int n = WiFi.scanNetworks(false, true);
  String body = "{\"bssid\":{";
  int aps = 0;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != SCAN_SSID) continue;
    if (aps > 0) body += ",";
    body += "\"" + WiFi.BSSIDstr(i) + "\":" + String(WiFi.RSSI(i));
    aps++;
  }
  body += "}}";
  WiFi.scanDelete();

  if (aps == 0) {
    lcdLine(1, "Nenhum AP");
    Serial.println("[SCAN] nenhum AP de " + String(SCAN_SSID));
    return;
  }

  // 2. Envio ao servidor
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.begin(PREDICT_URL);
  http.addHeader("Content-Type", "application/json");
  unsigned long t0 = millis();
  int code = http.POST(body);
  String resp = code > 0 ? http.getString() : "";
  http.end();
  unsigned long latency = millis() - t0;

  if (code != 200) {
    lcdLine(1, "Servidor " + String(code));
    Serial.println("[PREDICT] HTTP " + String(code) + " " + resp);
    return;
  }

  // 3. Resultado
  String main = mainPart(resp);
  String pos  = jsonString(main, "posicao");
  float conf  = jsonNumber(main, "confianca");
  String shown = vote(pos);

  if (conf < MIN_CONFIDENCE) {
    lcdLine(0, "incerto");
  } else {
    lcdLine(0, shown);
  }
  lcdLine(1, String((int)conf) + "% AP=" + String(aps) + " " + String(latency) + "ms");

  Serial.println("[PREDICT] " + pos + " (" + String(conf, 1) + "%) votado=" + shown +
                 " APs=" + String(aps) + " latencia=" + String(latency) + "ms");
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(CONFIRM_BTN, INPUT_PULLUP);
  lcd.begin(16, 2);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  connectWiFi();
}

void loop() {
  // Botão: pausa / retoma
  int s = digitalRead(CONFIRM_BTN);
  if (s != btnState && millis() - btnChange > 50) {
    btnChange = millis();
    btnState = s;
    if (s == LOW) {
      running = !running;
      lcdLine(1, running ? "Rodando" : "Pausado");
    }
  }

  maybeReconnect();
  if (running && WiFi.status() == WL_CONNECTED && millis() - lastCycle >= SCAN_INTERVAL) {
    lastCycle = millis();
    cycle();
  }
}
