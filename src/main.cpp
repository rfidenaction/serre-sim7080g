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

// Prototypes des boucles internes
static void loopInit();
static void loopRun();

// Pointeur vers la loop active
static void (*currentLoop)() = loopInit;

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

    Logger::begin();   // Logger toujours en premier
    Logger::info("Boot système");

    bootTimeMs  = millis();
    startTimeMs = bootTimeMs;

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

    // --- Tâche PowerManager (cycle lent) ---
    TaskManager::addTask(
        []() {
            PowerManager::update();
        },
        POWER_MANAGER_UPDATE_INTERVAL_MS
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
