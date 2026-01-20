// src/Connectivity/CellularManager.h
#pragma once
#include <Arduino.h>
#include <TinyGsmClient.h>
#include "Config/IO-Config.h"

class CellularManager {
public:
    // Cycle de vie
    static void init();      // Initialisation matérielle uniquement
    static void handle();    // Machine d'états (appelée toutes les 2s)
    
    // Gestion accès modem (système de ticket)
    static bool requestModem();   // Demande accès → true si accordé
    static void freeModem();      // Libère l'accès après usage
    
    // Getters (lecture passive pour DataLogger)
    static bool isReady();              // Modem connecté et prêt
    static int getSignalQuality();      // 0-31 = signal, 99 = unknown
    static String getOperator();        // Nom opérateur
    static IPAddress getLocalIP();      // IP locale
    
private:
    enum class State {
        IDLE,
        MODEM_INIT,
        SIM_CHECK,
        NETWORK_CONFIG,
        NETWORK_WAIT,
        CONNECTED,
        ERROR
    };
    
    // Machine d'états
    static State currentState;
    static unsigned long lastStateChange;
    static int stateCycleCount;
    
    // Sous-états pour séquences multi-cycles
    static int subStep;
    
    // Compteur dédié pour timeout bearer (NETWORK_WAIT)
    static int bearerCycleCount;
    
    // Durée maximale autorisée par appel de handle()
    static constexpr unsigned long BUDGET_MS = 100;
    static unsigned long handleStartTime;
    static bool budgetExceeded();
    
    // Gestion ticket modem
    static bool modemLocked;              // true = ticket donné à un client
    static unsigned long modemLockTime;   // Timestamp du verrouillage
    static constexpr unsigned long MODEM_LOCK_TIMEOUT_MS = 100000;  // 100 secondes
    
    // Vérification interne disponibilité
    static bool isModemAvailable();
    
    // Handlers des états
    static void handleModemInit();
    static void handleSimCheck();
    static void handleNetworkConfig();
    static void handleNetworkWait();
    static void handleConnected();
    static void handleError();
    
    // Helper pour changement d'état
    static void changeState(State newState, const char* stateName);
    
    // Variables d'état (mises à jour par handle())
    static bool ready;
    static int signalQuality;
    static String operatorName;
    static IPAddress localIP;
};