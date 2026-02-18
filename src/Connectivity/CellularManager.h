// src/Connectivity/CellularManager.h
// Gestionnaire modem SIM7080G — 100% non-bloquant
// Tous les appels AT passent par le système pending (SEND/WAIT)
// TinyGSM conservé uniquement comme wrapper haut niveau (SMS, futur MQTT)

#ifndef CELLULARMANAGER_H
#define CELLULARMANAGER_H

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <Preferences.h>
#include "Connectivity/CellularEvent.h"

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
    // Réception lignes modem (appelé par CellularEvent via main.cpp)
    // -----------------------------------------------------------------------------
    static void onModemLine(CellularLineType type, const char* line);

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
    static bool isPendingActive();       // Pending en cours ?

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
    // Système pending (SEND/WAIT non-bloquant)
    // -----------------------------------------------------------------------------
    // WAIT_OK           : commande SET simple (ex: AT)
    // WAIT_OK_OR_ERROR  : commande SET avec ERROR possible (ex: AT+CFUN=0)
    // WAIT_CPIN         : capture +CPIN: xxx puis OK (SIM_CHECK)
    // WAIT_NUMERIC      : capture ligne 100% digits puis OK (CCID/IMEI/IMSI)
    // WAIT_PREFIX       : capture première ligne matchant pendingPrefix puis OK
    //                     Utilisé pour toute commande QUERY (AT+CEREG?, AT+CSQ, etc.)
    // -----------------------------------------------------------------------------
    enum class PendingKind {
        NONE,
        WAIT_OK,
        WAIT_OK_OR_ERROR,
        WAIT_CPIN,
        WAIT_NUMERIC,
        WAIT_PREFIX         // Générique : capture ligne matchant un préfixe configurable
    };
    
    static bool pendingActive;
    static PendingKind pendingKind;
    static unsigned long pendingStartMs;
    static unsigned long pendingTimeoutMs;
    static bool pendingDone;
    static bool pendingSuccess;
    static char pendingData[64];    // Données extraites (ligne complète pour WAIT_PREFIX)
    static char pendingPrefix[16];  // Préfixe attendu pour WAIT_PREFIX (ex: "+CEREG:")

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

    // -----------------------------------------------------------------------------
    // Helpers pending
    // prefix : utilisé uniquement avec WAIT_PREFIX, ignoré sinon
    // -----------------------------------------------------------------------------
    static void startPending(PendingKind kind, unsigned long timeoutMs, const char* prefix = nullptr);
    static void clearPending();
    static void checkPendingTimeout();
};

// -----------------------------------------------------------------------------
// Accès externe au modem TinyGSM (pour SmsManager)
// -----------------------------------------------------------------------------
extern TinyGsm& getModem();

#endif // CELLULARMANAGER_H