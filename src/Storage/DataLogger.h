// Storage/DataLogger.h
#pragma once

#include <Arduino.h>
#include <map>
#include <time.h>

// ─────────────────────────────────────────────
// Référentiel temporel
// ─────────────────────────────────────────────

enum class TimeBase : uint8_t {
    Relative,   // millis depuis boot
    UTC         // timestamp absolu
};

// ─────────────────────────────────────────────
// Types de données
// ─────────────────────────────────────────────

enum class DataType : uint8_t {
    Battery,
    Sensor,
    Actuator,
    System
};

enum class DataId : uint8_t {
    BatteryVoltage,
    BatteryPercent,
    Charging,
    ExternalPower,

    AirTemperature,
    AirHumidity,
    SoilMoisture1,
    SoilMoisture2,

    Valve1State,
    Valve2State,

    // Wi-Fi
    WifiStaEnabled,
    WifiStaConnected,
    WifiApEnabled,
    WifiRssi,

    Boot,
    Error,

    Count
};

// ─────────────────────────────────────────────
// Enregistrement
// ─────────────────────────────────────────────

struct DataRecord {
    uint32_t timestamp;   // millis ou UTC selon timeBase
    TimeBase timeBase;
    DataType type;
    DataId   id;
    float    value;
};

// ─────────────────────────────────────────────
// Dernière observation exposée au Web
// ─────────────────────────────────────────────

struct LastDataForWeb {
    float     value     = 0.0f;
    uint32_t  t_rel_ms  = 0;
    time_t    t_utc     = 0;
    bool      utc_valid = false;
};

// ─────────────────────────────────────────────
// DataLogger
// ─────────────────────────────────────────────

class DataLogger {
public:
    static void init();

    static void push(DataType type, DataId id, float value);

    static bool getLast(DataId id, DataRecord& out); // live

    static void handle(); // réparation + flush

    // ───────────── Web ─────────────
    static bool hasLastDataForWeb(DataId id, LastDataForWeb& out);
    static bool getLastUtcRecord(DataId id, DataRecord& out);
    static String getCurrentValueWithTime(DataId id);
    static String getGraphCsv(DataId id, uint32_t daysBack = 30);

private:
    // ───────────── Temps ─────────────
    static uint32_t nowRelative();

    // ───────────── Buffers ─────────────
    static constexpr size_t LIVE_SIZE    = 200;
    static constexpr size_t PENDING_SIZE = 2000;
    static constexpr size_t FLUSH_SIZE   = 50;

    static constexpr uint32_t FLUSH_TIMEOUT_MS = 3600000UL; // 1 heure

    static DataRecord live[LIVE_SIZE];
    static DataRecord pending[PENDING_SIZE];

    static size_t liveIndex;
    static size_t pendingCount;

    // ───────────── Web RAM ─────────────
    static std::map<DataId, LastDataForWeb> lastDataForWeb;

    // ───────────── Internes ─────────────
    static void addLive(const DataRecord& r);
    static void addPending(const DataRecord& r);

    static void tryFlush();
    static void flushToFlash(size_t count);
};
