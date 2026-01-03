#pragma once
#include <Arduino.h>  // nécessaire pour Serial plus tard

namespace Logger {
    void begin();                     // initialisation (Serial.begin plus tard)
    void debug(const char* msg);      // message de debug
    void info(const char* msg);       // message d’information
    void warn(const char* msg);       // warning
    void error(const char* msg);      // message d’erreur
}