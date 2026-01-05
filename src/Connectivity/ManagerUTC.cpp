// Connectivity/ManagerUTC.cpp

#include "Connectivity/ManagerUTC.h"
#include <WiFi.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Constantes temporelles
// -----------------------------------------------------------------------------
static constexpr uint32_t BOOT_RETRY_INTERVAL_MS   = 30UL * 1000UL;              // 30 s
static constexpr uint8_t  BOOT_MAX_ATTEMPTS        = 10;

static constexpr uint32_t RESYNC_PERIOD_MS         = 3UL * 60UL * 60UL * 1000UL;  // 3 h
static constexpr uint32_t UTC_VALIDITY_PERIOD_MS   = 25UL * 60UL * 60UL * 1000UL; // 25 h

// -----------------------------------------------------------------------------
// État interne
// -----------------------------------------------------------------------------
bool     ManagerUTC::everSynced     = false;
uint8_t  ManagerUTC::bootAttempts  = 0;
uint32_t ManagerUTC::lastAttemptMs = 0;
uint32_t ManagerUTC::lastSyncMs    = 0;

uint32_t ManagerUTC::syncRelMs     = 0;
time_t   ManagerUTC::syncUtc       = 0;

int32_t  ManagerUTC::utcOffset     = 0;

// -----------------------------------------------------------------------------
// Initialisation
// -----------------------------------------------------------------------------
void ManagerUTC::init()
{
    everSynced     = false;
    bootAttempts  = 0;
    lastAttemptMs = 0;
    lastSyncMs    = 0;

    syncRelMs     = 0;
    syncUtc       = 0;
    utcOffset     = 0;

    // Initialisation NTP (UTC pur)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

// -----------------------------------------------------------------------------
// Boucle autonome
// -----------------------------------------------------------------------------
void ManagerUTC::handle()
{
    const uint32_t nowMs = millis();

    // -------------------------------------------------------------------------
    // Phase BOOT : tentatives rapides
    // -------------------------------------------------------------------------
    if (!everSynced) {
        if (bootAttempts < BOOT_MAX_ATTEMPTS &&
            nowMs - lastAttemptMs >= BOOT_RETRY_INTERVAL_MS) {

            lastAttemptMs = nowMs;
            bootAttempts++;

            if (trySync()) {
                everSynced  = true;
                lastSyncMs  = nowMs;
            }
        }
        return;
    }

    // -------------------------------------------------------------------------
    // Régime permanent : resync lent
    // -------------------------------------------------------------------------
    if (nowMs - lastSyncMs >= RESYNC_PERIOD_MS) {
        if (trySync()) {
            lastSyncMs = nowMs;
        }
    }
}

// -----------------------------------------------------------------------------
// API publique
// -----------------------------------------------------------------------------
bool ManagerUTC::isUtcValid()
{
    if (!everSynced) return false;
    return (millis() - lastSyncMs) <= UTC_VALIDITY_PERIOD_MS;
}

bool ManagerUTC::hasEverSynced()
{
    return everSynced;
}

int32_t ManagerUTC::getUtcOffset()
{
    return utcOffset;
}

time_t ManagerUTC::nowUtc()
{
    return static_cast<time_t>(millis() / 1000UL) + utcOffset;
}

time_t ManagerUTC::convertFromRelative(uint32_t t_rel_ms)
{
    // Conversion basée sur le point de synchro
    return syncUtc +
           static_cast<int32_t>((t_rel_ms - syncRelMs) / 1000UL);
}

// -----------------------------------------------------------------------------
// Synchronisation NTP (Wi-Fi)
// -----------------------------------------------------------------------------
bool ManagerUTC::trySync()
{
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    time_t utcNow = 0;
    time(&utcNow);

    // Garde minimale : date crédible (> 2024)
    if (utcNow < 1700000000) {
        return false;
    }

    const uint32_t nowMs = millis();

    // Référence temporelle de synchro
    syncRelMs = nowMs;
    syncUtc   = utcNow;

    // Calcul offset UTC ↔ temps machine
    utcOffset = static_cast<int32_t>(
        utcNow - static_cast<time_t>(nowMs / 1000UL)
    );

    return true;
}
