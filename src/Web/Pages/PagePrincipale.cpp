// Web/Pages/PagePrincipale.cpp
#include "Web/Pages/PagePrincipale.h"

#include "Storage/DataLogger.h"
#include "Config/NetworkConfig.h"

// startTime est déclaré dans main.cpp
extern unsigned long startTime;

// ─────────────────────────────────────────────
// Uptime système
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
// Accès sécurisé LastDataForWeb (sans fallback)
// ─────────────────────────────────────────────

static bool getWebData(DataId id, LastDataForWeb& out)
{
    return DataLogger::hasLastDataForWeb(id, out);
}

// ─────────────────────────────────────────────
// Formatage date UTC
// ─────────────────────────────────────────────

static String formatUtc(time_t t)
{
    struct tm* tm = localtime(&t);
    char buf[20];
    strftime(buf, sizeof(buf), "%d/%m/%y %H:%M", tm);
    return String(buf);
}

// ─────────────────────────────────────────────
// Génération HTML
// ─────────────────────────────────────────────

String PagePrincipale::getHtml()
{
    // ───────── Batterie ─────────
    LastDataForWeb battV, battPct, chargingD, extPowerD;

    bool hasBattV     = getWebData(DataId::BatteryVoltage, battV);
    bool hasBattPct   = getWebData(DataId::BatteryPercent, battPct);
    bool hasCharging  = getWebData(DataId::Charging, chargingD);
    bool hasExtPower  = getWebData(DataId::ExternalPower, extPowerD);

    // ───────── Wi-Fi ─────────
    LastDataForWeb staEnD, staConnD, apEnD, rssiD;
    bool hasStaEn   = getWebData(DataId::WifiStaEnabled, staEnD);
    bool hasStaConn = getWebData(DataId::WifiStaConnected, staConnD);
    bool hasApEn    = getWebData(DataId::WifiApEnabled, apEnD);
    bool hasRssi    = getWebData(DataId::WifiRssi, rssiD);

    bool staEnabled   = hasStaEn   && staEnD.value   > 0.5f;
    bool staConnected = hasStaConn && staConnD.value > 0.5f;
    bool apEnabled    = hasApEn    && apEnD.value    > 0.5f;
    int  wifiRssi     = hasRssi    ? (int)rssiD.value : 0;

    String staStatus =
        !hasStaEn ? "" :
        !staEnabled ? "Désactivé" :
        (staConnected && hasRssi
            ? "Connecté (" + String(wifiRssi) + " dBm)"
            : "Recherche réseau...");

    String apStatus = hasApEn ? (apEnabled ? "Actif" : "Désactivé") : "";

    // ───────── Réseau ─────────
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
.time { font-size: 1.1em; margin-top: 8px; opacity: 0.9; }
.switch { position: relative; display: inline-block; width: 90px; height: 44px; }
.switch input { opacity: 0; width: 0; height: 0; }
.slider { position: absolute; cursor: pointer; inset: 0; background-color: #ccc; transition: .4s; border-radius: 44px; }
.slider:before { position: absolute; content: ""; height: 36px; width: 36px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
input:checked + .slider { background-color: #0d47a1; }
input:checked + .slider:before { transform: translateX(46px); }
</style>

<script>
function formatAge(ms) {
  let s = Math.floor(ms / 1000);
  let h = Math.floor(s / 3600); s %= 3600;
  let m = Math.floor(s / 60);   s %= 60;
  return "Depuis " + h + "h " + m + "m " + s + "s";
}

setInterval(() => {
  document.querySelectorAll('[data-age-ms]').forEach(e => {
    let ms = parseInt(e.dataset.ageMs);
    ms += 1000;
    e.dataset.ageMs = ms;
    e.textContent = formatAge(ms);
  });
}, 1000);
</script>
</head>
<body>

<h1>Serre de Marie-Pierre</h1>
)HTML";

    // ───────── Alimentation externe ─────────
    if (hasExtPower) {
        html += "<div class=\"card\"><p>Alim externe</p>";
        html += "<p class=\"value\">" + String(extPowerD.value > 0.5f ? "Oui" : "Non") + "</p>";
        if (extPowerD.utc_valid) {
            html += "<p class=\"time\">" + formatUtc(extPowerD.t_utc) + "</p>";
        } else {
            unsigned long age = millis() - extPowerD.t_rel_ms;
            html += "<p class=\"time\" data-age-ms=\"" + String(age) + "\">Depuis 0s</p>";
        }
        html += "</div>";
    }

    // ───────── Batterie ─────────
    if (hasBattV && hasBattPct && hasCharging) {
        html += "<div class=\"card\"><p>Batterie</p>";
        html += "<p class=\"value\">" + String(battV.value, 2) + " V (" + String((int)battPct.value) + " %)</p>";
        html += "<p>" + String(chargingD.value > 0.5f ? "En charge" : "Pas en charge") + "</p>";
        if (battV.utc_valid) {
            html += "<p class=\"time\">" + formatUtc(battV.t_utc) + "</p>";
        } else {
            unsigned long age = millis() - battV.t_rel_ms;
            html += "<p class=\"time\" data-age-ms=\"" + String(age) + "\">Depuis 0s</p>";
        }
        html += "</div>";
    }

    // ───────── Uptime ─────────
    html += "<div class=\"card\"><p>Durée de fonctionnement</p>";
    html += "<p class=\"value\">" + getUptimeString() + "</p></div>";

    html += R"HTML(
<form action="/reset" method="post">
  <button type="submit">Redémarrer la carte</button>
</form>

</body>
</html>
)HTML";

    return html;
}
