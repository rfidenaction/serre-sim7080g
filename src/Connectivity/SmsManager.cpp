// src/Connectivity/SmsManager.cpp
#include "Connectivity/SmsManager.h"
#include "Connectivity/CellularManager.h"
#include "Config/NetworkConfig.h"
#include "Utils/Logger.h"
#include <TinyGsmClient.h>

// Tag pour logs
static const char* TAG = "SMS";

// Accès au modem (déclaré dans CellularManager.cpp)
extern TinyGsm& getModem();

// -----------------------------------------------------------------------------
// Membres statiques
// -----------------------------------------------------------------------------
std::vector<SmsManager::SmsItem> SmsManager::queue;
SmsManager::State SmsManager::currentState = State::IDLE;
int SmsManager::globalRetryCount = 0;
unsigned long SmsManager::bootTime = 0;
bool SmsManager::startupSmsSent = false;
bool SmsManager::modemAcquired = false;

// Compteurs pour statistiques
int SmsManager::cmgfAttempts = 0;
int SmsManager::cmgsAttempts = 0;
int SmsManager::textAttempts = 0;

// -----------------------------------------------------------------------------
// Initialisation
// -----------------------------------------------------------------------------
void SmsManager::init()
{
    queue.reserve(MAX_QUEUE_SIZE);
    currentState = State::IDLE;
    globalRetryCount = 0;
    bootTime = millis();
    startupSmsSent = false;
    modemAcquired = false;
    cmgfAttempts = 0;
    cmgsAttempts = 0;
    textAttempts = 0;
    
    Logger::info(TAG, "SmsManager initialisé");
    Logger::debug(TAG, "Destinataires configurés: " + String(SMS_NUMBERS_COUNT));
}

// -----------------------------------------------------------------------------
// Envoyer le SMS de bienvenue
// -----------------------------------------------------------------------------
void SmsManager::sendStartupSms()
{
    String message = "Modem connecte - Reseau: " + CellularManager::getOperator() +
                     " - IP: " + CellularManager::getLocalIP().toString() +
                     " - Signal: " + String(CellularManager::getSignalQuality()) + "/31";
    
    alert(message);
    startupSmsSent = true;
    Logger::info(TAG, "SMS de bienvenue ajouté à la file");
}

// -----------------------------------------------------------------------------
// Recommencer un cycle complet
// -----------------------------------------------------------------------------
void SmsManager::restartSmsCycle()
{
    globalRetryCount++;
    
    if (globalRetryCount >= MAX_GLOBAL_RETRIES) {
        Logger::error(TAG, "Abandon après " + String(MAX_GLOBAL_RETRIES) + " cycles");
        finishCurrentSms(false);
    } else {
        Logger::warn(TAG, "Retry cycle " + String(globalRetryCount + 1) + "/" + String(MAX_GLOBAL_RETRIES));
        currentState = State::CMGF_TRY1;
    }
}

// -----------------------------------------------------------------------------
// Terminer le SMS en cours
// -----------------------------------------------------------------------------
void SmsManager::finishCurrentSms(bool success)
{
    if (!queue.empty()) {
        if (success) {
            int totalAttempts = cmgfAttempts + cmgsAttempts + textAttempts;
            Logger::info(TAG, "✅ SMS envoyé à " + queue.front().number + 
                         " (CMGF:" + String(cmgfAttempts) + 
                         " CMGS:" + String(cmgsAttempts) + 
                         " TEXT:" + String(textAttempts) + 
                         " Total:" + String(totalAttempts) + ")");
        } else {
            Logger::error(TAG, "❌ SMS échoué pour " + queue.front().number +
                          " après " + String(globalRetryCount) + " cycles complets");
        }
        queue.erase(queue.begin());
    }
    
    // Libérer le modem
    if (modemAcquired) {
        CellularManager::freeModem();
        modemAcquired = false;
        Logger::debug(TAG, "Modem libéré");
    }
    
    // Reset machine d'états et compteurs
    currentState = State::IDLE;
    globalRetryCount = 0;
    cmgfAttempts = 0;
    cmgsAttempts = 0;
    textAttempts = 0;
}

