// Web/Pages/PagePrincipale.cpp
#include "Web/Pages/PagePrincipale.h"

#include "Storage/DataLogger.h"
#include "Config/NetworkConfig.h"

#include <time.h>

// startTime est déclaré dans main.cpp
extern unsigned long startTime;

// ─────────────────────────────────────────────
// Helpers sécurisés pour extraire les valeurs du variant
// ─────────────────────────────────────────────

// Extrait un float du variant de manière sécurisée (pas de crash)
static float getFloat(const LastDataForWeb& d, float defaultValue = 0.0f)
{
    if (std::holds_alternative<float>(d.value)) {
        return std::get<float>(d.value);
    }
    // Erreur : le variant contient un String, pas un float
    Serial.println("[PagePrincipale] WARNING: Tentative d'extraire float depuis un String!");
    return defaultValue;
}

// Extrait un String du variant de manière sécurisée (pas de crash)
static String getString(const LastDataForWeb& d, const String& defaultValue = "")
{
    if (std::holds_alternative<String>(d.value)) {
        return std::get<String>(d.value);
    }
    // Erreur : le variant contient un float, pas un String
    Serial.println("[PagePrincipale] WARNING: Tentative d'extraire String depuis un float!");
    return defaultValue;
}

// ─────────────────────────────────────────────
// Uptime
// ─────────────────────────────────────────────

String PagePrincipale::getUptimeString()
{
    unsigned long secs = (millis() - startTime) / 1000;
    int days = secs / 86400; secs %= 86400;
    int hours = secs / 3600; secs %= 3600;
    int mins = secs / 60; secs %= 60;

    return String(days) + "j " +
           String(hours) + "h " +
           String(mins) + "m " +
           String(secs) + "s";
}

// ─────────────────────────────────────────────
// Helpers temps (UI uniquement)
// ─────────────────────────────────────────────

static String formatUtc(time_t t)
{
    struct tm* tm = localtime(&t);
    char buf[20];
    strftime(buf, sizeof(buf), "%d/%m/%y %H:%M", tm);
    return String(buf);
}

static String formatSince(uint32_t ageMs)
{
    uint32_t s = ageMs / 1000;
    uint32_t m = s / 60; s %= 60;
    uint32_t h = m / 60; m %= 60;

    String out = "Depuis ";
    if (h) out += String(h) + "h ";
    if (m) out += String(m) + "m ";
    out += String(s) + "s";
    return out;
}

static String timeHtml(const LastDataForWeb& d)
{
    if (d.utc_valid) {
        return formatUtc(d.t_utc);
    }

    uint32_t ageMs = millis() - d.t_rel_ms;
    return "<span class=\"age\" data-age-ms=\"" +
           String(ageMs) + "\">" +
           formatSince(ageMs) +
           "</span>";
}

// ─────────────────────────────────────────────
// Génération HTML
// ─────────────────────────────────────────────

