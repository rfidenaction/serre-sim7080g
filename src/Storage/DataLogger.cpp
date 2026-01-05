// Storage/DataLogger.cpp
#include "Storage/DataLogger.h"
#include "Connectivity/ManagerUTC.h"

#include <SPIFFS.h>

// -----------------------------------------------------------------------------
// Buffers
// -----------------------------------------------------------------------------
DataRecord DataLogger::live[LIVE_SIZE];
DataRecord DataLogger::pending[PENDING_SIZE];

size_t DataLogger::liveIndex    = 0;
size_t DataLogger::pendingCount = 0;

std::map<DataId, LastDataForWeb> DataLogger::lastDataForWeb;

static unsigned long lastFlushMs = 0;

// -----------------------------------------------------------------------------
// Temps
// -----------------------------------------------------------------------------
uint32_t DataLogger::nowRelative()
{
    return millis();
}

// -----------------------------------------------------------------------------
// Init
// -----------------------------------------------------------------------------
void DataLogger::init()
{
    lastFlushMs = millis();

    // Reconstruction LastDataForWeb depuis la flash
    for (int id = 0; id < (int)DataId::Count; ++id) {
        DataRecord rec;
        if (getLastUtcRecord((DataId)id, rec)) {
            LastDataForWeb e;
            e.value     = rec.value;
            e.t_rel_ms  = 0;
            e.t_utc     = rec.timestamp;
            e.utc_valid = true;
            lastDataForWeb[(DataId)id] = e;
        }
    }
}

// -----------------------------------------------------------------------------
// PUSH — point d’entrée unique
// -----------------------------------------------------------------------------
void DataLogger::push(DataType type, DataId id, float value)
{
    DataRecord r;
    r.type      = type;
    r.id        = id;
    r.value     = value;
    r.timestamp = nowRelative();
    r.timeBase  = TimeBase::Relative;

    // LIVE : toujours, temps machine
    addLive(r);

    bool utcValid = ManagerUTC::isUtcValid();

    // PENDING : toujours
    if (utcValid) {
        r.timestamp = ManagerUTC::nowUtc();
        r.timeBase  = TimeBase::UTC;
    }
    addPending(r);

    // Vue Web
    LastDataForWeb& w = lastDataForWeb[id];
    w.value    = value;
    w.t_rel_ms = nowRelative();
    if (utcValid) {
        w.t_utc     = r.timestamp;
        w.utc_valid = true;
    } else {
        w.utc_valid = false;
    }
}

// -----------------------------------------------------------------------------
// LIVE
// -----------------------------------------------------------------------------
void DataLogger::addLive(const DataRecord& r)
{
    live[liveIndex] = r;
    liveIndex = (liveIndex + 1) % LIVE_SIZE;
}

// -----------------------------------------------------------------------------
// PENDING
// -----------------------------------------------------------------------------
void DataLogger::addPending(const DataRecord& r)
{
    if (pendingCount < PENDING_SIZE) {
        pending[pendingCount++] = r;
    }
}

// -----------------------------------------------------------------------------
// HANDLE — réparation + flush
// -----------------------------------------------------------------------------
void DataLogger::handle()
{
    // Réparation UTC si NTP devenu valide
    if (ManagerUTC::isUtcValid()) {
        for (size_t i = 0; i < pendingCount; ++i) {
            if (pending[i].timeBase == TimeBase::Relative) {
                pending[i].timestamp =
                    ManagerUTC::convertFromRelative(pending[i].timestamp);
                pending[i].timeBase = TimeBase::UTC;
            }
        }
    }

    bool flushByCount =
        pendingCount >= FLUSH_SIZE;

    bool flushByTime =
        pendingCount > 0 &&
        (millis() - lastFlushMs >= FLUSH_TIMEOUT_MS);

    if (flushByCount || flushByTime) {
        tryFlush();
    }
}

// -----------------------------------------------------------------------------
// TRY FLUSH
// -----------------------------------------------------------------------------
void DataLogger::tryFlush()
{
    if (!ManagerUTC::isUtcValid()) return;

    size_t flushable = 0;
    for (size_t i = 0; i < pendingCount; ++i) {
        if (pending[i].timeBase == TimeBase::UTC) {
            flushable++;
        } else {
            break;
        }
    }

    if (flushable == 0) return;

    size_t toFlush = min(flushable, FLUSH_SIZE);
    flushToFlash(toFlush);
}

// -----------------------------------------------------------------------------
// FLUSH TO FLASH
// -----------------------------------------------------------------------------
void DataLogger::flushToFlash(size_t count)
{
    File f = SPIFFS.open("/datalog.csv", FILE_APPEND);
    if (!f) return;

    for (size_t i = 0; i < count; ++i) {
        DataRecord& r = pending[i];
        f.printf("%lu,%d,%d,%.3f\n",
                 r.timestamp,
                 (int)r.type,
                 (int)r.id,
                 r.value);
    }
    f.close();

    // Décalage FIFO
    for (size_t i = count; i < pendingCount; ++i) {
        pending[i - count] = pending[i];
    }
    pendingCount -= count;

    lastFlushMs = millis();
}

// -----------------------------------------------------------------------------
// WEB / LECTURE
// -----------------------------------------------------------------------------
bool DataLogger::hasLastDataForWeb(DataId id, LastDataForWeb& out)
{
    auto it = lastDataForWeb.find(id);
    if (it == lastDataForWeb.end()) return false;
    out = it->second;
    return true;
}
