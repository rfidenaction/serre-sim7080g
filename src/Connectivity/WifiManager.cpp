// src/Connectivity/WiFiManager.cpp

#include <Arduino.h>
#include "Connectivity/WiFiManager.h"
#include <WiFi.h>
#include "Config/NetworkConfig.h"
#include "Utils/Logger.h"

// =============================================================================
// Variables statiques
// =============================================================================

// Machine d'états
WiFiManager::State WiFiManager::state = WiFiManager::State::AP_CONFIG;

// Interlocks zone boot — supprimés (la machine d'états est structurellement sûre)

// États runtime
bool WiFiManager::staEnabled    = true;
bool WiFiManager::staConnected  = false;
bool WiFiManager::apEnabled     = false;  // false tant que softAP() n'a pas réussi

// Timing
unsigned long WiFiManager::apStabilizeStartMs = 0;
unsigned long WiFiManager::connectStartMs     = 0;
unsigned long WiFiManager::retryStartMs       = 0;
unsigned long WiFiManager::lastConnectLogMs   = 0;

// Flags de demandes externes
bool WiFiManager::apDisableRequested  = false;
bool WiFiManager::staChangeRequested  = false;
bool WiFiManager::staChangeValue      = false;

// Stockage persistant
Preferences WiFiManager::preferences;

// =============================================================================
// Initialisation — active la radio WiFi (lwIP) mais ne démarre ni AP ni STA
// =============================================================================
void WiFiManager::init()
{
    loadPreferences();

    // WiFi.mode() doit être appelé ici (dans setup()) car :
    // - Il initialise lwIP (pile TCP/IP), prérequis pour WebServer::init()
    // - ~50ms, acceptable dans setup()
    // - La suite (softAPConfig, softAP, STA) est gérée par la machine d'états
    wifi_mode_t mode = staEnabled ? WIFI_AP_STA : WIFI_AP;
    WiFi.mode(mode);

    state = State::AP_CONFIG;

    Logger::info("WiFi", String("init — STA ") + (staEnabled ? "activé" : "désactivé")
                 + ", mode " + (staEnabled ? "AP_STA" : "AP"));
}

// =============================================================================
// Chargement préférences
// =============================================================================
void WiFiManager::loadPreferences()
{
    preferences.begin("wifi", false);
    staEnabled = preferences.getBool("sta", true);
    preferences.end();
}

// =============================================================================
// Transition d'état avec log
// =============================================================================
void WiFiManager::changeState(State newState)
{
    // Noms lisibles pour le log
    static const char* names[] = {
        "AP_CONFIG", "AP_START", "AP_STABILIZE",
        "STA_CONFIG", "STA_BEGIN",
        "STA_CONNECTING", "STA_CONNECTED", "STA_DISCONNECT",
        "STA_WAIT_RETRY", "IDLE"
    };

    int oldIdx = static_cast<int>(state);
    int newIdx = static_cast<int>(newState);

    Logger::info("WiFi", String(names[oldIdx]) + " -> " + names[newIdx]);

    state = newState;
}

// =============================================================================
// Traduction wl_status_t en chaîne lisible (diagnostic)
// =============================================================================
const char* WiFiManager::wlStatusToString(wl_status_t status)
{
    switch (status) {
        case WL_IDLE_STATUS:     return "IDLE";
        case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
        case WL_CONNECTED:       return "CONNECTED";
        case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED:    return "DISCONNECTED";
        default:                 return "UNKNOWN";
    }
}

