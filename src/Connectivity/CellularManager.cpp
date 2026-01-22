// src/Connectivity/CellularManager.cpp
// Version corrigée : séquences PWRKEY non-bloquantes
// Respecte l'invariant : aucun delay(), handle() < 100ms

#include "Connectivity/CellularManager.h"
#include "Core/PowerManager.h"
#include "Utils/Logger.h"
#include "Config/IO-Config.h"
#include "Config/NetworkConfig.h"

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
#endif

// Tag pour logs
static const char* TAG = "Cellular";

// -----------------------------------------------------------------------------
// Configuration modem
// -----------------------------------------------------------------------------
enum {
    MODEM_CATM = 1,
    MODEM_NB_IOT = 2,
    MODEM_CATM_NBIOT = 3
};

// Messages de statut réseau
static const char *register_info[] = {
    "Not registered, MT is not currently searching an operator to register to.",
    "Registered, home network.",
    "Not registered, but MT is currently trying to attach or searching an operator to register to.",
    "Registration denied, the GPRS service is disabled.",
    "Unknown.",
    "Registered, roaming."
};

// -----------------------------------------------------------------------------
// Membres statiques
// -----------------------------------------------------------------------------
CellularManager::State CellularManager::currentState = State::IDLE;
unsigned long CellularManager::lastStateChange = 0;
int CellularManager::stateCycleCount = 0;
bool CellularManager::enabled = true;
bool CellularManager::connected = false;
int CellularManager::signalQuality = 99;
String CellularManager::operatorName = "";
IPAddress CellularManager::localIP = IPAddress(0, 0, 0, 0);

int CellularManager::subStep = 0;
int CellularManager::bearerCycleCount = 0;
unsigned long CellularManager::handleStartTime = 0;

// Timestamp pour séquences PWRKEY non-bloquantes
unsigned long CellularManager::powerStepStartMs = 0;

// Gestion ticket modem
bool CellularManager::modemLocked = false;
unsigned long CellularManager::modemLockTime = 0;

// Préférences
Preferences CellularManager::preferences;

// Compteur de recovery (pour éviter boucle infinie)
static int recoveryCount = 0;
static bool recoveryInProgress = false;
static bool pendingDisable = false;  // Demande de désactivation en attente
static constexpr int MAX_RECOVERY_ATTEMPTS = 3;
static constexpr int ERROR_WAIT_CYCLES = 150;  // 5 minutes (150 * 2s)

// Timeouts en nombre de cycles (2s par cycle)
static constexpr int TIMEOUT_AT_RESPONSE = 10;      // 20s pour réponses AT (+CFUN)
static constexpr int TIMEOUT_NETWORK_WAIT = 60;     // 120s pour enregistrement réseau
static constexpr int MODEM_RETRY_MAX = 6;           // Nombre de retry avant power cycle
static constexpr int TIMEOUT_BEARER = 5;            // 10s pour activation bearer

// Instance modem
#ifdef DUMP_AT_COMMANDS
static StreamDebugger debugger(Serial1, Serial);
static TinyGsm modem(debugger);
#else
static TinyGsm modem(Serial1);
#endif

// -----------------------------------------------------------------------------
// Accès externe au modem (utilisé par SmsManager)
// -----------------------------------------------------------------------------
TinyGsm& getModem()
{
    return modem;
}

