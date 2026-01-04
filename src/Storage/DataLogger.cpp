// Storage/DataLogger.cpp
#include "Storage/DataLogger.h"

#include <SPIFFS.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Stockage RAM : dernière observation exposée au Web
// -----------------------------------------------------------------------------
std::map<DataId, LastDataForWeb> DataLogger::lastDataForWeb;

// -----------------------------------------------------------------------------
// Initialisation
// -----------------------------------------------------------------------------
void DataLogger::init()
{
    // Initialisation SPIFFS, buffers internes, etc.
    // (code existant inchangé)

    // -------------------------------------------------------------------------
    // Reconstruction de LastDataForWeb depuis la flash
    // -------------------------------------------------------------------------
    for (int id = 0; id < (int)DataId::Count; ++id) {
        DataRecord rec;
        if (getLastUtcRecord((DataId)id, rec)) {
            LastDataForWeb entry;
            entry.value     = rec.value;
            entry.t_rel_ms  = 0;              // inconnu au boot
            entry.t_utc     = rec.timestamp;
            entry.utc_valid = true;

            lastDataForWeb[(DataId)id] = entry;
        }
    }
}

// -----------------------------------------------------------------------------
// Réception d'une nouvelle mesure (LIVE)
// -----------------------------------------------------------------------------
void DataLogger::addLive(DataId id, float value)
{
    uint32_t t_rel = nowRelative();

    // -------------------------------------------------------------------------
    // Mise à jour de la vue exposée au Web (RAM uniquement)
    // -------------------------------------------------------------------------
    LastDataForWeb& entry = lastDataForWeb[id];
    entry.value    = value;
    entry.t_rel_ms = t_rel;

    if (isUtcValid()) {
        entry.t_utc     = nowUTC();
        entry.utc_valid = true;
    } else {
        entry.utc_valid = false;
    }

    // -------------------------------------------------------------------------
    // Traitement existant (pending / règles internes)
    // -------------------------------------------------------------------------
    addPending(id, value);
}

// -----------------------------------------------------------------------------
// Accès Web : dernière donnée disponible
// -----------------------------------------------------------------------------
bool DataLogger::hasLastDataForWeb(DataId id, LastDataForWeb& out)
{
    auto it = lastDataForWeb.find(id);
    if (it == lastDataForWeb.end()) {
        return false;
    }

    out = it->second;
    return true;
}

// ─────────────────────────────────────────
// 1. Dernière valeur UTC dans la flash
// ─────────────────────────────────────────
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
            candidate.id        = id;
            candidate.value     = val;
            found = true;
        }
    }

    file.close();

    if (found) out = candidate;
    return found;
}

// ─────────────────────────────────────────
// 2. API LEGACY (sera supprimée plus tard)
// ─────────────────────────────────────────
String DataLogger::getCurrentValueWithTime(DataId id)
{
    DataRecord r;
    if (getLastUtcRecord(id, r)) {
        struct tm* tm = localtime((time_t*)&r.timestamp);
        char dateStr[20];
        strftime(dateStr, sizeof(dateStr), "%d/%m/%y %H:%M", tm);

        if (id == DataId::BatteryVoltage) {
            return String(r.value, 2) + " V (" + String(dateStr) + ")";
        } else if (id == DataId::BatteryPercent) {
            return String((int)r.value) + " % (" + String(dateStr) + ")";
        } else {
            return String(r.value, 2) + " (" + String(dateStr) + ")";
        }
    }

    return "NC";
}

// ─────────────────────────────────────────
// 3. Graphique sur période (FLASH)
// ─────────────────────────────────────────
String DataLogger::getGraphCsv(DataId id, uint32_t daysBack)
{
    File file = SPIFFS.open("/datalog.csv", FILE_READ);
    if (!file) return "";

    uint32_t cutoffTime = nowUTC() - (daysBack * 86400UL);

    String csv;
    bool first = true;

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
            ts >= cutoffTime)
        {
            if (!first) csv += ",";
            csv += String(val, 2);
            first = false;
        }
    }

    file.close();
    return csv;
}
