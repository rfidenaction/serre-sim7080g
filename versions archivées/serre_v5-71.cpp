// Version 5.71 - Correction serveur AP
#define XPOWERS_CHIP_AXP2101
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <XPowersLib.h>
#include <Preferences.h>

XPowersAXP2101 PMU;

// --- Configuration WiFi ---
const char* sta_ssid = "MyFree2";
const char* sta_password = "1234567890";

const char* ap_ssid = "Serre_de_Marie-Pierre";
const char* ap_password = "1234567890";

IPAddress apIP(192,168,4,1);
IPAddress apGW(192,168,4,1);
IPAddress apSN(255,255,255,0);

IPAddress staIP(192,168,1,150);
IPAddress staGW(192,168,1,1);
IPAddress staSN(255,255,255,0);
IPAddress staDNS(192,168,1,1);

// --- État WiFi ---
bool wifiStaEnabled = true;
bool staConnecting = false;
bool staConnected = false;

bool apEnabled = true;  // Runtime uniquement, non persistant

unsigned long lastStaAttempt = 0;
const unsigned long STA_RETRY_INTERVAL = 5000;

// --- Serveur et préférences ---
AsyncWebServer server(80);
Preferences preferences;

// --- PMU et batterie (inchangé) ---
unsigned long startTime = millis();
String gsmStatus = "Modem éteint";
String externalPower = "Non";
float batteryVoltage = 0.0;
int batteryPercent = 0;
bool isCharging = false;
bool pmuDetected = false;

// --- Historique batterie (inchangé) ---
const int MAX_POINTS = 5000;
float voltageHistory[MAX_POINTS];
int historyCount = 0;
const char* historyFile = "/history.dat";

// --- Fonctions utilitaires (inchangées) ---
String getUptime() {
  unsigned long secs = (millis() - startTime) / 1000;
  int days = secs / 86400; secs %= 86400;
  int hours = secs / 3600; secs %= 3600;
  int mins = secs / 60; secs %= 60;
  return String(days) + "j " + String(hours) + "h " + String(mins) + "m " + String(secs) + "s";
}

void loadHistory() {
  if (!SPIFFS.exists(historyFile)) return;
  File file = SPIFFS.open(historyFile, FILE_READ);
  if (!file) return;
  int bytes = file.readBytes((char*)voltageHistory, min(file.size(), (size_t)(MAX_POINTS * sizeof(float))));
  historyCount = bytes / sizeof(float);
  file.close();
}

void saveHistory() {
  File file = SPIFFS.open(historyFile, FILE_WRITE);
  if (!file) return;
  file.write((uint8_t*)voltageHistory, historyCount * sizeof(float));
  file.close();
}

void addVoltage(float v) {
  if (historyCount < MAX_POINTS) {
    voltageHistory[historyCount++] = v;
  } else {
    memmove(voltageHistory, voltageHistory + 1, (MAX_POINTS - 1) * sizeof(float));
    voltageHistory[MAX_POINTS - 1] = v;
  }
}

// --- Démarrage WiFi au boot (ton placement exact) ---
void startWiFi() {
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  bool apConfigResult = WiFi.softAPConfig(apIP, apGW, apSN);
  if (!apConfigResult) Serial.println("Erreur softAPConfig");
  delay(50);

  bool apResult = WiFi.softAP(ap_ssid, ap_password);
  Serial.println(apResult ? "AP démarrée" : "Erreur AP");
  Serial.println("IP AP: " + WiFi.softAPIP().toString());

  apEnabled = true;  // Forcé à true à chaque boot (non persistant)

  if (wifiStaEnabled) {
    WiFi.begin(sta_ssid, sta_password);
    staConnecting = true;
    lastStaAttempt = millis();
  }
}

// --- Gestion STA (inchangée) ---
void handleSta() {
  if (!wifiStaEnabled) {
    if (staConnected || staConnecting) WiFi.disconnect();
    staConnected = false;
    staConnecting = false;
    return;
  }

  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (!staConnected) {
      WiFi.config(staIP, staGW, staSN, staDNS);
      staConnected = true;
      staConnecting = false;
    }
  } else {
    staConnected = false;
    if (millis() - lastStaAttempt > STA_RETRY_INTERVAL) {
      WiFi.disconnect();
      WiFi.begin(sta_ssid, sta_password);
      staConnecting = true;
      lastStaAttempt = millis();
    }
  }
}

// --- Extinction AP à chaud ---
void disableAP() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apEnabled = false;
  Serial.println("AP éteint à chaud");
}

void setup() {
  Serial.begin(115200);

  if (!SPIFFS.begin(true)) {
    Serial.println("Erreur SPIFFS");
    return;
  }
  loadHistory();

  if (PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, 15, 7)) {
    PMU.disableTSPinMeasure();
    pmuDetected = true;
  }

  preferences.begin("wifi", false);
  wifiStaEnabled = preferences.getBool("sta", true);
  preferences.end();

  startWiFi();

  gsmStatus = "Non initialisé";

  // --- Page web ---
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (pmuDetected) {
      batteryVoltage = PMU.getBattVoltage() / 1000.0;
      batteryPercent = constrain((batteryVoltage - 3.0) / (4.2 - 3.0) * 100, 0, 100);
      isCharging = PMU.isCharging();
      externalPower = PMU.isVbusGood() ? "Oui" : "Non";
      addVoltage(batteryVoltage);
    }

    String staStatus = wifiStaEnabled ?
      (staConnected ? "Connecté à MyFree2 192.168.1.150 (" + String(WiFi.RSSI()) + " dBm)" : "Recherche MyFree2...") :
      "Désactivé";

    String apStatus = "Serre_de_Marie-Pierre  192.168.4.1";

    String html = R"HTML(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Serre de Marie-Pierre</title>