// -----------------------------------------------------------------------------
// Handle — Machine d'états (appelée toutes les 2s)
// -----------------------------------------------------------------------------
void SmsManager::handle()
{
    // Attendre 60s après le boot
    if ((millis() - bootTime) < STARTUP_DELAY_MS) {
        return;
    }
    
    // Envoyer SMS de bienvenue (une seule fois)
    if (!startupSmsSent && CellularManager::isReady()) {
        sendStartupSms();
    }
    
    // Rien à envoyer
    if (queue.empty()) {
        return;
    }
    
    // Si on n'a pas le ticket, le demander
    if (!modemAcquired) {
        if (!CellularManager::requestModem()) {
            return;  // Modem pas disponible, attendre
        }
        modemAcquired = true;
        Logger::debug(TAG, "Modem acquis pour envoi SMS");
    }
    
    TinyGsm& modem = getModem();
    
    switch (currentState) {
        
        // -----------------------------------------------------------------
        // IDLE - Démarrage envoi
        // -----------------------------------------------------------------
        case State::IDLE:
            Logger::info(TAG, "Début envoi SMS à " + queue.front().number);
            Logger::debug(TAG, "Message: " + queue.front().message);
            currentState = State::CMGF_TRY1;
            globalRetryCount = 0;
            cmgfAttempts = 0;
            cmgsAttempts = 0;
            textAttempts = 0;
            break;
        
        // -----------------------------------------------------------------
        // AT+CMGF=1 - Tentative 1
        // -----------------------------------------------------------------
        case State::CMGF_TRY1:
            {
                cmgfAttempts++;
                Logger::debug(TAG, "AT+CMGF=1 (essai " + String(cmgfAttempts) + ")");
                modem.sendAT("+CMGF=1");
                int result = modem.waitResponse(TIMEOUT_SHORT);
                
                if (result == 1) {
                    Logger::debug(TAG, "Mode texte OK");
                    currentState = State::CMGS_TRY1;
                } else {
                    Logger::debug(TAG, "CMGF timeout, retry prochain cycle");
                    currentState = State::CMGF_TRY2;
                }
            }
            break;
        
        // -----------------------------------------------------------------
        // AT+CMGF=1 - Tentative 2
        // -----------------------------------------------------------------
        case State::CMGF_TRY2:
            {
                cmgfAttempts++;
                Logger::debug(TAG, "AT+CMGF=1 (essai " + String(cmgfAttempts) + ")");
                modem.sendAT("+CMGF=1");
                int result = modem.waitResponse(TIMEOUT_SHORT);
                
                if (result == 1) {
                    Logger::debug(TAG, "Mode texte OK");
                    currentState = State::CMGS_TRY1;
                } else {
                    Logger::warn(TAG, "CMGF échec après " + String(cmgfAttempts) + " essais");
                    restartSmsCycle();
                }
            }
            break;
        
        // -----------------------------------------------------------------
        // AT+CMGS - Tentative 1
        // -----------------------------------------------------------------
        case State::CMGS_TRY1:
            {
                cmgsAttempts++;
                Logger::debug(TAG, "AT+CMGS (essai " + String(cmgsAttempts) + ")");
                modem.sendAT("+CMGS=\"", queue.front().number.c_str(), "\"");
                int result = modem.waitResponse(TIMEOUT_SHORT, ">");
                
                if (result == 1) {
                    Logger::debug(TAG, "Prompt > reçu");
                    currentState = State::TEXT;
                } else {
                    Logger::debug(TAG, "CMGS timeout, retry prochain cycle");
                    currentState = State::CMGS_TRY2;
                }
            }
            break;
        
        // -----------------------------------------------------------------
        // AT+CMGS - Tentative 2
        // -----------------------------------------------------------------
        case State::CMGS_TRY2:
            {
                cmgsAttempts++;
                Logger::debug(TAG, "AT+CMGS (essai " + String(cmgsAttempts) + ")");
                modem.sendAT("+CMGS=\"", queue.front().number.c_str(), "\"");
                int result = modem.waitResponse(TIMEOUT_SHORT, ">");
                
                if (result == 1) {
                    Logger::debug(TAG, "Prompt > reçu");
                    currentState = State::TEXT;
                } else {
                    Logger::warn(TAG, "CMGS échec après " + String(cmgsAttempts) + " essais");
                    restartSmsCycle();
                }
            }
            break;
        
        // -----------------------------------------------------------------
        // TEXT - Envoi du message + Ctrl+Z
        // -----------------------------------------------------------------
        case State::TEXT:
            {
                textAttempts++;
                Logger::debug(TAG, "Envoi texte + Ctrl+Z (essai " + String(textAttempts) + ")");
                Serial1.print(queue.front().message);
                Serial1.write(26);  // Ctrl+Z
                
                int result = modem.waitResponse(TIMEOUT_TEXT, "+CMGS:");
                
                if (result == 1) {
                    finishCurrentSms(true);
                } else {
                    Logger::warn(TAG, "TEXT timeout (essai " + String(textAttempts) + ")");
                    restartSmsCycle();
                }
            }
            break;
        
        // -----------------------------------------------------------------
        // États finaux (ne devraient pas rester ici)
        // -----------------------------------------------------------------
        case State::DONE:
        case State::ERROR:
            currentState = State::IDLE;
            break;
    }
}

// -----------------------------------------------------------------------------
// Alert — Envoie un message à tous les numéros configurés
// -----------------------------------------------------------------------------
void SmsManager::alert(const String& message)
{
    for (size_t i = 0; i < SMS_NUMBERS_COUNT; i++) {
        send(SMS_NUMBERS[i], message);
    }
}

// -----------------------------------------------------------------------------
// Send — Ajoute un SMS à la file d'attente
// -----------------------------------------------------------------------------
void SmsManager::send(const char* number, const String& message)
{
    if (queue.size() >= MAX_QUEUE_SIZE) {
        Logger::warn(TAG, "File pleine, suppression du plus ancien");
        queue.erase(queue.begin());
    }
    
    SmsItem item;
    item.number = String(number);
    item.message = message;
    queue.push_back(item);
    
    Logger::debug(TAG, "SMS en file pour " + item.number + " (" + String(queue.size()) + " en attente)");
}

// -----------------------------------------------------------------------------
// QueueSize — Nombre de SMS en attente
// -----------------------------------------------------------------------------
size_t SmsManager::queueSize()
{
    return queue.size();
}

// -----------------------------------------------------------------------------
// IsBusy — Envoi en cours
// -----------------------------------------------------------------------------
bool SmsManager::isBusy()
{
    return currentState != State::IDLE;
}