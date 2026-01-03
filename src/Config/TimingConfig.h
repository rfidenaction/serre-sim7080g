// Config/TimingConfig.h
#pragma once

// -----------------------------------------------------------------------------
// Périodes d’échantillonnage (ms)
// -----------------------------------------------------------------------------

// Intervalle de lecture de la batterie et log PowerManager → DataLogger
// 30 secondes suffisent pour suivre l'état de la batterie et de l'alimentation externe.
// Ajuster uniquement si nécessaire.
#define POWERMANAGER_UPDATE_INTERVAL_MS   30000   // Lecture batterie (PMU)

// À venir :
// #define TEMP_SENSOR_UPDATE_INTERVAL_MS   10000
// #define HUM_SENSOR_UPDATE_INTERVAL_MS    15000
// #define LOGGING_INTERVAL_MS              60000
