// Storage/DataLogger.cpp
#include "Storage/DataLogger.h"
#include "Connectivity/ManagerUTC.h"

#include <SPIFFS.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Buffers
// -----------------------------------------------------------------------------
DataRecord DataLogger::live[LIVE_SIZE];
DataRecord DataLogger::pending[PENDING_SIZE];

size_t DataLogger::liveIndex    = 0;

// Pending FIFO circulaire
size_t DataLogger::pendingHead  = 0;   // index du plus ancien
size_t DataLogger::pendingCount = 0;   // nombre d’éléments valides

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
// Initialisation
// -----------------------------------------------------------------------------
void DataLogger::init()
{
    lastFlushMs = millis();

    pendingHead  = 0;
    pendingCount = 0;

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
    uint32_t relNow = nowRelative();
    bool utcValid   = ManagerUTC::isUtcValid();
    uint32_t utcNow = utcValid ? ManagerUTC::nowUtc() : 0;

    // LIVE (toujours relatif)
    DataRecord liveRec;
    liveRec.type      = type;
    liveRec.id        = id;
    liveRec.value     = value;
    liveRec.timestamp = relNow;
    liveRec.timeBase  = TimeBase::Relative;
    addLive(liveRec);

    // PENDING
    DataRecord pendRec;
    pendRec.type      = type;
    pendRec.id        = id;
    pendRec.value     = value;
    pendRec.timestamp = utcValid ? utcNow : relNow;
    pendRec.timeBase  = utcValid ? TimeBase::UTC : TimeBase::Relative;
    addPending(pendRec);

    // Vue Web
    LastDataForWeb& w = lastDataForWeb[id];
    w.value = value;

    if (utcValid) {
        w.t_utc     = utcNow;
        w.utc_valid = true;
    } else {
        w.t_rel_ms  = relNow;
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
// PENDING — FIFO circulaire avec perte FIFO
// -----------------------------------------------------------------------------
void DataLogger::addPending(const DataRecord& r)
{
    // Si plein : on perd le plus ancien (FIFO)
    if (pendingCount == PENDING_SIZE) {
        pendingHead = (pendingHead + 1) % PENDING_SIZE;
        pendingCount--;
    }

    size_t index =
        (pendingHead + pendingCount) % PENDING_SIZE;

    pending[index] = r;
    pendingCount++;
}

// -----------------------------------------------------------------------------
// HANDLE — réparation + flush
// -----------------------------------------------------------------------------
void DataLogger::handle()
{
    // Réparation UTC si NTP devenu valide
    if (ManagerUTC::isUtcValid()) {
        for (size_t i = 0; i < pendingCount; ++i) {
            size_t idx = (pendingHead + i) % PENDING_SIZE;
            if (pending[idx].timeBase == TimeBase::Relative) {
                pending[idx].timestamp =
                    ManagerUTC::convertFromRelative(pending[idx].timestamp);
                pending[idx].timeBase = TimeBase::UTC;
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
        size_t idx = (pendingHead + i) % PENDING_SIZE;
        if (pending[idx].timeBase == TimeBase::UTC) {
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
        size_t idx = (pendingHead + i) % PENDING_SIZE;
        DataRecord& r = pending[idx];

        f.printf("%lu,%d,%d,%.3f\n",
                 r.timestamp,
                 (int)r.type,
                 (int)r.id,
                 r.value);
    }
    f.close();

    pendingHead =
        (pendingHead + count) % PENDING_SIZE;
    pendingCount -= count;

    lastFlushMs = millis();
}

// -----------------------------------------------------------------------------
// WEB — dernière valeur RAM
// -----------------------------------------------------------------------------
bool DataLogger::hasLastDataForWeb(DataId id, LastDataForWeb& out)
{
    auto it = lastDataForWeb.find(id);
    if (it == lastDataForWeb.end()) return false;
    out = it->second;
    return true;
}

// -----------------------------------------------------------------------------
// FLASH — dernière valeur UTC
// -----------------------------------------------------------------------------
bool DataLogger::getLastUtcRecord(DataId id, DataRecord& out)
{
    File file = SPIFFS.open("/datalog.csv", FILE_READ);
    if (!file) return false;

    String line;
    bool found = false;
    DataRecord candidate;

    while (file.available()) {
        line = file.readStringUntil('\n');
        if (line.length() == 0) continue;

        unsigned long ts;
        uint8_t typeByte, idByte;
        float val;

        int parsed = sscanf(line.c_str(), "%lu,%hhu,%hhu,%f",
                            &ts, &typeByte, &idByte, &val);
        if (parsed == 4 && idByte == static_cast<uint8_t>(id)) {
            candidate.timestamp = ts;
            candidate.timeBase  = TimeBase::UTC;
            candidate.type      = static_cast<DataType>(typeByte);
            candidate.id        = id;
            candidate.value     = val;
            found = true;
        }
    }

    file.close();
    if (found) out = candidate;
    return found;
}

// -----------------------------------------------------------------------------
// GRAPH CSV (FLASH) — avec timestamp UTC
// -----------------------------------------------------------------------------
String DataLogger::getGraphCsv(DataId id, uint32_t daysBack)
{
    File file = SPIFFS.open("/datalog.csv", FILE_READ);
    if (!file) return "";

    uint32_t cutoffTime = 0;
    if (daysBack > 0) {
        cutoffTime = ManagerUTC::nowUtc() - (daysBack * 86400UL);
    }

    String csv = "timestamp,value\n";

    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.length() == 0) continue;

        unsigned long ts;
        uint8_t typeByte, idByte;
        float val;

        int parsed = sscanf(line.c_str(), "%lu,%hhu,%hhu,%f",
                            &ts, &typeByte, &idByte, &val);

        if (parsed == 4 &&
            idByte == static_cast<uint8_t>(id) &&
            (daysBack == 0 || ts >= cutoffTime))
        {
            csv += String(ts) + ",";
            csv += String(val, 2) + "\n";
        }
    }

    file.close();
    return csv;
}
