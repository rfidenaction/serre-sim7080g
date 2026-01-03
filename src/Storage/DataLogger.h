// Storage/DataLogger.h
#pragma once

#include <Arduino.h>

// ─────────────────────────────────────────────
// Référentiel temporel
// ─────────────────────────────────────────────

enum class TimeBase : uint8_t {
    Relative,   // secondes depuis boot
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

    Boot,
    Error
};

// ─────────────────────────────────────────────
// Enregistrement
// ─────────────────────────────────────────────

struct DataRecord {
    uint32_t timestamp;
    TimeBase timeBase;
    DataType type;
    DataId   id;
    float    value;
};

// ─────────────────────────────────────────────
// DataLogger
// ─────────────────────────────────────────────

class DataLogger {
public:
    static void init();

    static void push(DataType type, DataId id, float value);

    static void onNtpSync(uint32_t utcNow);

    static bool getLast(DataId id, DataRecord& out);  // Pour automate (live buffer)

    static void handle();

    // ─────────────────────────────────────────
    // Fonctions pour le site web
    // ─────────────────────────────────────────

    static bool getLastUtcRecord(DataId id, DataRecord& out);

    static String getCurrentValueWithTime(DataId id);

    static String getGraphCsv(DataId id, uint32_t daysBack = 30);

private:
    static bool     ntpValid;
    static uint32_t ntpOffset;

    static uint32_t nowRelative();
    static uint32_t nowUTC();

    static const size_t LIVE_SIZE    = 200;
    static const size_t PENDING_SIZE = 2000;
    static const size_t FLUSH_SIZE   = 50;

    static DataRecord live[LIVE_SIZE];
    static DataRecord pending[PENDING_SIZE];
    static DataRecord flush[FLUSH_SIZE];

    static size_t liveIndex;
    static size_t pendingCount;
    static size_t flushCount;

    static void addLive(const DataRecord& r);
    static void addPending(const DataRecord& r);
    static void tryFlush();
    static void flushToFlash();
};