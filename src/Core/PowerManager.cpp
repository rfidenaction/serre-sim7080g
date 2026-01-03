// Core/PowerManager.cpp
#include "Core/PowerManager.h"

// -----------------------------------------------------------------------------
// Membres statiques
// -----------------------------------------------------------------------------
XPowersAXP2101 PowerManager::PMU;
bool  PowerManager::pmuDetected    = false;
float PowerManager::batteryVoltage = 0.0f;
int   PowerManager::batteryPercent = 0;
bool  PowerManager::charging       = false;
bool  PowerManager::externalPower  = false;

// -----------------------------------------------------------------------------
// Initialisation
// -----------------------------------------------------------------------------
void PowerManager::init()
{
    pmuDetected = PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, 15, 7);

    if (pmuDetected) {
        PMU.disableTSPinMeasure();
        update(); // Initialisation immédiate de l’état
    }
}

// -----------------------------------------------------------------------------
// Mise à jour (sans temporisation)
// -----------------------------------------------------------------------------
void PowerManager::update()
{
    if (!pmuDetected) {
        return;
    }

    batteryVoltage = PMU.getBattVoltage() / 1000.0f;

    batteryPercent = constrain(
        (batteryVoltage - 3.0f) / (4.2f - 3.0f) * 100.0f,
        0,
        100
    );

    charging      = PMU.isCharging();
    externalPower = PMU.isVbusGood();
}

// -----------------------------------------------------------------------------
// Getters
// -----------------------------------------------------------------------------
bool PowerManager::isPmuDetected()
{
    return pmuDetected;
}

float PowerManager::getBatteryVoltage()
{
    return batteryVoltage;
}

int PowerManager::getBatteryPercent()
{
    return batteryPercent;
}

bool PowerManager::isCharging()
{
    return charging;
}

bool PowerManager::isExternalPowerPresent()
{
    return externalPower;
}
