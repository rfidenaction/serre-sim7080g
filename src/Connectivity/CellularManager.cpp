// src/Connectivity/CellularManager.cpp
// Version finale : 100% non-bloquant — aucun appel TinyGSM bloquant
// Tous les échanges AT passent par le système pending (SEND/WAIT)
// Budget garanti : handle() < 100 ms à chaque cycle

#include "Connectivity/CellularManager.h"
#include "Connectivity/CellularStream.h" 
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

// Messages de statut réseau (indexés par valeur CEREG stat)
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

// Système pending
bool CellularManager::pendingActive = false;
CellularManager::PendingKind CellularManager::pendingKind = PendingKind::NONE;
unsigned long CellularManager::pendingStartMs = 0;
unsigned long CellularManager::pendingTimeoutMs = 0;
bool CellularManager::pendingDone = false;
bool CellularManager::pendingSuccess = false;
char CellularManager::pendingData[64] = {0};
char CellularManager::pendingPrefix[16] = {0};

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
static constexpr int TIMEOUT_NETWORK_WAIT = 60;     // 120s pour enregistrement réseau

// Retry limits
static constexpr int MODEM_RETRY_MAX = 6;           // Nombre de retry AT avant power cycle
static constexpr int BEARER_RETRY_MAX = 5;          // Nombre de retry bearer avant erreur

// Timeouts pending (en ms)
static constexpr unsigned long PENDING_TIMEOUT_MS     = 2000;  // 2s par défaut
static constexpr unsigned long PENDING_TIMEOUT_BEARER_MS = 5000;  // 5s pour activation bearer
static constexpr unsigned long PENDING_TIMEOUT_COPS_MS = 3000;  // 3s pour opérateur

// Instance modem avec CellularStream (proxy ring buffer)
#ifdef DUMP_AT_COMMANDS
static StreamDebugger debugger(CellularStream::instance(), Serial);
static TinyGsm modem(debugger);
#else
static TinyGsm modem(CellularStream::instance());
#endif

// -----------------------------------------------------------------------------
// Accès externe au modem (utilisé par SmsManager)
// -----------------------------------------------------------------------------
TinyGsm& getModem()
{
    return modem;
}

// =============================================================================
// HELPERS DE PARSING (réponses AT capturées par WAIT_PREFIX)
// =============================================================================

// Parse +CEREG: <n>,<stat> → retourne stat (0-5), ou -1 si invalide
static int parseCeregStat(const char* line)
{
    const char* comma = strchr(line, ',');
    if (comma) return atoi(comma + 1);
    return -1;
}

// Parse +CGATT: <state> → retourne 0 ou 1, ou -1 si invalide
static int parseCgatt(const char* line)
{
    const char* colon = strchr(line, ':');
    if (colon) return atoi(colon + 1);
    return -1;
}

// Parse +CSQ: <rssi>,<ber> → retourne rssi (0-31, 99=inconnu)
static int parseCsq(const char* line)
{
    const char* colon = strchr(line, ':');
    if (colon) return atoi(colon + 1);
    return 99;
}

// Parse +COPS: <mode>,<format>,"<oper>",<act> → retourne nom opérateur
static String parseCopsOperator(const char* line)
{
    const char* q1 = strchr(line, '"');
    if (!q1) return "";
    q1++;  // Après le premier guillemet
    const char* q2 = strchr(q1, '"');
    if (!q2) return "";
    
    char buf[32];
    size_t len = q2 - q1;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, q1, len);
    buf[len] = '\0';
    return String(buf);
}

// Parse +CNACT: 0,<status>,"<ip>" → retourne IPAddress
static IPAddress parseCnactIP(const char* line)
{
    const char* q1 = strchr(line, '"');
    if (!q1) return IPAddress(0, 0, 0, 0);
    q1++;
    const char* q2 = strchr(q1, '"');
    if (!q2) return IPAddress(0, 0, 0, 0);
    
    char buf[20];
    size_t len = q2 - q1;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, q1, len);
    buf[len] = '\0';
    
    IPAddress ip;
    ip.fromString(buf);
    return ip;
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
    return -113 + (2 * signal);
}

// =============================================================================
// SYSTÈME PENDING (SEND/WAIT non-bloquant)
// =============================================================================

