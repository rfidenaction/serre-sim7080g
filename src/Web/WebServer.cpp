// Web/WebServer.cpp
#include "Web/WebServer.h"

#include "Web/Pages/PagePrincipale.h"
#include "Web/Pages/PageLogs.h"
#include "Connectivity/WiFiManager.h"
#include "Connectivity/CellularManager.h"
#include "Storage/DataLogger.h"

#include <SPIFFS.h>

AsyncWebServer WebServer::server(80);

void WebServer::init()
{
    // Configuration des routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/wifi-toggle", HTTP_POST, handleWifiToggle);
    server.on("/ap-toggle", HTTP_POST, handleApToggle);
    server.on("/graphdata", HTTP_GET, handleGraphData);
    server.on("/reset", HTTP_POST, handleReset);
    
    // Routes de gestion des logs
    server.on("/logs", HTTP_GET, handleLogs);
    server.on("/logs/download", HTTP_GET, handleLogsDownload);
    server.on("/logs/clear", HTTP_POST, handleLogsClear);

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

    // Appliquer et PERSISTER l'état désiré
    WiFiManager::setSTAEnabled(newState);

    // Temps volontairement large pour l'écriture flash
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

// ─────────────────────────────────────────────
// Gestion des logs
// ─────────────────────────────────────────────

void WebServer::handleLogs(AsyncWebServerRequest *request)
{
    // Vérifier si le GSM est actif
    bool gsmActive = CellularManager::isReady();
    
    // Récupérer les statistiques du fichier de logs
    LogFileStats stats = DataLogger::getLogFileStats();
    
    // Générer la page avec ou sans avertissement + stats
    String html = PageLogs::getHtml(gsmActive, stats);
    request->send(200, "text/html", html);
}

void WebServer::handleLogsDownload(AsyncWebServerRequest *request)
{
    // Vérification : GSM ne doit PAS être actif
    if (CellularManager::isReady()) {
        request->send(403, "text/plain", 
            "Erreur : GSM actif. Désactivez le GSM avant de télécharger les logs.");
        Serial.println("[WebServer] Tentative de téléchargement logs avec GSM actif - BLOQUÉ");
        return;
    }
    
    // Vérifier que le fichier existe
    if (!SPIFFS.exists("/datalog.csv")) {
        request->send(404, "text/plain", "Aucune donnée disponible");
        Serial.println("[WebServer] Téléchargement logs demandé mais fichier inexistant");
        return;
    }
    
    // Envoyer le fichier directement (pas de chargement en RAM)
    // Le paramètre 'true' force le téléchargement (Content-Disposition: attachment)
    request->send(SPIFFS, "/datalog.csv", "text/csv", true);
    Serial.println("[WebServer] Téléchargement logs démarré");
}

void WebServer::handleLogsClear(AsyncWebServerRequest *request)
{
    // Vérification : GSM ne doit PAS être actif
    if (CellularManager::isReady()) {
        request->send(403, "text/plain", 
            "Erreur : GSM actif. Désactivez le GSM avant de supprimer les logs.");
        Serial.println("[WebServer] Tentative de suppression logs avec GSM actif - BLOQUÉ");
        return;
    }
    
    // Supprimer l'historique
    DataLogger::clearHistory();
    
    // Réponse de succès
    request->send(200, "text/plain", "Historique supprimé avec succès");
    Serial.println("[WebServer] Logs supprimés par l'utilisateur");
}