String PagePrincipale::getHtml()
{
    LastDataForWeb d;

    // ───────── Batterie ─────────
    String batteryLine;
    String batteryTime;

    float voltage = 0.0f;
    int percent   = -1;

    if (DataLogger::hasLastDataForWeb(DataId::BatteryVoltage, d)) {
        voltage = getFloat(d);  // Safe : retourne 0.0f si erreur
        batteryTime = timeHtml(d);
    }

    if (DataLogger::hasLastDataForWeb(DataId::BatteryPercent, d)) {
        percent = (int)getFloat(d);  // Safe : retourne 0 si erreur
    }

    batteryLine = String(voltage, 2) + " V";
    if (percent >= 0) {
        batteryLine += " (" + String(percent) + " %)";
    }

    String charging;
    if (DataLogger::hasLastDataForWeb(DataId::Charging, d)) {
        charging = getFloat(d) > 0.5f ? "En charge" : "Pas en charge";  // Safe
    }

    // ───────── Alimentation externe ─────────
    String externalPower;
    String externalPowerTime;
    if (DataLogger::hasLastDataForWeb(DataId::ExternalPower, d)) {
        externalPower = getFloat(d) > 0.5f ? "Oui" : "Non";  // Safe
        externalPowerTime = timeHtml(d);
    }

    // ───────── Wi-Fi ─────────
    bool staEnabled   = false;
    bool staConnected = false;
    bool apEnabled    = false;

    String wifiTime;

    if (DataLogger::hasLastDataForWeb(DataId::WifiStaEnabled, d)) {
        staEnabled = getFloat(d) > 0.5f;  // Safe
        wifiTime = timeHtml(d);
    }

    if (DataLogger::hasLastDataForWeb(DataId::WifiStaConnected, d)) {
        staConnected = getFloat(d) > 0.5f;  // Safe
        wifiTime = timeHtml(d);
    }

    int wifiRssi = 0;
    bool hasRssi = DataLogger::hasLastDataForWeb(DataId::WifiRssi, d);
    if (hasRssi) wifiRssi = (int)getFloat(d);  // Safe

    if (DataLogger::hasLastDataForWeb(DataId::WifiApEnabled, d)) {
        apEnabled = getFloat(d) > 0.5f;  // Safe
        wifiTime = timeHtml(d);
    }

    String staStatus =
        !staEnabled ? "Désactivé" :
        (staConnected && hasRssi ? "Connecté (" + String(wifiRssi) + " dBm)" :
         staConnected ? "Connecté" :
                        "Recherche réseau...");

    String apStatus = apEnabled ? "Actif" : "Désactivé";

    // ───────── Config réseau ─────────
    String staSsid = WIFI_STA_SSID;
    String staIp   = WIFI_STA_IP.toString();
    String apIp    = WIFI_AP_IP.toString();

    // ───────── HTML ─────────
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
.subtext { font-size: 1.2em; margin-top: 15px; }
small { font-size: 0.8em; }
.switch { position: relative; display: inline-block; width: 90px; height: 44px; }
.switch input { opacity: 0; width: 0; height: 0; }
.slider { position: absolute; cursor: pointer; inset: 0; background-color: #ccc; transition: .4s; border-radius: 44px; }
.slider:before { position: absolute; content: ""; height: 36px; width: 36px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
input:checked + .slider { background-color: #0d47a1; }
input:checked + .slider:before { transform: translateX(46px); }
input:disabled + .slider { opacity: 0.5; cursor: default; }
</style>

<script>
function toggleSta(cb) {
  const params = new URLSearchParams();
  if (cb.checked) {
    params.append('state', '1');
  }
  fetch('/wifi-toggle', { method: 'POST', body: params });
}

function toggleAp(cb) {
  if (!cb.checked) {
    fetch('/ap-toggle', { method: 'POST', body: new URLSearchParams() });
  }
}

setInterval(() => {
  document.querySelectorAll('.age').forEach(e => {
    let ms = parseInt(e.dataset.ageMs);
    ms += 1000;
    e.dataset.ageMs = ms;

    let s = Math.floor(ms / 1000);
    let m = Math.floor(s / 60); s %= 60;
    let h = Math.floor(m / 60); m %= 60;

    e.textContent = 'Depuis ' +
      (h ? h + 'h ' : '') +
      (m ? m + 'm ' : '') +
      s + 's';
  });
}, 1000);

// rafraîchissement périodique
setInterval(() => {
  location.reload();
}, 30000);
</script>
</head>
<body>

<h1>Serre de Marie-Pierre</h1>

<div class="card">
  <p>WIFI</p>
  <p class="value">)HTML" + staStatus + R"HTML(</p>
  <p class="subtext">SSID : )HTML" + staSsid + R"HTML(<br>IP : )HTML" + staIp + R"HTML(</p>
  <p><small>)HTML" + wifiTime + R"HTML(</small></p>
  <label class="switch">
    <input type="checkbox"
           )HTML" + String(staEnabled ? "checked" : "") + R"HTML(
           onchange="toggleSta(this)">
    <span class="slider"></span>
  </label>
</div>

<div class="card">
  <p>ACCES LOCAL</p>
  <p class="value">)HTML" + apStatus + R"HTML(</p>
  <p class="subtext">IP : )HTML" + apIp + R"HTML(</p>
  <p><small>)HTML" + wifiTime + R"HTML(</small></p>
  <label class="switch">
    <input type="checkbox"
           )HTML" + String(apEnabled ? "checked" : "") + R"HTML(
           )HTML" + String(apEnabled ? "" : "disabled") + R"HTML(
           onchange="toggleAp(this)">
    <span class="slider"></span>
  </label>
</div>

<div class="card">
  <p>Statut GSM</p>
  <p class="value">GSM non actif</p>
</div>

<div class="card">
  <p>Alim externe</p>
  <p class="value">)HTML" + externalPower + R"HTML(</p>
  <p><small>)HTML" + externalPowerTime + R"HTML(</small></p>
</div>

<div class="card">
  <p>Batterie</p>
  <p class="value">)HTML" + batteryLine + R"HTML(</p>
  <p><small>)HTML" + batteryTime + R"HTML(</small></p>
  <p>)HTML" + charging + R"HTML(</p>
</div>

<div class="card">
  <p>Durée de fonctionnement</p>
  <p class="value">)HTML" + getUptimeString() + R"HTML(</p>
</div>

<div class="card" style="margin-top: 40px;">
  <a href="/logs" style="color: white; text-decoration: none; display: block;">
    <p style="font-size: 1.2em;">🗂️ Gestion des Logs</p>
    <p style="font-size: 0.9em;">Télécharger ou supprimer les données</p>
  </a>
</div>

</body>
</html>
)HTML";

    return html;
}