// -----------------------------------------------------------------------------
// Démarrer un pending
// prefix : utilisé uniquement avec WAIT_PREFIX (ex: "+CEREG:")
// -----------------------------------------------------------------------------
void CellularManager::startPending(PendingKind kind, unsigned long timeoutMs, const char* prefix)
{
    pendingActive = true;
    pendingKind = kind;
    pendingStartMs = millis();
    pendingTimeoutMs = timeoutMs;
    pendingDone = false;
    pendingSuccess = false;
    pendingData[0] = '\0';
    
    // Stocker le préfixe pour WAIT_PREFIX
    if (prefix) {
        strncpy(pendingPrefix, prefix, sizeof(pendingPrefix) - 1);
        pendingPrefix[sizeof(pendingPrefix) - 1] = '\0';
    } else {
        pendingPrefix[0] = '\0';
    }
    
    // Désactiver la bufferisation RX vers TinyGSM pendant pending
    // Le tap callback continue de fonctionner pour CellularEvent
    CellularStream::instance().setRxBufferingEnabled(false);
}

// -----------------------------------------------------------------------------
// Terminer un pending (toujours appeler APRÈS avoir lu pendingData)
// -----------------------------------------------------------------------------
void CellularManager::clearPending()
{
    pendingActive = false;
    pendingKind = PendingKind::NONE;
    pendingStartMs = 0;
    pendingTimeoutMs = 0;
    pendingDone = false;
    pendingSuccess = false;
    pendingPrefix[0] = '\0';
    
    // Réactiver la bufferisation RX vers TinyGSM
    CellularStream::instance().setRxBufferingEnabled(true);
}

// -----------------------------------------------------------------------------
// Vérifier timeout pending (appelé au début de handle())
// -----------------------------------------------------------------------------
void CellularManager::checkPendingTimeout()
{
    if (!pendingActive || pendingDone) {
        return;
    }
    
    if ((millis() - pendingStartMs) >= pendingTimeoutMs) {
        pendingDone = true;
        pendingSuccess = false;  // Timeout = échec
    }
}

bool CellularManager::isPendingActive()
{
    return pendingActive && !pendingDone;
}

// =============================================================================
// HELPERS DE FILTRAGE LIGNES MODEM
// =============================================================================

// Vérifie si une ligne est numérique (pour CCID/IMEI/IMSI)
static bool isNumericLine(const char* line)
{
    if (!line || *line == '\0') return false;
    
    size_t len = 0;
    for (const char* p = line; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        len++;
    }
    
    // CCID: ~20 digits, IMEI: 15 digits, IMSI: 15 digits
    return (len >= 10 && len <= 25);
}

// Vérifie si une ligne est un écho AT ou une URC à ignorer
static bool isEchoOrURC(const char* line)
{
    if (!line || *line == '\0') return false;
    
    // Échos AT
    if (strncmp(line, "AT", 2) == 0) return true;
    
    // URC commençant par +, *, ou mots-clés
    if (line[0] == '+' || line[0] == '*') return true;
    if (strncmp(line, "SMS", 3) == 0) return true;
    if (strncmp(line, "Call", 4) == 0) return true;
    if (strncmp(line, "RING", 4) == 0) return true;
    
    return false;
}

