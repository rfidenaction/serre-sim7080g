// Core/EventManager.h
#pragma once

#include <Arduino.h>

/*
 * EventManager v2.1
 *
 * Extension :
 *  - ajout de prime() pour initialiser les états lors de la transition INIT → RUN
 *
 * Rôle :
 *  - observer les sous-systèmes
 *  - conserver l’état courant ET précédent
 *  - fournir une base saine pour la détection d’événements
 *
 * Toujours :
 *  - aucune règle métier
 *  - aucune action
 *  - aucune persistance
 */

class EventManager {
public:
    static void init();

    // Initialisation explicite des états (appelée une seule fois INIT → RUN)
    static void prime();

    // Appelé périodiquement par TaskManager
    static void handle();

    // ---------------------------------------------------------------------
    // Accès aux états Power
    // ---------------------------------------------------------------------

    static bool  hasPowerState();
    static bool  hasPreviousPowerState();

    static float getBatteryVoltage();
    static float getPreviousBatteryVoltage();

    static int   getBatteryPercent();
    static int   getPreviousBatteryPercent();

    static bool  isCharging();
    static bool  wasCharging();

    static bool  isExternalPowerPresent();
    static bool  wasExternalPowerPresent();

    // ---------------------------------------------------------------------
    // Accès aux états WiFi
    // ---------------------------------------------------------------------

    static bool hasWifiState();
    static bool hasPreviousWifiState();

    static bool isStaEnabled();
    static bool wasStaEnabled();

    static bool isStaConnected();
    static bool wasStaConnected();

    static int  getRssi();
    static int  getPreviousRssi();

private:
    // ---------------------------------------------------------------------
    // États internes
    // ---------------------------------------------------------------------

    struct PowerState {
        bool  valid = false;
        float batteryVoltage = 0.0f;
        int   batteryPercent = 0;
        bool  charging = false;
        bool  externalPower = false;
    };

    struct WifiState {
        bool valid = false;
        bool staEnabled = false;
        bool staConnected = false;
        int  rssi = 0;
    };

    static PowerState currentPower;
    static PowerState previousPower;

    static WifiState  currentWifi;
    static WifiState  previousWifi;

    // Méthodes internes
    static void readPowerState(PowerState& target);
    static void readWifiState(WifiState& target);
};
