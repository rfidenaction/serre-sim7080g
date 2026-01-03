#include "Logger.h"

namespace Logger {

    void begin() {
        // Pour l'instant rien. Plus tard: Serial.begin(SERIAL_BAUDRATE)
    }

    void debug(const char* msg) { }
    void info(const char* msg) { }
    void warn(const char* msg) { }
    void error(const char* msg) { }

}