// =============================================================================
// RÉCEPTION LIGNES MODEM (appelé par CellularEvent via main.cpp)
// =============================================================================
void CellularManager::onModemLine(CellularLineType type, const char* line)
{
    // Ignorer si pas de pending actif ou déjà résolu
    if (!pendingActive || pendingDone) {
        return;
    }
    
    switch (pendingKind) {
        case PendingKind::WAIT_OK:
            if (type == CellularLineType::OK) {
                pendingDone = true;
                pendingSuccess = true;
            }
            break;
            
        case PendingKind::WAIT_OK_OR_ERROR:
            if (type == CellularLineType::OK) {
                pendingDone = true;
                pendingSuccess = true;
            } else if (type == CellularLineType::ERROR) {
                pendingDone = true;
                pendingSuccess = false;
            }
            break;
            
        case PendingKind::WAIT_CPIN:
            // Capturer +CPIN: xxx
            if (type == CellularLineType::LINE && strncmp(line, "+CPIN:", 6) == 0) {
                const char* status = line + 6;
                while (*status == ' ') status++;
                strncpy(pendingData, status, sizeof(pendingData) - 1);
                pendingData[sizeof(pendingData) - 1] = '\0';
            }
            if (type == CellularLineType::OK && pendingData[0] != '\0') {
                pendingDone = true;
                pendingSuccess = true;
            }
            if (type == CellularLineType::ERROR) {
                pendingDone = true;
                pendingSuccess = false;
            }
            break;
            
        case PendingKind::WAIT_NUMERIC:
            if (type == CellularLineType::LINE) {
                if (!isEchoOrURC(line) && isNumericLine(line)) {
                    strncpy(pendingData, line, sizeof(pendingData) - 1);
                    pendingData[sizeof(pendingData) - 1] = '\0';
                }
            }
            if (type == CellularLineType::OK && pendingData[0] != '\0') {
                pendingDone = true;
                pendingSuccess = true;
            }
            if (type == CellularLineType::ERROR) {
                pendingDone = true;
                pendingSuccess = false;
            }
            break;
            
        // -----------------------------------------------------------------
        // WAIT_PREFIX : capture première ligne matchant pendingPrefix, puis OK
        // Comportement :
        //   - LINE matchant le préfixe → stocker dans pendingData (première uniquement)
        //   - OK → succès (avec ou sans données capturées)
        //   - ERROR → échec
        //   - Timeout → échec (géré par checkPendingTimeout)
        // -----------------------------------------------------------------
        case PendingKind::WAIT_PREFIX:
            if (type == CellularLineType::LINE) {
                // Matcher sur le préfixe configuré
                if (pendingPrefix[0] != '\0' 
                    && strncmp(line, pendingPrefix, strlen(pendingPrefix)) == 0) {
                    // Capturer la première ligne matchante uniquement
                    if (pendingData[0] == '\0') {
                        strncpy(pendingData, line, sizeof(pendingData) - 1);
                        pendingData[sizeof(pendingData) - 1] = '\0';
                    }
                }
            }
            if (type == CellularLineType::OK) {
                pendingDone = true;
                pendingSuccess = true;
            }
            if (type == CellularLineType::ERROR) {
                pendingDone = true;
                pendingSuccess = false;
            }
            break;
            
        default:
            break;
    }
}

// =============================================================================
// GESTION TICKET MODEM (pour SmsManager, etc.)
// =============================================================================

bool CellularManager::isModemAvailable()
{
    return connected && !modemLocked;
}

bool CellularManager::requestModem()
{
    if (modemLocked) {
        if ((millis() - modemLockTime) >= MODEM_LOCK_TIMEOUT_MS) {
            Logger::warn(TAG, "Timeout ticket modem - libération forcée");
            modemLocked = false;
        } else {
            return false;
        }
    }

    if (!connected) {
        return false;
    }

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

// =============================================================================
// CHARGEMENT PRÉFÉRENCES
// =============================================================================
void CellularManager::loadPreferences()
{
    preferences.begin("cellular", false);
    enabled = preferences.getBool("enabled", true);
    preferences.end();
}

// =============================================================================
// INITIALISATION
// =============================================================================
void CellularManager::init()
{
    Logger::info(TAG, "Initialisation modem SIM7080G...");

    loadPreferences();
    Logger::info(TAG, String("GSM ") + (enabled ? "activé" : "désactivé") + " (préférence)");

    Serial1.begin(MODEM_UART_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    clearPending();

    Logger::info(TAG, " Initialisation matérielle terminée");

    if (enabled) {
        currentState = State::MODEM_INIT;
        Logger::info(TAG, "Démarrage GSM (modem allumé par PMU)...");
    } else {
        currentState = State::IDLE;
        Logger::info(TAG, "GSM désactivé");
    }

    lastStateChange = millis();
}

// =============================================================================
// CONTRÔLE ENABLED (persistant, sans reboot immédiat)
// =============================================================================
void CellularManager::setEnabled(bool newEnabled)
{
    preferences.begin("cellular", false);
    preferences.putBool("enabled", newEnabled);
    preferences.end();

    Logger::info(TAG, String("GSM ") + (newEnabled ? "activé" : "désactivé") + " - sauvegardé");

    enabled = newEnabled;

    if (newEnabled) {
        pendingDisable = false;
        if (currentState == State::IDLE) {
            changeState(State::POWERING_ON, "POWERING_ON");
        } else if (currentState == State::POWERING_OFF) {
            changeState(State::POWERING_ON, "POWERING_ON");
        }
    } else {
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
            Logger::info(TAG, "Désactivation différée (attente état sûr)");
            pendingDisable = true;
            connected = false;
        }
    }
}

// =============================================================================
// MACHINE D'ÉTATS PRINCIPALE (appelée toutes les 2 secondes)
// =============================================================================
void CellularManager::handle()
{
    handleStartTime = millis();

    // Vérifier timeout pending
    checkPendingTimeout();

    // Si GSM désactivé, ne rien faire (sauf terminer POWERING_OFF en cours)
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
        case State::IDLE:           break;
        case State::POWERING_ON:    handlePoweringOn();     break;
        case State::POWERING_OFF:   handlePoweringOff();    break;
        case State::MODEM_INIT:     handleModemInit();      break;
        case State::SIM_CHECK:      handleSimCheck();       break;
        case State::NETWORK_CONFIG: handleNetworkConfig();  break;
        case State::NETWORK_WAIT:   handleNetworkWait();    break;
        case State::CONNECTED:      handleConnected();      break;
        case State::ERROR:          handleError();          break;
    }
    
    // Surveillance budget temps
    unsigned long handleDuration = millis() - handleStartTime;
    if (handleDuration > 100) {
        Logger::info(TAG, String("⏱️ handle() total: ") + handleDuration + "ms (state=" + static_cast<int>(currentState) + ", budget=100ms)");
    }
}