// =============================================================================
// Application des demandes externes (uniquement en états stables)
// =============================================================================
void WiFiManager::applyPendingRequests()
{
    // Priorité 1 : changement STA → reboot (domine tout)
    if (staChangeRequested) {
        staChangeRequested = false;

        Logger::info("WiFi", String("application setSTAEnabled(") + (staChangeValue ? "true" : "false") + ") → reboot");

        preferences.begin("wifi", false);
        preferences.putBool("sta", staChangeValue);
        preferences.end();

        delay(100);  // Laisser le temps au flash d'écrire
        ESP.restart();
        // Ne revient jamais ici
    }

    // Priorité 2 : coupure AP à chaud
    if (apDisableRequested) {
        apDisableRequested = false;

        Logger::info("WiFi", "application disableAP()");

        WiFi.softAPdisconnect(true);  // ~14ms

        // Changer le mode selon que STA est actif ou non
        if (staEnabled) {
            WiFi.mode(WIFI_STA);  // ~0.04ms
        } else {
            WiFi.mode(WIFI_OFF);
        }

        apEnabled = false;
        Logger::info("WiFi", "AP éteint à chaud (réactivation uniquement par reboot)");
    }
}

// =============================================================================
// Machine d'états principale — appelée par TaskManager toutes les 250ms
// =============================================================================
void WiFiManager::handle()
{
    switch (state) {

    // =========================================================================
    // ZONE BOOT — traversée une seule fois, jamais revisitée
    // WiFi.mode() est appelé dans init(), on démarre directement à AP_CONFIG
    // =========================================================================

    case State::AP_CONFIG: {
        // WiFi.softAPConfig() : ~1ms
        bool ok = WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET);

        if (!ok) {
            Logger::error("WiFi", "softAPConfig() ERREUR");
        }

        changeState(State::AP_START);
        break;
    }

    case State::AP_START: {
        // WiFi.softAP() : ~0.1ms (box présente) ou ~725ms (box absente)
        // ⚠️ C'est le SEUL appel potentiellement long (>100ms)
        // Il n'est exécuté qu'UNE SEULE FOIS dans toute la vie du programme
        unsigned long t0 = millis();
        bool ok = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
        unsigned long dt = millis() - t0;

        apEnabled = ok;  // Seule source de vérité pour l'état AP

        if (ok) {
            Logger::info("WiFi", "AP démarré — IP: " + WiFi.softAPIP().toString()
                         + " (" + String(dt) + "ms)");
        } else {
            Logger::error("WiFi", "softAP() ERREUR — AP non disponible (" + String(dt) + "ms)");
        }

        // Aiguillage : STA activé ou pas ?
        if (staEnabled) {
            apStabilizeStartMs = millis();
            changeState(State::AP_STABILIZE);
        } else {
            changeState(State::IDLE);
        }
        break;
    }

    case State::AP_STABILIZE: {
        // Attente non-bloquante de stabilisation du driver AP+STA
        // Reproduit les delays historiques (100ms + 50ms + marge) en machine d'états
        // Sans ce délai, le premier WiFi.begin() échoue systématiquement
        // avec CONNECT_FAILED (driver pas prêt après softAP)
        if (millis() - apStabilizeStartMs >= AP_STABILIZE_MS) {
            Logger::info("WiFi", "AP stabilisé (" + String(AP_STABILIZE_MS) + "ms)");
            changeState(State::STA_CONFIG);
        }
        break;
    }

    // =========================================================================
    // ZONE RÉGIME PERMANENT — boucle bornée, jamais de retour en zone boot
    // =========================================================================

    case State::STA_CONFIG: {
        // WiFi.config() : ~4ms
        WiFi.config(WIFI_STA_IP, WIFI_STA_GATEWAY, WIFI_STA_SUBNET, WIFI_STA_DNS);

        changeState(State::STA_BEGIN);
        break;
    }

    case State::STA_BEGIN: {
        // WiFi.begin() : ~2ms
        WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);

        connectStartMs   = millis();  // Début du timeout de connexion
        lastConnectLogMs = connectStartMs;  // Prochain log dans 5s
        changeState(State::STA_CONNECTING);
        break;
    }

    case State::STA_CONNECTING: {
        // Polling WiFi.status() : <1ms
        wl_status_t status = WiFi.status();

        if (status == WL_CONNECTED) {
            staConnected = true;
            Logger::info("WiFi", "STA connecté — IP: " + WiFi.localIP().toString()
                         + ", RSSI: " + String(WiFi.RSSI()) + " dBm");
            changeState(State::STA_CONNECTED);
            break;
        }

        unsigned long now = millis();

        // Log périodique toutes les 5s (diagnostic sans spam)
        if (now - lastConnectLogMs >= STA_CONNECT_LOG_MS) {
            lastConnectLogMs = now;
            unsigned long elapsed = now - connectStartMs;
            Logger::info("WiFi", "STA connecting... "
                         + String(elapsed / 1000) + "s, status="
                         + wlStatusToString(status));
        }

        // Timeout ?
        if (now - connectStartMs > STA_CONNECT_TIMEOUT_MS) {
            Logger::info("WiFi", "STA timeout ("
                         + String(STA_CONNECT_TIMEOUT_MS / 1000) + "s)"
                         + ", dernier status=" + wlStatusToString(status));
            staConnected = false;
            changeState(State::STA_DISCONNECT);
        }

        // Note : les demandes externes ne sont PAS traitées ici.
        // Elles seront appliquées après passage en état stable.
        break;
    }

    case State::STA_CONNECTED: {
        // État stable — on traite les demandes externes
        applyPendingRequests();

        // Vérification connexion
        if (WiFi.status() != WL_CONNECTED) {
            staConnected = false;
            Logger::info("WiFi", "STA connexion perdue");
            changeState(State::STA_DISCONNECT);
        }
        break;
    }

    case State::STA_DISCONNECT: {
        // WiFi.disconnect() : ~0.5ms
        WiFi.disconnect();

        staConnected = false;

        // Début de la période d'attente avant retry
        retryStartMs = millis();

        changeState(State::STA_WAIT_RETRY);
        break;
    }

    case State::STA_WAIT_RETRY: {
        // État stable — on traite les demandes externes
        applyPendingRequests();

        // Attente du délai de retry (30s)
        // Inclut implicitement le délai post-disconnect (>= 250ms via tick TaskManager)
        if (millis() - retryStartMs > STA_RETRY_DELAY_MS) {
            Logger::info("WiFi", "STA retry après " + String(STA_RETRY_DELAY_MS / 1000) + "s");
            changeState(State::STA_CONFIG);
        }
        break;
    }

    case State::IDLE: {
        // STA désactivé — rien à faire côté WiFi STA
        // État stable — on traite les demandes externes
        applyPendingRequests();
        break;
    }

    }  // switch
}

