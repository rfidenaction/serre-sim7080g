// Core/PowerManager.cpp
#include "Core/PowerManager.h"
#include "Utils/Logger.h"
// Tag pour logs
static const char* TAG = "PowerMgr";
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
        Logger::info(TAG, "PMU AXP2101 détecté");
        
        // =====================================================================
        // 🔧 CONFIGURATION PMU POUR MODEM (basé sur exemple LilyGO fonctionnel)
        // =====================================================================
        // Configuration testée et validée le 14 janvier 2026 avec exemple
        // MinimalModemNBIOTExample.ino de LilyGO.
        // 
        // Le PMU active DC3 par défaut au boot, ce qui met DC3 en protection.
        // Solution : disable → delay → définir voltage → enable
        // 
        // DC3   = Alimentation principale modem (3.0V)
        // BLDO2 = Alimentation auxiliaire modem (3.3V)
        // =====================================================================
        
        Logger::info(TAG, "Configuration PMU pour modem...");
        
        // Fix DC3 + définir voltage pour modem
        PMU.disableDC3();
        Logger::debug(TAG, "DC3 disabled");
        delay(500);
        
        PMU.setDC3Voltage(3000);      // 3.0V pour modem SIM7080G
        PMU.enableDC3();
        Logger::info(TAG, "✅ DC3 configuré: 3.0V");
        
        // Activer BLDO2 pour alimentation auxiliaire modem
        PMU.setBLDO2Voltage(3300);    // 3.3V pour modem SIM7080G
        PMU.enableBLDO2();
        Logger::info(TAG, "✅ BLDO2 configuré: 3.3V");
        
        Logger::info(TAG, "✅ Configuration PMU modem terminée");
        
        // =====================================================================
        
        PMU.disableTSPinMeasure();
        
        update(); // Initialisation immédiate de l'état
    } else {
        Logger::error(TAG, "PMU AXP2101 non détecté");
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