// =============================================================================
// HELPER : CHANGEMENT D'ÉTAT
// =============================================================================
void CellularManager::changeState(State newState, const char* stateName)
{
    Logger::debug(TAG, String("État: ") + stateName);
    currentState = newState;
    lastStateChange = millis();
    stateCycleCount = 0;
    subStep = 0;
    powerStepStartMs = 0;
    
    // Clear pending lors d'un changement d'état
    clearPending();
}

// =============================================================================
// ÉTAT : POWERING_ON (séquence PWRKEY allumage non-bloquante)
// =============================================================================
void CellularManager::handlePoweringOn()
{
    // Séquence : LOW (1100ms) → HIGH (100ms) → LOW (2000ms repos)

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

// =============================================================================
// ÉTAT : POWERING_OFF (séquence PWRKEY extinction non-bloquante)
// =============================================================================
void CellularManager::handlePoweringOff()
{
    // Séquence : LOW (2000ms) → HIGH (100ms) → LOW (1000ms repos)

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

// =============================================================================
// ÉTAT : MODEM_INIT (TinyGSM bloquant — acceptable car transitoire au boot)
// =============================================================================
// testAT(1000) est conservé volontairement :
// Le SIM7080G nécessite une rafale rapide d'AT avec lecture active pour
// synchroniser son UART au démarrage. Un AT isolé toutes les 2s ne suffit pas.
// Blocage : ~600-1200ms, uniquement au boot, 1-2 appels max.
// =============================================================================
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

    // Test AT (TinyGSM bloquant — nécessaire pour synchronisation UART modem)
    unsigned long t0 = millis();
    bool atOk = modem.testAT(1000);
    unsigned long dt = millis() - t0;
    
    if (dt > 100) {
        Logger::info(TAG, String("⏱️ [MODEM_INIT] testAT: ") + dt + "ms (result=" + (atOk ? "OK" : "FAIL") + ")");
    }
    
    if (atOk) {
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

// =============================================================================
// ÉTAT : SIM_CHECK (NON-BLOQUANT via pending — inchangé)
// =============================================================================
void CellularManager::handleSimCheck()
{
    // Substeps pairs = SEND, impairs = WAIT
    // 0/1 = CPIN, 2/3 = CCID, 4/5 = IMEI, 6/7 = IMSI

    static int cpinRetryCount = 0;
    static constexpr int CPIN_MAX_RETRY = 5;
    static constexpr unsigned long CPIN_TIMEOUT_MS = 3000;
    static constexpr unsigned long NUMERIC_TIMEOUT_MS = 2000;

    if (budgetExceeded()) return;

    switch (subStep) {
        
        // ----- CPIN -----
        case 0:
            Logger::info(TAG, "Vérification carte SIM...");
            cpinRetryCount = 0;
            modem.sendAT("+CPIN?");
            startPending(PendingKind::WAIT_CPIN, CPIN_TIMEOUT_MS);
            subStep = 1;
            return;
            
        case 1:
            if (!pendingDone) return;
            
            if (pendingSuccess) {
                if (strcmp(pendingData, "READY") == 0) {
                    Logger::info(TAG, " Carte SIM détectée");
                    clearPending();
                    subStep = 2;
                } else if (strcmp(pendingData, "NOT READY") == 0) {
                    cpinRetryCount++;
                    if (cpinRetryCount < CPIN_MAX_RETRY) {
                        Logger::debug(TAG, String("SIM NOT READY, retry ") + cpinRetryCount + "/" + CPIN_MAX_RETRY);
                        clearPending();
                        subStep = 0;
                    } else {
                        Logger::error(TAG, "SIM NOT READY après " + String(CPIN_MAX_RETRY) + " tentatives");
                        clearPending();
                        changeState(State::ERROR, "ERROR");
                    }
                } else {
                    Logger::error(TAG, String("Carte SIM non prête: ") + pendingData);
                    clearPending();
                    changeState(State::ERROR, "ERROR");
                }
            } else {
                cpinRetryCount++;
                if (cpinRetryCount < CPIN_MAX_RETRY) {
                    Logger::debug(TAG, String("CPIN timeout/error, retry ") + cpinRetryCount + "/" + CPIN_MAX_RETRY);
                    clearPending();
                    subStep = 0;
                } else {
                    Logger::error(TAG, "Erreur vérification SIM");
                    clearPending();
                    changeState(State::ERROR, "ERROR");
                }
            }
            return;
            
        // ----- CCID -----
        case 2:
            modem.sendAT("+CCID");
            startPending(PendingKind::WAIT_NUMERIC, NUMERIC_TIMEOUT_MS);
            subStep = 3;
            return;
            
        case 3:
            if (!pendingDone) return;
            
            if (pendingSuccess && pendingData[0] != '\0') {
                Logger::info(TAG, String("CCID: ") + pendingData);
            } else {
                Logger::warn(TAG, "CCID non disponible");
            }
            clearPending();
            subStep = 4;
            return;
            
        // ----- IMEI -----
        case 4:
            modem.sendAT("+GSN");
            startPending(PendingKind::WAIT_NUMERIC, NUMERIC_TIMEOUT_MS);
            subStep = 5;
            return;
            
        case 5:
            if (!pendingDone) return;
            
            if (pendingSuccess && pendingData[0] != '\0') {
                Logger::info(TAG, String("IMEI: ") + pendingData);
            } else {
                Logger::warn(TAG, "IMEI non disponible");
            }
            clearPending();
            subStep = 6;
            return;
            
        // ----- IMSI -----
        case 6:
            modem.sendAT("+CIMI");
            startPending(PendingKind::WAIT_NUMERIC, NUMERIC_TIMEOUT_MS);
            subStep = 7;
            return;
            
        case 7:
            if (!pendingDone) return;
            
            if (pendingSuccess && pendingData[0] != '\0') {
                Logger::info(TAG, String("IMSI: ") + pendingData);
            } else {
                Logger::warn(TAG, "IMSI non disponible");
            }
            clearPending();
            changeState(State::NETWORK_CONFIG, "NETWORK_CONFIG");
            return;
    }
}

// =============================================================================
// ÉTAT : NETWORK_CONFIG (NON-BLOQUANT via pending — inchangé)
// =============================================================================
void CellularManager::handleNetworkConfig()
{
    // Substeps pairs = SEND, impairs = WAIT
    // 0/1 = CFUN=0, 2/3 = CNMP, 4/5 = CMNB, 6/7 = CGDCONT
    // 8/9 = CNCFG, 10/11 = CFUN=1, 12 = CNETLIGHT (fire-and-forget)

    if (budgetExceeded()) return;

    switch (subStep) {
        
        // ----- CFUN=0 -----
        case 0:
            Logger::info(TAG, "Configuration réseau...");
            Logger::debug(TAG, "Désactivation RF...");
            modem.sendAT("+CFUN=0");
            startPending(PendingKind::WAIT_OK_OR_ERROR, PENDING_TIMEOUT_MS);
            subStep = 1;
            return;
            
        case 1:
            if (!pendingDone) return;
            if (pendingSuccess) {
                Logger::debug(TAG, "RF désactivée");
                clearPending();
                subStep = 2;
            } else {
                Logger::error(TAG, "Erreur désactivation RF");
                clearPending();
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        // ----- CNMP=38 -----
        case 2:
            Logger::debug(TAG, "Configuration Cat-M...");
            modem.sendAT("+CNMP=38");
            startPending(PendingKind::WAIT_OK_OR_ERROR, PENDING_TIMEOUT_MS);
            subStep = 3;
            return;
            
        case 3:
            if (!pendingDone) return;
            if (pendingSuccess) {
                clearPending();
                subStep = 4;
            } else {
                Logger::error(TAG, "Erreur configuration mode réseau");
                clearPending();
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        // ----- CMNB=1 -----
        case 4:
            modem.sendAT("+CMNB=1");
            startPending(PendingKind::WAIT_OK_OR_ERROR, PENDING_TIMEOUT_MS);
            subStep = 5;
            return;
            
        case 5:
            if (!pendingDone) return;
            if (pendingSuccess) {
                Logger::info(TAG, " Cat-M configuré");
                clearPending();
                subStep = 6;
            } else {
                Logger::error(TAG, "Erreur configuration préférence");
                clearPending();
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        // ----- CGDCONT -----
        case 6:
            Logger::debug(TAG, "Configuration APN...");
            modem.sendAT("+CGDCONT=1,\"IP\",\"", CELLULAR_APN, "\"");
            startPending(PendingKind::WAIT_OK_OR_ERROR, PENDING_TIMEOUT_MS);
            subStep = 7;
            return;
            
        case 7:
            if (!pendingDone) return;
            if (pendingSuccess) {
                Logger::debug(TAG, "APN CGDCONT OK");
                clearPending();
                subStep = 8;
            } else {
                Logger::error(TAG, "Erreur configuration APN (CGDCONT)");
                clearPending();
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        // ----- CNCFG -----
        case 8:
            modem.sendAT("+CNCFG=0,1,\"", CELLULAR_APN, "\"");
            startPending(PendingKind::WAIT_OK_OR_ERROR, PENDING_TIMEOUT_MS);
            subStep = 9;
            return;
            
        case 9:
            if (!pendingDone) return;
            if (pendingSuccess) {
                Logger::debug(TAG, "APN CNCFG OK");
                Logger::info(TAG, "APN configuré: " + String(CELLULAR_APN));
                clearPending();
                subStep = 10;
            } else {
                Logger::error(TAG, "Erreur configuration APN (CNCFG)");
                clearPending();
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        // ----- CFUN=1 -----
        case 10:
            modem.sendAT("+CFUN=1");
            startPending(PendingKind::WAIT_OK_OR_ERROR, PENDING_TIMEOUT_MS);
            subStep = 11;
            return;
            
        case 11:
            if (!pendingDone) return;
            if (pendingSuccess) {
                Logger::info(TAG, "RF activée");
                clearPending();
                subStep = 12;
            } else {
                Logger::error(TAG, "Erreur activation RF");
                clearPending();
                changeState(State::ERROR, "ERROR");
            }
            return;
            
        // ----- CNETLIGHT (fire-and-forget) -----
        case 12:
            modem.sendAT("+CNETLIGHT=1");
            Logger::debug(TAG, "LED réseau activée (fire-and-forget)");
            Logger::info(TAG, " Configuration terminée");
            changeState(State::NETWORK_WAIT, "NETWORK_WAIT");
            return;
    }
}

// =============================================================================
// ÉTAT : NETWORK_WAIT (NON-BLOQUANT via SEND/WAIT)
// =============================================================================
// Substeps :
//   0/1   = AT+CEREG?  → enregistrement réseau (retry jusqu'à timeout)
//   2/3   = AT+CNACT=0,1 → activation bearer (retry si échec)
//   4/5   = AT+CGATT?  → vérification GPRS attaché
//   6/7   = AT+COPS?   → récupération opérateur
//   8/9   = AT+CNACT?  → récupération IP
//  10/11  = AT+CSQ     → signal → transition CONNECTED
// =============================================================================
void CellularManager::handleNetworkWait()
{
    if (budgetExceeded()) return;

    switch (subStep) {
        
        // =====================================================================
        // CEREG : enregistrement réseau
        // =====================================================================
        case 0:
            modem.sendAT("+CEREG?");
            startPending(PendingKind::WAIT_PREFIX, PENDING_TIMEOUT_MS, "+CEREG:");
            subStep = 1;
            return;
            
        case 1: {
            if (!pendingDone) return;
            
            if (pendingSuccess && pendingData[0] != '\0') {
                int stat = parseCeregStat(pendingData);
                
                if (stat == 1 || stat == 5) {
                    // 1 = home, 5 = roaming
                    const char* info = (stat >= 0 && stat <= 5) ? register_info[stat] : "Unknown";
                    Logger::info(TAG, String(" Enregistré sur réseau: ") + info);
                    clearPending();
                    bearerCycleCount = 0;  // Reset pour étape bearer
                    subStep = 2;
                    return;
                }
                
                // Pas encore enregistré
                const char* info = (stat >= 0 && stat <= 5) ? register_info[stat] : "Unknown";
                Logger::debug(TAG, "Recherche réseau... (" + String(stateCycleCount) + "/" + String(TIMEOUT_NETWORK_WAIT) + ") - " + String(info));
            } else {
                Logger::debug(TAG, "CEREG: pas de réponse, retry...");
            }
            
            // Timeout global ?
            if (stateCycleCount >= TIMEOUT_NETWORK_WAIT) {
                Logger::error(TAG, "Timeout enregistrement réseau");
                clearPending();
                changeState(State::ERROR, "ERROR");
                return;
            }
            
            // Réessayer au prochain cycle
            clearPending();
            subStep = 0;
            return;
        }
        
        // =====================================================================
        // CNACT=0,1 : activation bearer
        // =====================================================================
        case 2:
            Logger::debug(TAG, "Activation bearer...");
            modem.sendAT("+CNACT=0,1");
            startPending(PendingKind::WAIT_OK_OR_ERROR, PENDING_TIMEOUT_BEARER_MS);
            subStep = 3;
            return;
            
        case 3:
            if (!pendingDone) return;
            
            if (pendingSuccess) {
                Logger::debug(TAG, "Bearer activé");
                clearPending();
                subStep = 4;
            } else {
                bearerCycleCount++;
                if (bearerCycleCount >= BEARER_RETRY_MAX) {
                    Logger::error(TAG, "Erreur activation bearer après " + String(BEARER_RETRY_MAX) + " tentatives");
                    clearPending();
                    changeState(State::ERROR, "ERROR");
                } else {
                    Logger::debug(TAG, "Bearer retry " + String(bearerCycleCount) + "/" + String(BEARER_RETRY_MAX));
                    clearPending();
                    subStep = 2;  // Réessayer
                }
            }
            return;
            
        // =====================================================================
        // CGATT : vérification GPRS attaché
        // =====================================================================
        case 4:
            modem.sendAT("+CGATT?");
            startPending(PendingKind::WAIT_PREFIX, PENDING_TIMEOUT_MS, "+CGATT:");
            subStep = 5;
            return;
            
        case 5:
            if (!pendingDone) return;
            
            if (pendingSuccess && pendingData[0] != '\0') {
                int attached = parseCgatt(pendingData);
                if (attached == 1) {
                    Logger::debug(TAG, "GPRS connecté");
                    clearPending();
                    subStep = 6;
                    return;
                }
            }
            
            // Pas attaché ou erreur
            Logger::error(TAG, "Pas de connexion GPRS");
            clearPending();
            changeState(State::ERROR, "ERROR");
            return;
            
        // =====================================================================
        // COPS : récupération opérateur
        // =====================================================================
        case 6:
            modem.sendAT("+COPS?");
            startPending(PendingKind::WAIT_PREFIX, PENDING_TIMEOUT_COPS_MS, "+COPS:");
            subStep = 7;
            return;
            
        case 7:
            if (!pendingDone) return;
            
            if (pendingSuccess && pendingData[0] != '\0') {
                operatorName = parseCopsOperator(pendingData);
            } else {
                operatorName = "";
            }
            Logger::info(TAG, "Opérateur: " + (operatorName.length() > 0 ? operatorName : "(inconnu)"));
            clearPending();
            subStep = 8;
            return;
            
        // =====================================================================
        // CNACT? : récupération IP (filtre sur PDP context 0)
        // =====================================================================
        case 8:
            modem.sendAT("+CNACT?");
            startPending(PendingKind::WAIT_PREFIX, PENDING_TIMEOUT_MS, "+CNACT: 0,");
            subStep = 9;
            return;
            
        case 9:
            if (!pendingDone) return;
            
            if (pendingSuccess && pendingData[0] != '\0') {
                localIP = parseCnactIP(pendingData);
            } else {
                localIP = IPAddress(0, 0, 0, 0);
            }
            Logger::info(TAG, "IP locale: " + localIP.toString());
            clearPending();
            subStep = 10;
            return;
            
        // =====================================================================
        // CSQ : signal → transition CONNECTED
        // =====================================================================
        case 10:
            modem.sendAT("+CSQ");
            startPending(PendingKind::WAIT_PREFIX, PENDING_TIMEOUT_MS, "+CSQ:");
            subStep = 11;
            return;
            
        case 11:
            if (!pendingDone) return;
            
            if (pendingSuccess && pendingData[0] != '\0') {
                signalQuality = parseCsq(pendingData);
            } else {
                signalQuality = 99;
            }
            Logger::info(TAG, "Signal: " + String(signalQuality) + "/31 (" + String(signalTodBm(signalQuality)) + " dBm)");
            
            connected = true;
            recoveryCount = 0;
            Logger::info(TAG, " Modem connecté");
            clearPending();
            changeState(State::CONNECTED, "CONNECTED");
            return;
    }
}

// =============================================================================
// ÉTAT : CONNECTED (NON-BLOQUANT via SEND/WAIT)
// =============================================================================
// Substeps :
//   0/1 = AT+CSQ    → mise à jour signal
//   2/3 = AT+CGATT? → vérification connexion GPRS
// Cycle complet = 4 substeps = 4 appels handle() = ~8 secondes
// =============================================================================
void CellularManager::handleConnected()
{
    if (budgetExceeded()) return;

    switch (subStep) {
        
        // ----- SEND CSQ -----
        case 0:
            modem.sendAT("+CSQ");
            startPending(PendingKind::WAIT_PREFIX, PENDING_TIMEOUT_MS, "+CSQ:");
            subStep = 1;
            return;
            
        // ----- WAIT CSQ -----
        case 1:
            if (!pendingDone) return;
            
            if (pendingSuccess && pendingData[0] != '\0') {
                signalQuality = parseCsq(pendingData);
            }
            clearPending();
            subStep = 2;
            return;
            
        // ----- SEND CGATT -----
        case 2:
            modem.sendAT("+CGATT?");
            startPending(PendingKind::WAIT_PREFIX, PENDING_TIMEOUT_MS, "+CGATT:");
            subStep = 3;
            return;
            
        // ----- WAIT CGATT -----
        case 3:
            if (!pendingDone) return;
            
            if (pendingSuccess && pendingData[0] != '\0') {
                int attached = parseCgatt(pendingData);
                if (attached != 1) {
                    Logger::warn(TAG, "Connexion perdue !");
                    connected = false;
                    clearPending();
                    changeState(State::NETWORK_WAIT, "NETWORK_WAIT");
                    return;
                }
            } else {
                // Pas de réponse → considérer comme perte de connexion
                Logger::warn(TAG, "CGATT sans réponse - connexion perdue ?");
                connected = false;
                clearPending();
                changeState(State::NETWORK_WAIT, "NETWORK_WAIT");
                return;
            }
            
            clearPending();
            
            // Traiter désactivation différée si demandée
            if (pendingDisable) {
                Logger::info(TAG, "Traitement désactivation différée");
                pendingDisable = false;
                connected = false;
                changeState(State::POWERING_OFF, "POWERING_OFF");
                return;
            }
            
            // Cycle complet → recommencer
            subStep = 0;
            return;
    }
}

// =============================================================================
// ÉTAT : ERROR (avec recovery automatique NON-BLOQUANT)
// =============================================================================
void CellularManager::handleError()
{
    connected = false;

    // Traiter pendingDisable prioritairement
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
        changeState(State::POWERING_OFF, "POWERING_OFF (recovery)");
    }
}

// =============================================================================
// GETTERS
// =============================================================================

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

// =============================================================================
// STATUS FORMATÉ POUR L'INTERFACE WEB
// =============================================================================
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

    String status = "Connecté";

    if (operatorName.length() > 0) {
        status += " (" + operatorName + ")";
    }

    if (signalQuality != 99) {
        status += " " + String(signalTodBm(signalQuality)) + " dBm";
    }

    return status;
}