// =============================================================================
// Demande de coupure AP (différée, appliquée en état stable)
// =============================================================================
void WiFiManager::disableAP()
{
    apDisableRequested = true;
    Logger::info("WiFi", "disableAP() demandé (sera appliqué en état stable)");
}

// =============================================================================
// Demande de changement STA (différée, appliquée en état stable → reboot)
// =============================================================================
void WiFiManager::setSTAEnabled(bool enabled)
{
    staChangeRequested = true;
    staChangeValue = enabled;
    Logger::info("WiFi", String("setSTAEnabled(") + (enabled ? "true" : "false") + ") demandé (sera appliqué en état stable)");
}

// =============================================================================
// Accesseurs
// =============================================================================
bool WiFiManager::isSTAEnabled()    { return staEnabled; }
bool WiFiManager::isSTAConnected()  { return staConnected; }
bool WiFiManager::isAPEnabled()     { return apEnabled; }

// =============================================================================
// Infos Web
// =============================================================================
String WiFiManager::getSTAStatus()
{
    if (!staEnabled) return "Désactivé";

    return staConnected
        ? "Connecté à " + String(WIFI_STA_SSID) + " " + WIFI_STA_IP.toString()
          + " (" + String(WiFi.RSSI()) + " dBm)"
        : "Recherche " + String(WIFI_STA_SSID) + "...";
}

String WiFiManager::getAPStatus()
{
    return String(WIFI_AP_SSID) + "  " + WIFI_AP_IP.toString();
}
