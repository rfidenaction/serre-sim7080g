// Web/WebServer.cpp
#include "Web/WebServer.h"

#include "Web/Pages/PagePrincipale.h"
#include "Connectivity/WiFiManager.h"
#include "Storage/DataLogger.h"

AsyncWebServer WebServer::server(80);

void WebServer::init()
{
    // Configuration des routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/wifi-toggle", HTTP_POST, handleWifiToggle);
    server.on("/ap-toggle", HTTP_POST, handleApToggle);
    server.on("/graphdata", HTTP_GET, handleGraphData);
    server.on("/reset", HTTP_POST, handleReset);

    // Démarrage du serveur asynchrone
    server.begin();
    Serial.println("Serveur web démarré");
}

// ─────────────────────────────────────────────
// Page principale
// ─────────────────────────────────────────────

void WebServer::handleRoot(AsyncWebServerRequest *request)
{
    // IMPORTANT :
    // Aucune mise à jour de données ici.
    // Toutes les données (batterie, wifi) sont mises à jour
    // périodiquement par TaskManager et stockées dans DataLogger.

    String html = PagePrincipale::getHtml();
    request->send(200, "text/html", html);
}

// ─────────────────────────────────────────────
// Commandes Wi-Fi
// ─────────────────────────────────────────────

void WebServer::handleWifiToggle(AsyncWebServerRequest *request)
{
    // CONTRAT HISTORIQUE :
    // state ABSENT  => STA OFF
    // state PRÉSENT => STA ON
    bool newState = request->hasParam("state", true);

    // Réponse immédiate (endpoint action-only, UI AJAX)
    request->send(204);

    // Appliquer et PERSISTER l’état désiré
    WiFiManager::setSTAEnabled(newState);

    // Temps volontairement large pour l’écriture flash
    // (opération rare, critique, non temps réel)
    delay(1000);

    // Reboot
    ESP.restart();
}

void WebServer::handleApToggle(AsyncWebServerRequest *request)
{
    bool wantOn = request->hasParam("state", true);

    // Réponse immédiate (pas de navigation)
    request->send(204);

    // Désactivation AP temporaire uniquement
    if (!wantOn) {
        WiFiManager::disableAP();
    }
}

// ─────────────────────────────────────────────
// Graphique batterie (FLASH via DataLogger)
// ─────────────────────────────────────────────

void WebServer::handleGraphData(AsyncWebServerRequest *request)
{
    // Historique tension batterie depuis DataLogger (FLASH)
    // Utilisation exceptionnelle, déclenchée par l'utilisateur
    String csv = DataLogger::getGraphCsv(DataId::BatteryVoltage, 30);
    request->send(200, "text/plain", csv);
}

// ─────────────────────────────────────────────
// Reset système
// ─────────────────────────────────────────────

void WebServer::handleReset(AsyncWebServerRequest *request)
{
    request->send(200, "text/plain", "Redémarrage...");
    delay(300);
    ESP.restart();
}
