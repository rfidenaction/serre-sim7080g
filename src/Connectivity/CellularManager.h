// src/Connectivity/CellularManager.h
// Version corrigée avec états POWERING_ON et POWERING_OFF

#ifndef CELLULARMANAGER_H
#define CELLULARMANAGER_H

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <Preferences.h>

class CellularManager
{
public:
    // -----------------------------------------------------------------------------
    // Machine d'états
    // -----------------------------------------------------------------------------
    enum class State {
        IDLE,             // Modem éteint
        POWERING_ON,      // Séquence allumage PWRKEY (non-bloquante)
        POWERING_OFF,     // Séquence extinction PWRKEY (non-bloquante)
        MODEM_INIT,       // Test AT + power-cycle si nécessaire
        SIM_CHECK,        // Vérification SIM + lecture CCID/IMEI/IMSI
        NETWORK_CONFIG,   // Configuration Cat-M + APN
        NETWORK_WAIT,     // Attente enregistrement réseau + activation bearer
        CONNECTED,        // Modem connecté et opérationnel
        ERROR             // Erreur avec recovery automatique
    };

    // -----------------------------------------------------------------------------
    // Budget temps par appel de handle()
    // -----------------------------------------------------------------------------
    static constexpr unsigned long BUDGET_MS = 100;  // 100 ms max par cycle

    // -----------------------------------------------------------------------------
    // Timeout ticket modem
    // -----------------------------------------------------------------------------
    static constexpr unsigned long MODEM_LOCK_TIMEOUT_MS = 30000;  // 30s

    // -----------------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------------
    static void init();
    static void handle();  // Appelée toutes les 2 secondes par TaskManager

    // -----------------------------------------------------------------------------
    // Contrôle ON/OFF (persistant)
    // -----------------------------------------------------------------------------
    static void setEnabled(bool enabled);  // Active/désactive le GSM (sauvegardé en NVS)
    static bool isEnabled();               // État activé/désactivé
    static bool isConnected();             // État connecté (réseau + IP)

    // -----------------------------------------------------------------------------
    // Gestion ticket modem (pour SmsManager, etc.)
    // -----------------------------------------------------------------------------
    static bool isModemAvailable();  // Modem prêt ET disponible
    static bool requestModem();      // Demander accès exclusif
    static void freeModem();         // Libérer accès

    // -----------------------------------------------------------------------------
    // Informations réseau
    // -----------------------------------------------------------------------------
    static int getSignalQuality();       // 0-31 (99 = inconnu)
    static String getOperator();         // Nom opérateur
    static IPAddress getLocalIP();       // IP locale

    // -----------------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------------
    static int signalTodBm(int signal);  // Conversion CSQ → dBm
    static String getStatus();           // Status formaté pour UI

private:
    // -----------------------------------------------------------------------------
    // État de la machine
    // -----------------------------------------------------------------------------
    static State currentState;
    static unsigned long lastStateChange;
    static int stateCycleCount;
    static bool enabled;          // Préférence persistante ON/OFF
    static bool connected;        // État connecté
    static int signalQuality;
    static String operatorName;
    static IPAddress localIP;

    // -----------------------------------------------------------------------------
    // Sous-états et compteurs
    // -----------------------------------------------------------------------------
    static int subStep;
    static int bearerCycleCount;
    static unsigned long handleStartTime;
    static unsigned long powerStepStartMs;  // Timestamp pour séquences PWRKEY

    // -----------------------------------------------------------------------------
    // Gestion ticket modem
    // -----------------------------------------------------------------------------
    static bool modemLocked;
    static unsigned long modemLockTime;

    // -----------------------------------------------------------------------------
    // Préférences NVS
    // -----------------------------------------------------------------------------
    static Preferences preferences;

    // -----------------------------------------------------------------------------
    // Handlers d'états
    // -----------------------------------------------------------------------------
    static void handlePoweringOn();
    static void handlePoweringOff();
    static void handleModemInit();
    static void handleSimCheck();
    static void handleNetworkConfig();
    static void handleNetworkWait();
    static void handleConnected();
    static void handleError();

    // -----------------------------------------------------------------------------
    // Helpers internes
    // -----------------------------------------------------------------------------
    static void changeState(State newState, const char* stateName);
    static bool budgetExceeded();
    static void loadPreferences();
};

// -----------------------------------------------------------------------------
// Accès externe au modem TinyGSM (pour SmsManager)
// -----------------------------------------------------------------------------
extern TinyGsm& getModem();

#endif // CELLULARMANAGER_H