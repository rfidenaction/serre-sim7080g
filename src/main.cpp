// main.cpp
// Point d’entrée principal du système
// Rôle : orchestration globale, aucune logique métier

#include <Arduino.h>
#include <SPIFFS.h>
#include <time.h>
#include <stdlib.h>

#include "Config/Config.h"
#include "Config/TimingConfig.h"

#include "Connectivity/WiFiManager.h"
#include "Connectivity/CellularManager.h"
#include "Connectivity/ManagerUTC.h"

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
// ⚠️ Symbole global unique (utilisé par PagePrincipale)
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

    // -------------------------------------------------------------------------
    // Initialisation timezone système (France / Paris)
    // -------------------------------------------------------------------------
    setenv("TZ", SYSTEM_TIMEZONE, 1);
    tzset();

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

    // -------------------------------------------------------------------------
    // INITIALISATION UTC / NTP
    // -------------------------------------------------------------------------
    ManagerUTC::init();

    // Tâche UTC / NTP (machine d’état autonome)
    TaskManager::addTask(
        []() {
            ManagerUTC::handle();
        },
        2000UL   // 2 secondes
    );

    // -------------------------------------------------------------------------
    // Tâche EventManager
    // -------------------------------------------------------------------------
    TaskManager::addTask(
        []() {
            EventManager::handle();
        },
        EVENT_MANAGER_PERIOD_MS
    );

    // -------------------------------------------------------------------------
    // TÂCHE BATTERIE / ALIMENTATION
    // -------------------------------------------------------------------------
    TaskManager::addTask(
        []() {
            // Mise à jour des mesures PMU
            PowerManager::update();

            // Tension batterie
            DataLogger::push(
                DataType::Battery,
                DataId::BatteryVoltage,
                PowerManager::getBatteryVoltage()
            );

            // Pourcentage batterie
            DataLogger::push(
                DataType::Battery,
                DataId::BatteryPercent,
                (float)PowerManager::getBatteryPercent()
            );

            // Batterie en charge
            DataLogger::push(
                DataType::Battery,
                DataId::Charging,
                PowerManager::isCharging() ? 1.0f : 0.0f
            );

            // Alimentation externe
            DataLogger::push(
                DataType::Battery,
                DataId::ExternalPower,
                PowerManager::isExternalPowerPresent() ? 1.0f : 0.0f
            );
        },
        POWER_MANAGER_UPDATE_INTERVAL_MS
    );

    // -------------------------------------------------------------------------
    // TÂCHE WI-FI → DataLogger
    // -------------------------------------------------------------------------
    TaskManager::addTask(
        []() {
            DataLogger::push(
                DataType::System,
                DataId::WifiStaEnabled,
                WiFiManager::isSTAEnabled() ? 1.0f : 0.0f
            );

            DataLogger::push(
                DataType::System,
                DataId::WifiStaConnected,
                WiFiManager::isSTAConnected() ? 1.0f : 0.0f
            );

            DataLogger::push(
                DataType::System,
                DataId::WifiApEnabled,
                WiFiManager::isAPEnabled() ? 1.0f : 0.0f
            );

            if (WiFiManager::isSTAConnected()) {
                DataLogger::push(
                    DataType::System,
                    DataId::WifiRssi,
                    (float)WiFi.RSSI()
                );
            } else {
                DataLogger::push(
                    DataType::System,
                    DataId::WifiRssi,
                    -100.0f
                );
            }
        },
        WIFI_STATUS_UPDATE_INTERVAL_MS
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
