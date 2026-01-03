// Web/WebServer.cpp
#include "Web/WebServer.h"
#include "Web/Pages/PagePrincipale.h"
#include "Connectivity/WiFiManager.h"
#include "Core/PowerManager.h"

AsyncWebServer WebServer::server(80);

void WebServer::init() {
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

void WebServer::handleRoot(AsyncWebServerRequest *request) {
    // Rafraîchissement des données batterie (lecture PMU + ajout à l'historique)
    PowerManager::update();

    // Génération de la page principale
    String html = PagePrincipale::getHtml();

    // Envoi de la page au client
    request->send(200, "text/html", html);
}

void WebServer::handleWifiToggle(AsyncWebServerRequest *request) {
    bool newState = request->hasParam("state", true);
    WiFiManager::setSTAEnabled(newState);
    request->redirect("/");
    delay(1000);
    ESP.restart();
}

void WebServer::handleApToggle(AsyncWebServerRequest *request) {
    bool wantOn = request->hasParam("state", true);
    if (!wantOn) {
        WiFiManager::disableAP();
    }
    request->redirect("/");
}

void WebServer::handleGraphData(AsyncWebServerRequest *request) {
    // Envoi des données d'historique de tension au format CSV pour Chart.js
    String data = PowerManager::getVoltageHistoryCsv();
    request->send(200, "text/plain", data);
}

void WebServer::handleReset(AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Redémarrage...");
    delay(1000);
    ESP.restart();
}