// -----------------------------------------------------------------------------
// Helper : budget temps dépassé ?
// -----------------------------------------------------------------------------
bool CellularManager::budgetExceeded()
{
    if ((millis() - handleStartTime) >= BUDGET_MS) {
        Logger::debug(TAG, "Budget temps dépassé, report au cycle suivant");
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Conversion signal (0-31) → dBm
// -----------------------------------------------------------------------------
int CellularManager::signalTodBm(int signal)
{
    if (signal == 99 || signal < 0 || signal > 31) {
        return -999;  // Inconnu
    }
    // Formule standard : dBm = -113 + (2 * CSQ)
    return -113 + (2 * signal);
}

// -----------------------------------------------------------------------------
// Gestion ticket modem
// -----------------------------------------------------------------------------
bool CellularManager::isModemAvailable()
{
    return connected && !modemLocked;
}

bool CellularManager::requestModem()
{
    // Vérifier timeout si modem déjà locké
    if (modemLocked) {
        if ((millis() - modemLockTime) >= MODEM_LOCK_TIMEOUT_MS) {
            Logger::warn(TAG, "Timeout ticket modem - libération forcée");
            modemLocked = false;
        } else {
            return false;  // Modem occupé
        }
    }

    // Vérifier disponibilité
    if (!connected) {
        return false;  // Modem pas prêt
    }

    // Accorder le ticket
    modemLocked = true;
    modemLockTime = millis();
    Logger::debug(TAG, "Ticket modem accordé");
    return true;
}

void CellularManager::freeModem()
{
    if (modemLocked) {
        modemLocked = false;
        Logger::debug(TAG, "Ticket modem libéré");
    }
}

// -----------------------------------------------------------------------------
// Chargement préférences
// -----------------------------------------------------------------------------
void CellularManager::loadPreferences()
{
    preferences.begin("cellular", false);
    enabled = preferences.getBool("enabled", true);  // Activé par défaut
    preferences.end();
}

// -----------------------------------------------------------------------------
// Initialisation
// -----------------------------------------------------------------------------
void CellularManager::init()
{
    Logger::info(TAG, "Initialisation modem SIM7080G...");

    // Chargement des préférences
    loadPreferences();
    Logger::info(TAG, String("GSM ") + (enabled ? "activé" : "désactivé") + " (préférence)");

    // Configuration UART pour modem
    Serial1.begin(MODEM_UART_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    // Configuration pins de contrôle
    // IMPORTANT : pinMode uniquement, pas de digitalWrite()
    // Le pin reste dans son état par défaut au boot
    // pinMode(MODEM_PWR_PIN, OUTPUT);
    // pinMode(MODEM_DTR_PIN, OUTPUT);

    // Note: PowerManager a déjà initialisé le PMU (DC3 + BLDO2)
    Logger::info(TAG, " Initialisation matérielle terminée");

    // Démarrer selon préférence
    if (enabled) {
        // Le modem est déjà allumé par le PMU (DC3 + BLDO2)
        // Pas besoin de PWRKEY au boot
        currentState = State::MODEM_INIT;
        Logger::info(TAG, "Démarrage GSM (modem allumé par PMU)...");
    } else {
        currentState = State::IDLE;
        Logger::info(TAG, "GSM désactivé");
    }

    lastStateChange = millis();
}

// -----------------------------------------------------------------------------
// Contrôle enabled (persistant, sans reboot immédiat)
// -----------------------------------------------------------------------------
void CellularManager::setEnabled(bool newEnabled)
{
    // Sauvegarder la préférence
    preferences.begin("cellular", false);
    preferences.putBool("enabled", newEnabled);
    preferences.end();

    Logger::info(TAG, String("GSM ") + (newEnabled ? "activé" : "désactivé") + " - sauvegardé");

    enabled = newEnabled;

    if (newEnabled) {
        // Activer : démarrer séquence d'allumage UNIQUEMENT si modem éteint
        pendingDisable = false;  // Annuler toute demande de désactivation
        if (currentState == State::IDLE) {
            // Modem éteint → il faut l'allumer via PWRKEY
            changeState(State::POWERING_ON, "POWERING_ON");
        } else if (currentState == State::POWERING_OFF) {
            // Extinction en cours → annuler et rallumer
            changeState(State::POWERING_ON, "POWERING_ON");
        }
        // Si déjà en cours de démarrage ou connecté → ne rien faire
    } else {
        // Désactiver : vérifier si on peut le faire en toute sécurité
        
        // États sûrs : on peut désactiver immédiatement
        if (currentState == State::IDLE || 
            currentState == State::CONNECTED || 
            currentState == State::ERROR ||
            currentState == State::POWERING_ON ||
            currentState == State::POWERING_OFF) {
            
            connected = false;
            if (currentState != State::IDLE && currentState != State::POWERING_OFF) {
                changeState(State::POWERING_OFF, "POWERING_OFF");
            }
            pendingDisable = false;
            
        } else {
            // États critiques (MODEM_INIT, SIM_CHECK, NETWORK_CONFIG, NETWORK_WAIT)
            // On pose un flag et on attendra un état sûr
            Logger::info(TAG, "Désactivation différée (attente état sûr)");
            pendingDisable = true;
            connected = false;
        }
    }
}

// -----------------------------------------------------------------------------
// Machine d'états (appelée toutes les 2 secondes)
// -----------------------------------------------------------------------------
void CellularManager::handle()
{
    handleStartTime = millis();

    // CORRECTION 3.1 : Si GSM désactivé, ne rien faire (sauf terminer POWERING_OFF en cours)
    // Exception : si pendingDisable est actif, on doit continuer jusqu'à un état sûr
    if (!enabled && !pendingDisable && currentState != State::POWERING_OFF) {
        return;
    }

    // Si modem locké par un client, ne rien faire (sauf vérifier timeout)
    if (modemLocked) {
        if ((millis() - modemLockTime) >= MODEM_LOCK_TIMEOUT_MS) {
            Logger::warn(TAG, "Timeout ticket modem - libération forcée");
            modemLocked = false;
        }
        return;
    }

    stateCycleCount++;

    switch (currentState) {
        
        case State::IDLE:
            // Modem éteint, rien à faire
            break;
            
        case State::POWERING_ON:
            handlePoweringOn();
            break;
            
        case State::POWERING_OFF:
            handlePoweringOff();
            break;
            
        case State::MODEM_INIT:
            handleModemInit();
            break;
            
        case State::SIM_CHECK:
            handleSimCheck();
            break;
            
        case State::NETWORK_CONFIG:
            handleNetworkConfig();
            break;
            
        case State::NETWORK_WAIT:
            handleNetworkWait();
            break;
            
        case State::CONNECTED:
            handleConnected();
            break;
            
        case State::ERROR:
            handleError();
            break;
    }
}

// -----------------------------------------------------------------------------
// Helper : changement d'état
// -----------------------------------------------------------------------------
void CellularManager::changeState(State newState, const char* stateName)
{
    Logger::debug(TAG, String("État: ") + stateName);
    currentState = newState;
    lastStateChange = millis();
    stateCycleCount = 0;
    subStep = 0;
    powerStepStartMs = 0;
}

// -----------------------------------------------------------------------------
// État : POWERING_ON (séquence PWRKEY allumage non-bloquante)
// -----------------------------------------------------------------------------
void CellularManager::handlePoweringOn()
{
    // Séquence : LOW (1100ms) → HIGH (100ms) → LOW (2000ms repos)
    // subStep :
    // 0 = démarrage, PWRKEY LOW
    // 1 = attente 1100ms
    // 2 = PWRKEY HIGH
    // 3 = attente 100ms
    // 4 = PWRKEY LOW (repos)
    // 5 = attente 2000ms
    // 6 = transition MODEM_INIT

    if (stateCycleCount == 1 && subStep == 0) {
        Logger::info(TAG, "Allumage modem (PWRKEY)...");
    }

    switch (subStep) {
        case 0:
            digitalWrite(MODEM_PWR_PIN, LOW);
            powerStepStartMs = millis();
            subStep = 1;
            return;
            
        case 1:
            if ((millis() - powerStepStartMs) >= 1100) {
                digitalWrite(MODEM_PWR_PIN, HIGH);
                powerStepStartMs = millis();
                subStep = 2;
            }
            return;
            
        case 2:
            if ((millis() - powerStepStartMs) >= 100) {
                digitalWrite(MODEM_PWR_PIN, LOW);
                powerStepStartMs = millis();
                subStep = 3;
            }
            return;
            
        case 3:
            if ((millis() - powerStepStartMs) >= 2000) {
                Logger::debug(TAG, "Séquence allumage terminée");
                changeState(State::MODEM_INIT, "MODEM_INIT");
            }
            return;
    }
}

// -----------------------------------------------------------------------------
// État : POWERING_OFF (séquence PWRKEY extinction non-bloquante)
// -----------------------------------------------------------------------------
void CellularManager::handlePoweringOff()
{
    // Séquence : LOW (2000ms) → HIGH (100ms) → LOW (1000ms repos)
    // subStep :
    // 0 = démarrage, PWRKEY LOW
    // 1 = attente 2000ms
    // 2 = PWRKEY HIGH
    // 3 = attente 100ms
    // 4 = PWRKEY LOW (repos)
    // 5 = attente 1000ms
    // 6 = transition IDLE

    if (stateCycleCount == 1 && subStep == 0) {
        Logger::info(TAG, "Extinction modem (PWRKEY)...");
    }

    switch (subStep) {
        case 0:
            digitalWrite(MODEM_PWR_PIN, LOW);
            powerStepStartMs = millis();
            subStep = 1;
            return;
            
        case 1:
            if ((millis() - powerStepStartMs) >= 2000) {
                digitalWrite(MODEM_PWR_PIN, HIGH);
                powerStepStartMs = millis();
                subStep = 2;
            }
            return;
            
        case 2:
            if ((millis() - powerStepStartMs) >= 100) {
                digitalWrite(MODEM_PWR_PIN, LOW);
                powerStepStartMs = millis();
                subStep = 3;
            }
            return;
            
        case 3:
            if ((millis() - powerStepStartMs) >= 1000) {
                Logger::debug(TAG, "Séquence extinction terminée");
                
                // CORRECTION 3.2 : Si extinction pour recovery, vérifier que enabled est toujours true
                // Le choix utilisateur doit TOUJOURS gagner sur le recovery
                if (recoveryInProgress && enabled) {
                    Logger::info(TAG, "Recovery : redémarrage modem...");
                    recoveryInProgress = false;
                    changeState(State::POWERING_ON, "POWERING_ON (recovery)");
                } else {
                    if (recoveryInProgress) {
                        Logger::info(TAG, "Recovery annulé (GSM désactivé par utilisateur)");
                        recoveryInProgress = false;
                    }
                    changeState(State::IDLE, "IDLE");
                }
            }
            return;
    }
}

// -----------------------------------------------------------------------------
// État : MODEM_INIT
// -----------------------------------------------------------------------------
void CellularManager::handleModemInit()
{
    // subStep :
    // 0 = test AT normal (avec retry progressif)
    // 1 = power-cycle : pin LOW
    // 2 = power-cycle : pin HIGH
    // 3 = power-cycle : pin LOW (état repos), fin séquence

    if (stateCycleCount == 1 && subStep == 0) {
        Logger::info(TAG, "Démarrage modem...");
    }

    // Power-cycle en cours (séquence non-bloquante)
    if (subStep > 0) {
        switch (subStep) {
            case 1:
                digitalWrite(MODEM_PWR_PIN, LOW);
                Logger::debug(TAG, "Power-cycle: LOW");
                subStep = 2;
                return;
                
            case 2:
                digitalWrite(MODEM_PWR_PIN, HIGH);
                Logger::debug(TAG, "Power-cycle: HIGH");
                subStep = 3;
                return;
                
            case 3:
                digitalWrite(MODEM_PWR_PIN, LOW);
                Logger::debug(TAG, "Power-cycle: terminé");
                subStep = 0;
                stateCycleCount = 0;
                return;
        }
    }

    if (budgetExceeded()) return;

    // Test AT
    if (modem.testAT(1000)) {
        Logger::info(TAG, " Modem répond aux commandes AT");
        changeState(State::SIM_CHECK, "SIM_CHECK");
        return;
    }

    // Retry progressif avant power-cycle
    if (stateCycleCount >= MODEM_RETRY_MAX) {
        Logger::info(TAG, "Démarrage power-cycle modem...");
        subStep = 1;
    } else {
        Logger::debug(TAG, "Attente modem... (" + String(stateCycleCount) + "/" + String(MODEM_RETRY_MAX) + ")");
    }
}

// -----------------------------------------------------------------------------
// État : SIM_CHECK
// -----------------------------------------------------------------------------
void CellularManager::handleSimCheck()
{
    // subStep :
    // 0 = vérification statut SIM
    // 1 = lecture CCID
    // 2 = lecture IMEI
    // 3 = lecture IMSI + transition

    if (budgetExceeded()) return;

    switch (subStep) {
        
        case 0:
            Logger::info(TAG, "Vérification carte SIM...");
            if (modem.getSimStatus() != SIM_READY) {
                Logger::error(TAG, "Carte SIM non détectée");
                changeState(State::ERROR, "ERROR");
                return;
            }
            Logger::info(TAG, " Carte SIM détectée");
            subStep = 1;
            return;
            
        case 1: {
            String ccid = modem.getSimCCID();
            Logger::info(TAG, "CCID: " + ccid);
            subStep = 2;
            return;
        }
            
        case 2: {
            String imei = modem.getIMEI();
            Logger::info(TAG, "IMEI: " + imei);
            subStep = 3;
            return;
        }
            
        case 3: {
            String imsi = modem.getIMSI();
            Logger::info(TAG, "IMSI: " + imsi);
            changeState(State::NETWORK_CONFIG, "NETWORK_CONFIG");
            return;
        }
    }
}

// -----------------------------------------------------------------------------
// État : NETWORK_CONFIG
// -----------------------------------------------------------------------------
void CellularManager::handleNetworkConfig()
{
    // subStep :
    // 0 = désactivation RF (AT+CFUN=0)
    // 1 = attente réponse CFUN=0
    // 2 = configuration mode réseau
    // 3 = attente réponse mode réseau
    // 4 = configuration préférence
    // 5 = attente réponse préférence
    // 6 = configuration APN (CGDCONT)
    // 7 = attente réponse CGDCONT
    // 8 = configuration APN (CNCFG)
    // 9 = attente réponse CNCFG
    // 10 = activation RF (AT+CFUN=1)
    // 11 = attente réponse CFUN=1
    // 12 = activation LED réseau + transition

    if (budgetExceeded()) return;

    switch (subStep) {
        
        case 0:
            Logger::info(TAG, "Configuration réseau...");
            Logger::debug(TAG, "Désactivation RF...");
            modem.sendAT("+CFUN=0");
            subStep = 1;
            stateCycleCount = 0;
            return;
            
        case 1:
            if (modem.waitResponse(1000) == 1) {
                Logger::debug(TAG, "RF désactivée");
                subStep = 2;
            } else if (stateCycleCount >= TIMEOUT_AT_RESPONSE) {
                Logger::error(TAG, "Timeout désactivation RF");
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        case 2:
            Logger::debug(TAG, "Configuration Cat-M...");
            modem.setNetworkMode(38);
            modem.setPreferredMode(1);
            modem.sendAT("+CNMP=38");
            subStep = 3;
            stateCycleCount = 0;
            return;
            
        case 3:
            if (modem.waitResponse(1000) == 1) {
                subStep = 4;
            } else if (stateCycleCount >= TIMEOUT_AT_RESPONSE) {
                Logger::error(TAG, "Erreur configuration mode réseau");
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        case 4:
            modem.sendAT("+CMNB=1");
            subStep = 5;
            stateCycleCount = 0;
            return;
            
        case 5:
            if (modem.waitResponse(1000) == 1) {
                Logger::info(TAG, " Cat-M configuré");
                subStep = 6;
            } else if (stateCycleCount >= TIMEOUT_AT_RESPONSE) {
                Logger::error(TAG, "Erreur configuration préférence");
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        case 6:
            Logger::debug(TAG, "Configuration APN...");
            modem.sendAT("+CGDCONT=1,\"IP\",\"", CELLULAR_APN, "\"");
            subStep = 7;
            stateCycleCount = 0;
            return;
            
        case 7:
            if (modem.waitResponse(1000) == 1) {
                Logger::debug(TAG, "APN CGDCONT OK");
                subStep = 8;
            } else if (stateCycleCount >= TIMEOUT_AT_RESPONSE) {
                Logger::error(TAG, "Erreur configuration APN (CGDCONT)");
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        case 8:
            modem.sendAT("+CNCFG=0,1,\"", CELLULAR_APN, "\"");
            subStep = 9;
            stateCycleCount = 0;
            return;
            
        case 9:
            if (modem.waitResponse(1000) == 1) {
                Logger::debug(TAG, "APN CNCFG OK");
                Logger::info(TAG, "APN configuré: " + String(CELLULAR_APN));
                subStep = 10;
                stateCycleCount = 0;
            } else if (stateCycleCount >= TIMEOUT_AT_RESPONSE) {
                Logger::error(TAG, "Erreur configuration APN (CNCFG)");
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        case 10:
            modem.sendAT("+CFUN=1");
            subStep = 11;
            stateCycleCount = 0;
            return;
            
        case 11:
            if (modem.waitResponse(1000) == 1) {
                Logger::info(TAG, "RF activée");
                subStep = 12;
                stateCycleCount = 0;
            } else if (stateCycleCount >= TIMEOUT_AT_RESPONSE) {
                Logger::error(TAG, "Timeout activation RF");
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        case 12:
            modem.sendAT("+CNETLIGHT=1");
            if (modem.waitResponse(1000) == 1) {
                Logger::debug(TAG, "LED réseau activée");
            }
            Logger::info(TAG, " Configuration terminée");
            changeState(State::NETWORK_WAIT, "NETWORK_WAIT");
            return;
    }
}

// -----------------------------------------------------------------------------
// État : NETWORK_WAIT
// -----------------------------------------------------------------------------
void CellularManager::handleNetworkWait()
{
    // subStep :
    // 0 = vérification statut réseau
    // 1 = envoi +CNACT=0,1 (activation bearer)
    // 2 = attente réponse +CNACT
    // 3 = vérification GPRS
    // 4 = récupération opérateur
    // 5 = récupération IP
    // 6 = récupération signal + transition CONNECTED

    if (budgetExceeded()) return;

    switch (subStep) {
        
        case 0: {
            SIM70xxRegStatus s = modem.getRegistrationStatus();
            
            if (s == REG_OK_HOME || s == REG_OK_ROAMING) {
                Logger::info(TAG, " Enregistré sur réseau: " + String(register_info[s]));
                subStep = 1;
                return;
            }
            
            if (stateCycleCount >= TIMEOUT_NETWORK_WAIT) {
                Logger::error(TAG, "Timeout enregistrement réseau");
                changeState(State::ERROR, "ERROR");
                return;
            }
            
            Logger::debug(TAG, "Recherche réseau... (" + String(stateCycleCount) + "/" + String(TIMEOUT_NETWORK_WAIT) + ") - " + String(register_info[s]));
            return;
        }
        
        case 1:
            Logger::debug(TAG, "Activation bearer...");
            modem.sendAT("+CNACT=0,1");
            subStep = 2;
            bearerCycleCount = 0;
            return;
            
        case 2: {
            bearerCycleCount++;
            int response = modem.waitResponse(1000);
            if (response == 1) {
                Logger::debug(TAG, "Bearer activé");
                subStep = 3;
            } else if (bearerCycleCount >= TIMEOUT_BEARER) {
                Logger::error(TAG, "Erreur activation bearer");
                changeState(State::ERROR, "ERROR");
            }
            return;
        }
        
        case 3:
            if (!modem.isGprsConnected()) {
                Logger::error(TAG, "Pas de connexion GPRS");
                changeState(State::ERROR, "ERROR");
                return;
            }
            Logger::debug(TAG, "GPRS connecté");
            subStep = 4;
            return;
            
        case 4:
            operatorName = modem.getOperator();
            Logger::info(TAG, "Opérateur: " + operatorName);
            subStep = 5;
            return;
            
        case 5:
            localIP = modem.localIP();
            Logger::info(TAG, "IP locale: " + localIP.toString());
            subStep = 6;
            return;
            
        case 6:
            signalQuality = modem.getSignalQuality();
            Logger::info(TAG, "Signal: " + String(signalQuality) + "/31 (" + String(signalTodBm(signalQuality)) + " dBm)");
            connected = true;
            recoveryCount = 0;  // Reset compteur recovery après succès
            Logger::info(TAG, " Modem connecté");
            changeState(State::CONNECTED, "CONNECTED");
            return;
    }
}

// -----------------------------------------------------------------------------
// État : CONNECTED
// -----------------------------------------------------------------------------
void CellularManager::handleConnected()
{
    // subStep :
    // 0 = lecture signal
    // 1 = vérification GPRS

    if (budgetExceeded()) return;

    switch (subStep) {
        
        case 0:
            signalQuality = modem.getSignalQuality();
            subStep = 1;
            return;
            
        case 1:
            if (!modem.isGprsConnected()) {
                Logger::warn(TAG, "Connexion perdue !");
                connected = false;
                changeState(State::NETWORK_WAIT, "NETWORK_WAIT");
                return;
            }
            
            // CORRECTION 3.3 : Traiter pendingDisable si demandé
            if (pendingDisable) {
                Logger::info(TAG, "Traitement désactivation différée");
                pendingDisable = false;
                connected = false;
                changeState(State::POWERING_OFF, "POWERING_OFF");
                return;
            }
            
            subStep = 0;
            return;
    }
}

// -----------------------------------------------------------------------------
// État : ERROR (avec recovery automatique NON-BLOQUANT)
// -----------------------------------------------------------------------------
void CellularManager::handleError()
{
    connected = false;

    // CORRECTION 3.3 : Traiter pendingDisable prioritairement
    if (pendingDisable) {
        Logger::info(TAG, "Traitement désactivation différée (depuis ERROR)");
        pendingDisable = false;
        changeState(State::POWERING_OFF, "POWERING_OFF");
        return;
    }

    // Premier cycle : log de l'erreur
    if (stateCycleCount == 1) {
        if (recoveryCount >= MAX_RECOVERY_ATTEMPTS) {
            Logger::error(TAG, "Modem en erreur - recovery max atteint (" + 
                         String(MAX_RECOVERY_ATTEMPTS) + " tentatives)");
            Logger::error(TAG, "Mode dégradé : GSM indisponible");
        } else {
            Logger::error(TAG, "Modem en erreur - recovery dans 5 min (tentative " + 
                         String(recoveryCount + 1) + "/" + String(MAX_RECOVERY_ATTEMPTS) + ")");
        }
    }

    // Si max recovery atteint → blocage définitif (mode dégradé)
    if (recoveryCount >= MAX_RECOVERY_ATTEMPTS) {
        return;
    }

    // Attendre 5 minutes avant recovery
    if (stateCycleCount >= ERROR_WAIT_CYCLES) {
        Logger::info(TAG, "Tentative de recovery modem...");
        recoveryCount++;
        recoveryInProgress = true;
        
        // Démarrer séquence d'extinction (non-bloquante)
        // Puis POWERING_OFF → POWERING_ON → MODEM_INIT
        changeState(State::POWERING_OFF, "POWERING_OFF (recovery)");
    }
}

// -----------------------------------------------------------------------------
// Getters
// -----------------------------------------------------------------------------
bool CellularManager::isEnabled()
{
    return enabled;
}

bool CellularManager::isConnected()
{
    return connected;
}

int CellularManager::getSignalQuality()
{
    return signalQuality;
}

String CellularManager::getOperator()
{
    return operatorName;
}

IPAddress CellularManager::getLocalIP()
{
    return localIP;
}

// -----------------------------------------------------------------------------
// Status formaté pour l'interface Web
// -----------------------------------------------------------------------------
String CellularManager::getStatus()
{
    if (!enabled) {
        return "Désactivé";
    }

    if (currentState == State::POWERING_ON) {
        return "Démarrage...";
    }

    if (currentState == State::POWERING_OFF) {
        return "Extinction...";
    }

    if (currentState == State::ERROR) {
        if (recoveryCount >= MAX_RECOVERY_ATTEMPTS) {
            return "Indisponible";
        }
        return "Erreur (recovery en cours)";
    }

    if (!connected) {
        return "Recherche réseau...";
    }

    // Connecté : afficher opérateur et signal
    String status = "Connecté";

    if (operatorName.length() > 0) {
        status += " (" + operatorName + ")";
    }

    if (signalQuality != 99) {
        status += " " + String(signalTodBm(signalQuality)) + " dBm";
    }

    return status;
}