// src/Connectivity/SmsManager.h
#pragma once
#include <Arduino.h>
#include <vector>

class SmsManager {
public:
    // Cycle de vie
    static void init();
    static void handle();   // Appelé par TaskManager toutes les 2s
    
    // Envoi de SMS
    static void alert(const String& message);                    // Envoie à tous les numéros configurés
    static void send(const char* number, const String& message); // Envoi à un numéro spécifique
    
    // Monitoring
    static size_t queueSize();
    static bool isBusy();   // Envoi en cours
    
private:
    // Structure d'un SMS en attente
    struct SmsItem {
        String number;
        String message;
    };
    
    // États de la machine d'envoi SMS
    enum class State {
        IDLE,           // Attente, vérifie si SMS en file
        CMGF_TRY1,      // AT+CMGF=1 tentative 1
        CMGF_TRY2,      // AT+CMGF=1 tentative 2
        CMGS_TRY1,      // AT+CMGS tentative 1
        CMGS_TRY2,      // AT+CMGS tentative 2
        TEXT,           // Envoi texte + Ctrl+Z
        DONE,           // Succès
        ERROR           // Échec définitif
    };
    
    // Configuration
    static constexpr size_t MAX_QUEUE_SIZE = 10;
    static constexpr int MAX_GLOBAL_RETRIES = 10;           // 10 cycles complets max
    static constexpr unsigned long TIMEOUT_SHORT = 100;     // 100ms pour CMGF/CMGS
    static constexpr unsigned long TIMEOUT_TEXT = 1500;     // 1500ms pour TEXT (temporaire)
    static constexpr unsigned long STARTUP_DELAY_MS = 60000; // 60s après boot
    
    // File d'attente
    static std::vector<SmsItem> queue;
    
    // Machine d'états
    static State currentState;
    static int globalRetryCount;        // Compteur de cycles complets
    static unsigned long bootTime;      // Timestamp du boot
    static bool startupSmsSent;         // SMS de bienvenue envoyé
    static bool modemAcquired;          // Ticket modem obtenu
    
    // Compteurs pour statistiques
    static int cmgfAttempts;            // Nombre d'essais CMGF
    static int cmgsAttempts;            // Nombre d'essais CMGS
    static int textAttempts;            // Nombre d'essais TEXT
    
    // Helpers
    static void restartSmsCycle();      // Recommencer un cycle complet
    static void finishCurrentSms(bool success);  // Terminer le SMS en cours
    static void sendStartupSms();       // Envoyer le SMS de bienvenue
};