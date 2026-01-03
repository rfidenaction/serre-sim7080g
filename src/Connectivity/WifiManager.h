#pragma once

#include <Arduino.h>           // pour String, millis()
#include <WiFi.h>              // pour WiFi, wl_status_t
#include <Preferences.h>
#include "Config/NetworkConfig.h"

/*
 * WiFiManager
 *
 * Politique WiFi (FIGÉE) :
 *
 * - AP TOUJOURS actif au démarrage
 * - AP peut être coupé à chaud (irréversible sans reboot)
 * - STA activable/désactivable par utilisateur (persistant)
 * - Tout changement d’état STA provoque un reboot volontaire
 *
 * États autorisés :
 * - AP ON  + STA ON
 * - AP ON  + STA OFF
 * - AP OFF + STA ON
 * - AP OFF + STA OFF (mode GSM seul)
 *
 * Priorité absolue : STABILITÉ
 */

class WiFiManager {
public:
    // Initialisation et gestion
    static void init();
    static void handle();

    // Contrôle
    static void disableAP();                 // Coupure AP à chaud
    static void setSTAEnabled(bool enabled); // Modifie l’état STA (persistant + reboot)

    // États
    static bool isSTAEnabled();
    static bool isSTAConnected();
    static bool isAPEnabled();

    // Infos pour interface Web
    static String getSTAStatus();
    static String getAPStatus();

private:
    // Méthodes internes
    static void loadPreferences();
    static void startWiFi();

    // États runtime
    static bool staEnabled;
    static bool staConnecting;
    static bool staConnected;
    static bool apEnabled;

    static unsigned long lastStaAttempt;

    // Stockage persistant
    static Preferences preferences;
};
