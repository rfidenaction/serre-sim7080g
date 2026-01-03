// Core/PowerManager.h
#pragma once

#define XPOWERS_CHIP_AXP2101   // Doit être défini avant XPowersLib.h

#include <Arduino.h>
#include <XPowersLib.h>

class PowerManager {
public:
    // -------------------------------------------------------------------------
    // Initialisation et mise à jour
    // -------------------------------------------------------------------------
    static void init();    // Initialisation du PMU
    static void update();  // Lecture immédiate du PMU (sans temporisation)

    // -------------------------------------------------------------------------
    // État matériel
    // -------------------------------------------------------------------------
    static bool isPmuDetected();  // PMU présent ou non

    // -------------------------------------------------------------------------
    // Getters état batterie (état instantané)
    // -------------------------------------------------------------------------
    static float getBatteryVoltage();        // Tension batterie en volts
    static int   getBatteryPercent();        // Pourcentage estimé
    static bool  isCharging();               // Batterie en charge
    static bool  isExternalPowerPresent();   // Alimentation externe (VBUS)

private:
    // -------------------------------------------------------------------------
    // Matériel
    // -------------------------------------------------------------------------
    static XPowersAXP2101 PMU;
    static bool pmuDetected;

    // -------------------------------------------------------------------------
    // État interne batterie
    // -------------------------------------------------------------------------
    static float batteryVoltage;
    static int   batteryPercent;
    static bool  charging;
    static bool  externalPower;
};
