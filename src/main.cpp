// main.cpp
// Point d’entrée principal du système
// Rôle : orchestration globale, aucune logique métier

#include <Arduino.h>
#include <SPIFFS.h>

#include "Config/Config.h"
#include "Config/TimingConfig.h"

#include "Connectivity/WiFiManager.h"
#include "Connectivity/CellularManager.h"

#include "Core/PowerManager.h"
#include "Core/TaskManager.h"
#include "Core/EventManager.h"

#include "Sensors/DataAcquisition.h"

#include "Storage/FileSystem.h"
#include "Storage/DataLogger.h"

#include "Web/WebServer.h"
#include "Utils/Logger.h"

// -----------------------------------------------------------------------------
// Cycle de vie système : INIT → RUN
// -----------------------------------------------------------------------------

static unsigned long bootTimeMs = 0;

// -----------------------------------------------------------------------------
// Temps de fonctionnement (utilisé par l’interface web)
// -----------------------------------------------------------------------------
// ⚠️ Correction : symbole unique et global (utilisé par PagePrincipale)
unsigned long startTime = 0;

// Prototypes des boucles internes
static void loopInit();
static void loopRun();

// Pointeur vers la loop active
static void (*currentLoop)() = loopInit;

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    delay(200);

    Logger::begin();   // Logger toujours en premier
    Logger::info("Boot système");

    bootTimeMs = millis();
    startTime  = bootTimeMs;

    // --- Système de fichiers ---
    if (!SPIFFS.begin(true)) {
        Logger::error("Erreur SPIFFS");
        // On continue quand même
    }

    FileSystem::init();
    DataLogger::init();

    // --- Alimentation / PMU ---
    PowerManager::init();   // Initialise + première lecture immédiate

    // --- Connectivités ---
    WiFiManager::init();        // STA + AP
    CellularManager::init();    // Non utilisé pour l’instant

    // --- Capteurs ---
    DataAcquisition::init();    // Initialisation matérielle uniquement

    // --- Serveur Web ---
    WebServer::init();

    Logger::info("Initialisation matérielle terminée");
}

// -----------------------------------------------------------------------------
// LOOP Arduino (immuable)
// -----------------------------------------------------------------------------

void loop()
{
    currentLoop();
}

// -----------------------------------------------------------------------------
// Phase INIT
// -----------------------------------------------------------------------------

static void loopInit()
{
    // Attente de stabilisation système
    if (millis() - bootTimeMs < SYSTEM_INIT_DELAY_MS) {
        return;
    }

    // -------------------------------------------------------------------------
    // Transition INIT → RUN (une seule fois)
    // -------------------------------------------------------------------------

    Logger::info("Entrée en régime permanent");

    // Initialisation EventManager avec état stable
    EventManager::init();
    EventManager::prime();

    // Démarrage du TaskManager
    TaskManager::init();

    // --- Tâche EventManager (rythme système surveillé) ---
    TaskManager::addTask(
        []() {
            EventManager::handle();
        },
        EVENT_MANAGER_PERIOD_MS
    );

    // --- Tâche PowerManager (cycle lent / batterie) ---
    TaskManager::addTask(
        []() {
            PowerManager::update();
        },
        POWER_MANAGER_UPDATE_INTERVAL_MS
    );

    // -------------------------------------------------------------------------
    // AJOUT : Tâche Wi-Fi (état STA / AP / RSSI → DataLogger)
    // Même logique et même périodicité que la batterie
    // -------------------------------------------------------------------------
    TaskManager::addTask(
        []() {
            // STA activé
            DataLogger::push(
                DataType::System,
                DataId::WifiStaEnabled,
                WiFiManager::isSTAEnabled() ? 1.0f : 0.0f
            );

            // STA connecté
            DataLogger::push(
                DataType::System,
                DataId::WifiStaConnected,
                WiFiManager::isSTAConnected() ? 1.0f : 0.0f
            );

            // AP activé
            DataLogger::push(
                DataType::System,
                DataId::WifiApEnabled,
                WiFiManager::isAPEnabled() ? 1.0f : 0.0f
            );

            // RSSI brut (si connecté)
            if (WiFiManager::isSTAConnected()) {
                DataLogger::push(
                    DataType::System,
                    DataId::WifiRssi,
                    (float)WiFi.RSSI()
                );
            } else {
                // Valeur sentinelle hors connexion
                DataLogger::push(
                    DataType::System,
                    DataId::WifiRssi,
                    -100.0f
                );
            }
        },
        WIFI_STATUS_UPDATE_INTERVAL_MS   // typiquement 30000 ms
    );

    // Bascule définitive vers la loop de production
    currentLoop = loopRun;
}

// -----------------------------------------------------------------------------
// Phase RUN (production)
// -----------------------------------------------------------------------------

static void loopRun()
{
    WiFiManager::handle();
    TaskManager::handle();
}
