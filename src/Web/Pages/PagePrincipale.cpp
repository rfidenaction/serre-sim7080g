#include "Web/Pages/PagePrincipale.h"
#include "Connectivity/WiFiManager.h"
#include "Storage/DataLogger.h"
#include "Connectivity/CellularManager.h"

// startTime est déclaré dans main.cpp
extern unsigned long startTime;

static String getUptimeString() {
    unsigned long secs = (millis() - startTime) / 1000;
    int days = secs / 86400; secs %= 86400;
    int hours = secs / 3600; secs %= 3600;
    int mins = secs / 60; secs %= 60;
    return String(days) + "j " + String(hours) + "h " + String(mins) + "m " + String(secs) + "s";
}

// Utilitaire pour récupérer la dernière valeur avec fallback
static float getLastValue(DataId id, float fallback = 0.0f) {
    DataRecord rec;
    if (DataLogger::getLast(id, rec)) return rec.value;
    return fallback;
}

String PagePrincipale::getHtml() {
    String staStatus = WiFiManager::isSTAEnabled()
        ? (WiFiManager::isSTAConnected() ? WiFiManager::getSTAStatus() : "Recherche MyFree2...")
        : "Désactivé";

    String apStatus = WiFiManager::getAPStatus();

    // Récupération des dernières valeurs en temps réel depuis DataLogger
    float batteryVoltage    = getLastValue(DataId::BatteryVoltage, 3.7f);
    int   batteryPercent    = (int)getLastValue(DataId::BatteryPercent, 50);
    bool  charging          = getLastValue(DataId::Charging, 0.0f) > 0.5f;
    bool  externalPower     = getLastValue(DataId::ExternalPower, 0.0f) > 0.5f;

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
      <input type="checkbox" name="state" value="1" )HTML" + String(WiFiManager::isSTAEnabled() ? "checked" : "") + R"HTML( onchange="this.form.submit()">
      <span class="slider"></span>
    </label>
    <p class="subtext">WIFI )HTML" + String(WiFiManager::isSTAEnabled() ? "ON" : "OFF") + R"HTML(</p>
  </form>
</div>

<div class="card">
  <p>ACCES LOCAL</p>
  <p class="value">)HTML" + apStatus + R"HTML(</p>
  <form action="/ap-toggle" method="post" style="margin:20px 0;">
    <label class="switch">
      <input type="checkbox" name="state" value="1" )HTML" + String(WiFiManager::isAPEnabled() ? "checked" : "") + R"HTML( onchange="this.form.submit()">
      <span class="slider"></span>
    </label>
    <p class="subtext">ACCES LOCAL )HTML" + String(WiFiManager::isAPEnabled() ? "ON" : "OFF") + R"HTML(</p>
  </form>
</div>

<div class="card">
  <p>Statut GSM</p>
  <p class="value">)HTML" + CellularManager::getStatus() + R"HTML(</p>
</div>

<div class="card">
  <p>Alim externe</p>
  <p class="value">)HTML" + String(externalPower ? "Oui" : "Non") + R"HTML(</p>
</div>

<div class="card" onclick="showGraph()">
  <p>Batterie</p>
  <p class="value">)HTML" + String(batteryVoltage, 2) + R"HTML( V ( )HTML" + String(batteryPercent) + R"HTML( % )</p>
  <p>)HTML" + String(charging ? "En charge" : "Pas en charge") + R"HTML(</p>
  <p>Cliquez pour graphique tension</p>
</div>

<div class="card">
  <p>Durée de fonctionnement</p>
  <p class="value">)HTML" + getUptimeString() + R"HTML(</p>
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

    return html;
}
