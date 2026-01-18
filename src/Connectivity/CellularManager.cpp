// src/Connectivity/CellularManager.cpp
#include "Connectivity/CellularManager.h"
#include "Core/PowerManager.h"
#include "Utils/Logger.h"
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
bool CellularManager::ready = false;
int CellularManager::signalQuality = 99;
String CellularManager::operatorName = "";
IPAddress CellularManager::localIP = IPAddress(0, 0, 0, 0);

// Nouveaux membres statiques
int CellularManager::subStep = 0;
int CellularManager::bearerCycleCount = 0;
unsigned long CellularManager::handleStartTime = 0;

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
// Initialisation
// -----------------------------------------------------------------------------
void CellularManager::init()
{
    Logger::info(TAG, "Initialisation modem SIM7080G...");
    
    // Configuration UART pour modem
    Serial1.begin(MODEM_UART_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    
    // Configuration pins de contrôle
    pinMode(MODEM_PWR_PIN, OUTPUT);
    pinMode(MODEM_DTR_PIN, OUTPUT);
    pinMode(MODEM_RI_PIN, INPUT);
    
    // Note: PowerManager a déjà initialisé le PMU (DC3 + BLDO2)
    Logger::info(TAG, "✅ Initialisation matérielle terminée");
    
    currentState = State::MODEM_INIT;
    lastStateChange = millis();
}

// -----------------------------------------------------------------------------
// Machine d'états (appelée toutes les 2 secondes)
// -----------------------------------------------------------------------------
void CellularManager::handle()
{
    handleStartTime = millis();  // Démarrer le chrono budget
    stateCycleCount++;
    
    switch (currentState) {
        
        case State::IDLE:
            // Attente, ne fait rien
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
    subStep = 0;  // Reset sous-état
}

// -----------------------------------------------------------------------------
// État : MODEM_INIT
// -----------------------------------------------------------------------------
void CellularManager::handleModemInit()
{
    // subStep :
    // 0 = test AT normal
    // 1 = power-cycle : pin LOW, attendre prochain cycle
    // 2 = power-cycle : pin HIGH, attendre prochain cycle
    // 3 = power-cycle : pin LOW, fin séquence
    
    // Première fois dans cet état
    if (stateCycleCount == 1 && subStep == 0) {
        Logger::info(TAG, "Démarrage modem...");
    }
    
    // --- Power-cycle en cours ---
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
                stateCycleCount = 0;  // Reset pour reprendre les tentatives AT
                return;
        }
    }
    
    // --- Mode normal : test AT ---
    if (budgetExceeded()) return;
    
    if (modem.testAT(1000)) {
        Logger::info(TAG, "✅ Modem répond aux commandes AT");
        changeState(State::SIM_CHECK, "SIM_CHECK");
        return;
    }
    
    // Si pas de réponse après MODEM_RETRY_MAX cycles, déclencher power-cycle
    if (stateCycleCount >= MODEM_RETRY_MAX) {
        Logger::info(TAG, "Démarrage power-cycle modem...");
        subStep = 1;  // Prochain cycle = première étape du power-cycle
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
                Logger::error(TAG, "❌ Carte SIM non détectée !");
                changeState(State::ERROR, "ERROR");
                return;
            }
            Logger::info(TAG, "✅ Carte SIM détectée");
            subStep = 1;
            return;
            
        case 1: {
            String ccid = modem.getSimCCID();
            Logger::debug(TAG, "CCID: " + ccid);
            subStep = 2;
            return;
        }
        
        case 2: {
            String imei = modem.getIMEI();
            Logger::debug(TAG, "IMEI: " + imei);
            subStep = 3;
            return;
        }
        
        case 3: {
            String imsi = modem.getIMSI();
            Logger::debug(TAG, "IMSI: " + imsi);
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
    // 0  = envoi +CFUN=0
    // 1  = attente réponse +CFUN=0
    // 2  = setNetworkMode
    // 3  = getNetworkMode + log
    // 4  = setPreferredMode
    // 5  = getPreferredMode + log
    // 6  = envoi +CGDCONT (APN)
    // 7  = attente réponse +CGDCONT
    // 8  = envoi +CNCFG (APN)
    // 9  = attente réponse +CNCFG
    // 10 = envoi +CFUN=1
    // 11 = attente réponse +CFUN=1
    // 12 = activation LED réseau (CNETLIGHT)
    
    if (budgetExceeded()) return;
    
    switch (subStep) {
        
        case 0:
            Logger::info(TAG, "Configuration réseau...");
            modem.sendAT("+CFUN=0");
            subStep = 1;
            stateCycleCount = 0;  // Reset pour timeout
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
            modem.setNetworkMode(2);
            subStep = 3;
            return;
            
        case 3: {
            uint8_t mode = modem.getNetworkMode();
            Logger::debug(TAG, "Mode réseau: " + String(mode));
            subStep = 4;
            return;
        }
            
        case 4:
            modem.setPreferredMode(MODEM_CATM_NBIOT);
            subStep = 5;
            return;
            
        case 5: {
            uint8_t pref = modem.getPreferredMode();
            Logger::info(TAG, "Mode préféré: " + String(pref));
            subStep = 6;
            return;
        }
            
        case 6:
            modem.sendAT("+CGDCONT=1,\"IP\",\"", CELLULAR_APN, "\"");
            subStep = 7;
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
            return;
            
        case 9:
            if (modem.waitResponse(1000) == 1) {
                Logger::debug(TAG, "APN CNCFG OK");
                Logger::info(TAG, "APN configuré: " + String(CELLULAR_APN));
                subStep = 10;
            } else if (stateCycleCount >= TIMEOUT_AT_RESPONSE) {
                Logger::error(TAG, "Erreur configuration APN (CNCFG)");
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        case 10:
            modem.sendAT("+CFUN=1");
            subStep = 11;
            stateCycleCount = 0;  // Reset pour timeout
            return;
            
        case 11:
            if (modem.waitResponse(1000) == 1) {
                Logger::debug(TAG, "RF activée");
                subStep = 12;
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
            Logger::info(TAG, "✅ Configuration terminée");
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
    // 2 = attente réponse +CNACT (jusqu'à TIMEOUT_BEARER cycles)
    // 3 = vérification GPRS
    // 4 = récupération opérateur
    // 5 = récupération IP
    // 6 = récupération signal + transition CONNECTED
    
    if (budgetExceeded()) return;
    
    switch (subStep) {
        
        case 0: {
            SIM70xxRegStatus s = modem.getRegistrationStatus();
            
            if (s == REG_OK_HOME || s == REG_OK_ROAMING) {
                Logger::info(TAG, "✅ Enregistré sur réseau: " + String(register_info[s]));
                subStep = 1;
                return;
            }
            
            // Timeout global enregistrement réseau
            if (stateCycleCount >= TIMEOUT_NETWORK_WAIT) {
                Logger::error(TAG, "Timeout enregistrement réseau");
                changeState(State::ERROR, "ERROR");
                return;
            }
            
            // Toujours en recherche
            Logger::debug(TAG, "Recherche réseau... (" + String(stateCycleCount) + "/" + String(TIMEOUT_NETWORK_WAIT) + ") - " + String(register_info[s]));
            return;
        }
        
        case 1:
            Logger::debug(TAG, "Activation bearer...");
            modem.sendAT("+CNACT=0,1");
            subStep = 2;
            bearerCycleCount = 0;  // Compteur dédié
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
            Logger::info(TAG, "Signal: " + String(signalQuality) + "/31");
            ready = true;
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
                ready = false;
                changeState(State::NETWORK_WAIT, "NETWORK_WAIT");
                return;
            }
            subStep = 0;  // Reboucle pour le prochain cycle
            return;
    }
}

// -----------------------------------------------------------------------------
// État : ERROR
// -----------------------------------------------------------------------------
void CellularManager::handleError()
{
    // Log une seule fois
    if (stateCycleCount == 1) {
        Logger::error(TAG, "Modem en erreur - attente intervention");
    }
    ready = false;
    // Reste en ERROR, nécessite redémarrage manuel
}

// -----------------------------------------------------------------------------
// Getters (lecture passive)
// -----------------------------------------------------------------------------
bool CellularManager::isReady()
{
    return ready;
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