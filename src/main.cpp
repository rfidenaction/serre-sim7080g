// main.cpp
// Point d'entrée principal du système
// Rôle : orchestration globale, aucune logique métier

#include <Arduino.h>
#include <SPIFFS.h>
#include <time.h>
#include <stdlib.h>

#include "Config/Config.h"
#include "Config/TimingConfig.h"

#include "Connectivity/WiFiManager.h"
#include "Connectivity/CellularStream.h"
#include "Connectivity/CellularEvent.h"
#include "Connectivity/CellularManager.h"
#include "Connectivity/SmsManager.h"
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
// Temps de fonctionnement (utilisé par l'interface web)
// -----------------------------------------------------------------------------
// ⚠️ Symbole global unique (utilisé par PagePrincipale)
unsigned long startTime = 0;

// Prototypes des boucles internes
static void loopInit();
static void loopRun();

// Pointeur vers la loop active
static void (*currentLoop)() = loopInit;

// -----------------------------------------------------------------------------
// Callback CellularEvent
// Logs uniquement : 
// - Erreurs (toujours)
// - OK quand pending actif (notre réponse attendue)
// - URC/LINE pendant boot
// -----------------------------------------------------------------------------
void onCellularLine(CellularLineType type, const char* line)
{
    // Toujours dispatcher vers CellularManager pour le système pending
    CellularManager::onModemLine(type, line);
    
    // Erreurs : toujours logger
    if (type == CellularLineType::ERROR) {
        Logger::error("CellEvent", "✗ ERROR");
        return;
    }
    
    // OK : logger uniquement si pending actif (c'est notre réponse)
    if (type == CellularLineType::OK) {
        if (CellularManager::isPendingActive()) {
            Logger::info("CellEvent", "✓ OK");
        }
        return;
    }
    
    // PROMPT : logger uniquement si pending actif
    if (type == CellularLineType::PROMPT) {
        if (CellularManager::isPendingActive()) {
            Logger::info("CellEvent", "→ >");
        }
        return;
    }
    
    // LINE (URC, réponses) : logger pendant boot ou si pending actif
    if (type == CellularLineType::LINE) {
        if (!CellularManager::isConnected() || CellularManager::isPendingActive()) {
            Logger::info("CellEvent", String("← ") + line);
        }
        return;
    }
}

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    delay(200);

    Logger::begin(Serial, Logger::Level::INFO);
    Logger::info("Boot système");

    // -------------------------------------------------------------------------
    // Initialisation timezone système (France / Paris)
    // -------------------------------------------------------------------------
        
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

    // --- CellularEvent (câblage dans l'ordre exact) ---
    // 1. Init CellularEvent
    CellularEvent::init();
    
    // 2. Brancher callback octet CellularStream → CellularEvent
    CellularStream::instance().setByteCallback(CellularEvent::onByte);
    
    // 3. Brancher callback ligne CellularEvent → main (qui dispatch vers CellularManager)
    CellularEvent::setLineCallback(onCellularLine);
    
    // 4. Activer le parsing
    CellularEvent::enableLineParsing(true);

    // --- Connectivités (APRÈS le câblage CellularEvent) ---
    WiFiManager::init();        // Active radio WiFi + lwIP (AP/STA démarrés par TaskManager)
    CellularManager::init();    // Modem SIM7080G Cat-M (instancie TinyGSM sur CellularStream)
    SmsManager::init();         // Gestionnaire SMS

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
    // TÂCHE CELLULAREVENT (polling UART modem - PRIORITAIRE)
    // -------------------------------------------------------------------------
    // Période 20ms — pompe Serial1 via CellularStream
    // Budget temps garanti 5ms max
    // Cette tâche DOIT être en premier (priorité absolue)
    // Guard : pas de polling si GSM désactivé (économie CPU + cohérence)
    // setEnabled(true) positionne le flag AVANT POWERING_ON → poll reprend
    // avant le premier échange AT
    TaskManager::addTask(
        []() {
            if (CellularManager::isEnabled()) {
                CellularEvent::poll();
            }
        },
        20UL  // 20 millisecondes
    );

    // -------------------------------------------------------------------------
    // TÂCHE WIFI (machine d'états non-bloquante)
    // -------------------------------------------------------------------------
    // Période 250ms — premier tick démarre la séquence AP_MODE_SET
    // L'AP démarrera ~750ms après l'entrée en RUN (tick 3)
    // Budget temps garanti <15ms sauf AP_START unique (~725ms au tick 3)
    TaskManager::addTask(
        []() {
            WiFiManager::handle();
        },
        250UL
    );

    // -------------------------------------------------------------------------
    // INITIALISATION UTC / NTP
    // -------------------------------------------------------------------------
    ManagerUTC::init();

    // Tâche UTC / NTP (machine d'état autonome)
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
    // TÂCHE CELLULARMANAGER (machine d'états modem)
    // -------------------------------------------------------------------------
    TaskManager::addTask(
        []() {
            CellularManager::handle();
        },
        2000UL  // 2 secondes
    );

    // -------------------------------------------------------------------------
    // TÂCHE SMSMANAGER (machine d'états SMS)
    // -------------------------------------------------------------------------
    // Guard : SMS impossible sans GSM actif
    TaskManager::addTask(
        []() {
            if (CellularManager::isEnabled()) {
                SmsManager::handle();
            }
        },
        2000UL  // 2 secondes
    );

    // -------------------------------------------------------------------------
    // TÂCHE DATALOGGER (flush SPIFFS + réparation UTC)
    // -------------------------------------------------------------------------
    TaskManager::addTask(
        []() {
            DataLogger::handle();
        },
        30000UL  // 30 secondes
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

    // -------------------------------------------------------------------------
    // TÂCHE CELLULAR → DataLogger (unifié avec pattern WiFi)
    // -------------------------------------------------------------------------
    TaskManager::addTask(
        []() {
            // État activé (préférence persistante)
            DataLogger::push(
                DataType::System,
                DataId::CellularEnabled,
                CellularManager::isEnabled() ? 1.0f : 0.0f
            );

            // État connecté (réseau + IP)
            DataLogger::push(
                DataType::System,
                DataId::CellularConnected,
                CellularManager::isConnected() ? 1.0f : 0.0f
            );

            // Qualité signal (0-31 = signal, 99 = unknown)
            DataLogger::push(
                DataType::System,
                DataId::CellularRssi,
                (float)CellularManager::getSignalQuality()
            );

            // Opérateur (String) - seulement si connecté
            if (CellularManager::isConnected()) {
                DataLogger::push(
                    DataType::System,
                    DataId::CellularOperator,
                    CellularManager::getOperator()
                );

                // IP locale (String)
                DataLogger::push(
                    DataType::System,
                    DataId::CellularIP,
                    CellularManager::getLocalIP().toString()
                );
            }
        },
        30000UL  // 30 secondes
    );

    // -------------------------------------------------------------------------
    // DEBUG : Statistiques CellularStream/CellularEvent (à supprimer après test)
    // -------------------------------------------------------------------------
    // Guard : pas de stats si GSM désactivé
    TaskManager::addTask(
        []() {
            if (!CellularManager::isEnabled()) return;
            
            uint32_t poll = CellularEvent::getPollCount();
            uint32_t tap = CellularStream::instance().getTapBytesCount();
            uint32_t ovf = CellularStream::instance().getOverflows();
            uint32_t lines = CellularEvent::getLinesReceived();
            uint32_t lineOvf = CellularEvent::getBufferOverflows();
            
            Logger::info("CellDbg", 
                String("poll=") + poll +
                " tap=" + tap + 
                " ovf=" + ovf + 
                " lines=" + lines + 
                " lineOvf=" + lineOvf
            );
        },
        10000UL  // 10 secondes
    );

    // Bascule définitive vers la loop de production
    currentLoop = loopRun;
}

// -----------------------------------------------------------------------------
// Phase RUN (production)
// -----------------------------------------------------------------------------

static void loopRun()
{
    TaskManager::handle();
}