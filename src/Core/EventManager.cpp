// Core/EventManager.cpp

#include "Core/EventManager.h"

#include "Core/TaskManagerMonitor.h"
#include "Core/PowerManager.h"
#include "Connectivity/WiFiManager.h"
#include <WiFi.h>

// -----------------------------------------------------------------------------
// États statiques
// -----------------------------------------------------------------------------

EventManager::PowerState EventManager::currentPower;
EventManager::PowerState EventManager::previousPower;

EventManager::WifiState  EventManager::currentWifi;
EventManager::WifiState  EventManager::previousWifi;

// -----------------------------------------------------------------------------
// Initialisation
// -----------------------------------------------------------------------------

void EventManager::init()
{
    currentPower.valid  = false;
    previousPower.valid = false;

    currentWifi.valid   = false;
    previousWifi.valid  = false;
}

// -----------------------------------------------------------------------------
// Amorçage des états (INIT → RUN)
// -----------------------------------------------------------------------------

void EventManager::prime()
{
    // Lecture initiale des états
    readPowerState(currentPower);
    readWifiState(currentWifi);

    // Copie stricte : état précédent == état courant
    previousPower = currentPower;
    previousWifi  = currentWifi;
}

// -----------------------------------------------------------------------------
// Point d’entrée périodique
// -----------------------------------------------------------------------------

void EventManager::handle()
{
    // Supervision temporelle
    TaskManagerMonitor::notifyCall();

    // Sauvegarde des états précédents
    previousPower = currentPower;
    previousWifi  = currentWifi;

    // Lecture des nouveaux états
    readPowerState(currentPower);
    readWifiState(currentWifi);
}

// -----------------------------------------------------------------------------
// Lecture PowerManager
// -----------------------------------------------------------------------------

void EventManager::readPowerState(PowerState& target)
{
    if (!PowerManager::isPmuDetected()) {
        target.valid = false;
        return;
    }

    target.batteryVoltage = PowerManager::getBatteryVoltage();
    target.batteryPercent = PowerManager::getBatteryPercent();
    target.charging       = PowerManager::isCharging();
    target.externalPower  = PowerManager::isExternalPowerPresent();
    target.valid          = true;
}

// -----------------------------------------------------------------------------
// Lecture WiFiManager
// -----------------------------------------------------------------------------

void EventManager::readWifiState(WifiState& target)
{
    target.staEnabled   = WiFiManager::isSTAEnabled();
    target.staConnected = WiFiManager::isSTAConnected();

    if (target.staConnected) {
        target.rssi = WiFi.RSSI();
    } else {
        target.rssi = 0;
    }

    target.valid = true;
}

// -----------------------------------------------------------------------------
// Accesseurs – Power
// -----------------------------------------------------------------------------

bool EventManager::hasPowerState()              { return currentPower.valid; }
bool EventManager::hasPreviousPowerState()      { return previousPower.valid; }

float EventManager::getBatteryVoltage()         { return currentPower.batteryVoltage; }
float EventManager::getPreviousBatteryVoltage() { return previousPower.batteryVoltage; }

int EventManager::getBatteryPercent()            { return currentPower.batteryPercent; }
int EventManager::getPreviousBatteryPercent()    { return previousPower.batteryPercent; }

bool EventManager::isCharging()                  { return currentPower.charging; }
bool EventManager::wasCharging()                 { return previousPower.charging; }

bool EventManager::isExternalPowerPresent()      { return currentPower.externalPower; }
bool EventManager::wasExternalPowerPresent()     { return previousPower.externalPower; }

// -----------------------------------------------------------------------------
// Accesseurs – WiFi
// -----------------------------------------------------------------------------

bool EventManager::hasWifiState()                { return currentWifi.valid; }
bool EventManager::hasPreviousWifiState()        { return previousWifi.valid; }

bool EventManager::isStaEnabled()                { return currentWifi.staEnabled; }
bool EventManager::wasStaEnabled()               { return previousWifi.staEnabled; }

bool EventManager::isStaConnected()              { return currentWifi.staConnected; }
bool EventManager::wasStaConnected()             { return previousWifi.staConnected; }

int EventManager::getRssi()                      { return currentWifi.rssi; }
int EventManager::getPreviousRssi()              { return previousWifi.rssi; }
