// src/Connectivity/WiFiManager.h
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "Config/NetworkConfig.h"

/*
 * WiFiManager — Machine d'états non-bloquante
 *
 * Politique WiFi (FIGÉE) :
 *
 * - AP TOUJOURS démarré au boot (une seule fois, jamais répété)
 * - AP peut être coupé à chaud (irréversible sans reboot)
 * - STA activable/désactivable par utilisateur (persistant + reboot)
 * - Tout changement d'état STA provoque un reboot volontaire
 *
 * États autorisés :
 * - AP ON  + STA ON
 * - AP ON  + STA OFF
 * - AP OFF + STA ON
 * - AP OFF + STA OFF (mode GSM seul)
 *
 * Intégration TaskManager :
 * - handle() appelé toutes les 250ms par TaskManager
 * - Chaque appel traite UN état puis retourne
 * - Budget temps garanti < 15ms sauf AP_START unique (~725ms)
 *
 * Priorité absolue : STABILITÉ
 * Le WiFi n'est pas critique — l'arrosage et les capteurs priment.
 */
class WiFiManager {
public:
    // -------------------------------------------------------------------------
    // Initialisation et gestion
    // -------------------------------------------------------------------------
    static void init();     // Active radio WiFi + lwIP, ne démarre ni AP ni STA
    static void handle();   // Machine d'états, appelée par TaskManager

    // -------------------------------------------------------------------------
    // Contrôle (via flags, appliqués en états stables)
    // -------------------------------------------------------------------------
    static void disableAP();                 // Demande coupure AP à chaud
    static void setSTAEnabled(bool enabled); // Demande changement STA (reboot)

    // -------------------------------------------------------------------------
    // États (lecture seule)
    // -------------------------------------------------------------------------
    static bool isSTAEnabled();
    static bool isSTAConnected();
    static bool isAPEnabled();

    // -------------------------------------------------------------------------
    // Infos pour interface Web
    // -------------------------------------------------------------------------
    static String getSTAStatus();
    static String getAPStatus();

private:
    // -------------------------------------------------------------------------
    // Machine d'états
    // -------------------------------------------------------------------------

    /*
     * ZONE BOOT (traversée une seule fois, jamais revisitée) :
     *
     *   [init() appelle WiFi.mode() — prérequis lwIP — ~50ms]
     *
     *   AP_CONFIG ────── WiFi.softAPConfig()  ~1ms
     *        │
     *        ▼
     *   AP_START ─────── WiFi.softAP()        ~0.1ms ou ~725ms (UNIQUE)
     *        │
     *        ├── STA désactivé ──► IDLE
     *        │
     *        ▼  STA activé
     *   AP_STABILIZE ── attente 1s (stabilisation driver AP+STA)
     *        │
     *        ▼
     *   STA_CONFIG ───── WiFi.config()        ~4ms
     *        │
     *        ▼
     *   STA_BEGIN ────── WiFi.begin()         ~2ms
     *        │
     *        ▼
     *   STA_CONNECTING
     *
     * ZONE RÉGIME PERMANENT (boucle bornée) :
     *
     *   STA_CONNECTING ─ timeout 30s ──► STA_DISCONNECT
     *        │
     *        └── WL_CONNECTED ──► STA_CONNECTED
     *
     *   STA_CONNECTED ── perte connexion ──► STA_DISCONNECT
     *
     *   STA_DISCONNECT ─ WiFi.disconnect() ──► STA_WAIT_RETRY
     *
     *   STA_WAIT_RETRY ─ attente 30s ──► STA_CONFIG (reboucle)
     *
     *   IDLE ─────────── rien (STA désactivé)
     *
     * Aucun chemin ne remonte vers la zone boot.
     * Les demandes externes (disableAP, setSTAEnabled) sont appliquées
     * uniquement dans les états stables : STA_CONNECTED, STA_WAIT_RETRY, IDLE.
     * staChangeRequested (reboot) est prioritaire sur apDisableRequested.
     */
    enum class State {
        // Zone boot (traversée une seule fois)
        // Note : WiFi.mode() est appelé dans init() (prérequis lwIP pour WebServer)
        AP_CONFIG,
        AP_START,
        AP_STABILIZE,       // Stabilisation driver post-AP avant lancement STA
        STA_CONFIG,
        STA_BEGIN,
        // Zone régime permanent
        STA_CONNECTING,
        STA_CONNECTED,
        STA_DISCONNECT,
        STA_WAIT_RETRY,
        IDLE
    };

    static State state;

    // -------------------------------------------------------------------------
    // Méthodes internes
    // -------------------------------------------------------------------------
    static void loadPreferences();
    static void applyPendingRequests();                        // Applique flags dans états stables
    static void changeState(State newState);                   // Transition + log
    static const char* wlStatusToString(wl_status_t status);   // Traduction lisible

    // (bootZoneDone et apStarted supprimés — la structure de la machine
    //  d'états garantit qu'on ne revient jamais en zone boot)

    // -------------------------------------------------------------------------
    // États runtime
    // -------------------------------------------------------------------------
    static bool staEnabled;     // Préférence utilisateur (persistante)
    static bool staConnected;   // Connexion STA effective
    static bool apEnabled;      // AP actif

    // -------------------------------------------------------------------------
    // Timing
    // -------------------------------------------------------------------------
    static unsigned long apStabilizeStartMs;  // Timestamp début stabilisation post-AP
    static unsigned long connectStartMs;      // Timestamp début WiFi.begin() (STA_BEGIN)
    static unsigned long retryStartMs;        // Timestamp début attente retry (STA_DISCONNECT)
    static unsigned long lastConnectLogMs;    // Dernier log périodique en STA_CONNECTING

    // -------------------------------------------------------------------------
    // Constantes internes
    // -------------------------------------------------------------------------
    static constexpr unsigned long AP_STABILIZE_MS         = 1000;   // Stabilisation post-AP
    static constexpr unsigned long STA_CONNECT_TIMEOUT_MS  = 30000;  // Timeout connexion
    static constexpr unsigned long STA_RETRY_DELAY_MS      = 30000;  // Délai entre retries
    static constexpr unsigned long STA_CONNECT_LOG_MS      = 5000;   // Log périodique en STA_CONNECTING

    // -------------------------------------------------------------------------
    // Flags de demandes externes (concurrence web → machine d'états)
    // -------------------------------------------------------------------------
    static bool apDisableRequested;     // disableAP() demandé
    static bool staChangeRequested;     // setSTAEnabled() demandé
    static bool staChangeValue;         // Valeur demandée pour STA

    // -------------------------------------------------------------------------
    // Stockage persistant
    // -------------------------------------------------------------------------
    static Preferences preferences;
};
