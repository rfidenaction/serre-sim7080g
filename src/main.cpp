// main.cpp
// Point d’entrée principal du système
// Rôle : orchestration globale, aucune logique métier

#include <Arduino.h>
#include <SPIFFS.h>

#include "Config.h"

#include "Connectivity/WiFiManager.h"
#include "Connectivity/CellularManager.h"

#include "Core/PowerManager.h"
#include "Core/TaskManager.h"

#include "Sensors/DataAcquisition.h"

#include "Storage/FileSystem.h"
#include "Storage/DataLogger.h"

#include "Web/WebServer.h"
#include "Utils/Logger.h"

// -----------------------------------------------------------------------------
// Temps de fonctionnement (utilisé par l’interface web)
// -----------------------------------------------------------------------------
static unsigned long startTimeMs = 0;

String getUptime()
{
    unsigned long secs = (millis() - startTimeMs) / 1000;

    int days  = secs / 86400; secs %= 86400;
    int hours = secs / 3600;  secs %= 3600;
    int mins  = secs / 60;    secs %= 60;

    return String(days) + "j "
         + String(hours) + "h "
         + String(mins) + "m "
         + String(secs) + "s";
}

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(200);

    Logger::init();   // Logger toujours en premier
    Logger::info("Boot système");

    startTimeMs = millis();

    // --- Système de fichiers ---
    if (!SPIFFS.begin(true)) {
        Logger::error("Erreur SPIFFS");
        // On continue quand même : certaines fonctions peuvent rester actives
    }

    FileSystem::init();     // Abstraction FS (SPIFFS / SD plus tard)
    DataLogger::init();     // Historique et persistance des données

    // --- Alimentation / PMU ---
    PowerManager::init();

    // --- Connectivités ---
    WiFiManager::init();        // STA + AP
    CellularManager::init();    // SIM7080G (modem éteint au départ si besoin)

    // --- Capteurs ---
    DataAcquisition::init();    // Initialisation matérielle uniquement

    // --- Serveur Web ---
    WebServer::init();

    Logger::info("Initialisation terminée");
}

// -----------------------------------------------------------------------------
// LOOP
// -----------------------------------------------------------------------------
void loop()
{
    // --- Gestion WiFi (reconnexion STA, états AP) ---
    WiFiManager::handle();

    // --- Tâches périodiques centralisées ---
    // TaskManager::handle();

    // --- Mise à jour batterie (lecture PMU) ---
    PowerManager::update();

    // --- Acquisition capteurs (si nécessaire en continu) ---
    // DataAcquisition::handle();

    // Rien d’autre ici :
    // - pas de init()
    // - pas de SPIFFS
    // - pas de Web
}