<style>
body { font-family: Arial; background: #1976d2; color: white; text-align: center; margin: 0; padding: 20px; }
h1 { background: #0d47a1; padding: 20px; border-radius: 10px; }
.card { background: rgba(255,255,255,0.2); margin: 20px auto; max-width: 600px; padding: 20px; border-radius: 15px; }
.value { font-size: 1.8em; font-weight: bold; }
.switch { position: relative; display: inline-block; width: 90px; height: 44px; }
.switch input { opacity: 0; width: 0; height: 0; }
.slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 44px; }
.slider:before { position: absolute; content: ""; height: 36px; width: 36px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
input:checked + .slider { background-color: #0d47a1; }
input:checked + .slider:before { transform: translateX(46px); }
.subtext { font-size: 1.2em; margin-top: 15px; }
#graphOverlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.8); z-index: 10; padding: 20px; }
canvas { background: white; border-radius: 15px; max-width: 90%; }
.version { margin-top: 10px; font-size: 1.2em; color: #0d47a1; font-weight: bold; }
</style>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<script>
function showGraph() {
  document.getElementById('graphOverlay').style.display = 'block';
  fetch('/graphdata')
    .then(r => r.text())
    .then(data => {
      let points = data.split(',').map(parseFloat);
      if (points.length === 1 && isNaN(points[0])) points = [3.0];
      const count = points.length;
      const labels = points.map((_, i) => Math.round((count - 1 - i) * 30 / 60) + ' min');
      const ctx = document.getElementById('chartCanvas').getContext('2d');
      new Chart(ctx, {
        type: 'line',
        data: { labels: labels, datasets: [{ label: 'Tension Batterie (V)', data: points, borderColor: '#0d47a1', backgroundColor: 'rgba(13,71,161,0.2)', fill: true }] },
        options: { responsive: true, scales: { y: { min: 3.0, max: 4.3 } } }
      });
    });
}
function hideGraph() { document.getElementById('graphOverlay').style.display = 'none'; }
setInterval(() => location.reload(), 10000);
</script>
</head>
<body>
<h1>Serre de Marie-Pierre</h1>

<div class="card">
  <p>WIFI</p>
  <p class="value">)HTML" + staStatus + R"HTML(</p>
  <form action="/wifi-toggle" method="post" style="margin:20px 0;">
    <label class="switch">
      <input type="checkbox" name="state" value="1" )HTML" + String(wifiStaEnabled ? "checked" : "") + R"HTML( onchange="this.form.submit()">
      <span class="slider"></span>
    </label>
    <p class="subtext">WIFI )HTML" + String(wifiStaEnabled ? "ON" : "OFF") + R"HTML(</p>
  </form>
</div>

<div class="card">
  <p>ACCES LOCAL</p>
  <p class="value">)HTML" + apStatus + R"HTML(</p>
  <form action="/ap-toggle" method="post" style="margin:20px 0;">
    <label class="switch">
      <input type="checkbox" name="state" value="1" )HTML" + String(apEnabled ? "checked" : "") + R"HTML( onchange="this.form.submit()">
      <span class="slider"></span>
    </label>
    <p class="subtext">ACCES LOCAL )HTML" + String(apEnabled ? "ON" : "OFF") + R"HTML(</p>
  </form>
</div>

<div class="card">
  <p>Statut GSM</p>
  <p class="value">)HTML" + gsmStatus + R"HTML(</p>
</div>

<div class="card">
  <p>Alim externe</p>
  <p class="value">)HTML" + externalPower + R"HTML(</p>
</div>

<div class="card" onclick="showGraph()">
  <p>Batterie</p>
  <p class="value">)HTML" + String(batteryVoltage,2) + R"HTML( V ( )HTML" + String(batteryPercent) + R"HTML( % )</p>
  <p>)HTML" + (isCharging ? "En charge" : "Pas en charge") + R"HTML(</p>
  <p>Cliquez pour graphique tension</p>
</div>

<div class="card">
  <p>Durée de fonctionnement</p>
  <p class="value">)HTML" + getUptime() + R"HTML(</p>
</div>

<form action="/reset" method="post">
  <button type="submit">Redémarrer la carte</button>
</form>

<p class="version">Version 5.71</p>

<div id="graphOverlay">
  <canvas id="chartCanvas"></canvas><br>
  <button onclick="hideGraph()">Fermer</button>
</div>

</body>
</html>
)HTML";

    request->send(200, "text/html", html);
  });

  // Routes
  server.on("/wifi-toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool newState = request->hasParam("state", true);
    preferences.begin("wifi", false);
    preferences.putBool("sta", newState);
    preferences.end();
    request->redirect("/");
    delay(1000);
    ESP.restart();
  });

  server.on("/ap-toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool wantOn = request->hasParam("state", true);

    if (wantOn) {
      request->redirect("/");
      return;
    }

    disableAP();
    request->redirect("/");
  });

  server.on("/graphdata", HTTP_GET, [](AsyncWebServerRequest *request) {
    String data;
    for (int i = 0; i < historyCount; i++) {
      data += String(voltageHistory[i], 2);
      if (i < historyCount - 1) data += ",";
    }
    request->send(200, "text/plain", data);
  });

  server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Redémarrage...");
    delay(1000);
    ESP.restart();
  });

  // --- Déplacement du server.begin() ici, APRÈS toutes les routes ---
  server.begin();

  Serial.println("Serveur démarré - Version 5.71");
}

void loop() {
  handleSta();

  static unsigned long lastSave = 0;
  if (millis() - lastSave > 300000) {
    saveHistory();
    lastSave = millis();
  }

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 30000 && pmuDetected) {
    addVoltage(PMU.getBattVoltage() / 1000.0);
    lastUpdate = millis();
  }
}