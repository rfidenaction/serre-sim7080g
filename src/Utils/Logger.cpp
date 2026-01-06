#include "Logger.h"

Stream* Logger::_output = nullptr;
Logger::Level Logger::_currentLevel = Logger::Level::INFO;

void Logger::begin(Stream& output, Level level) {
    _output = &output;
    _currentLevel = level;
}

void Logger::setLevel(Level level) {
    _currentLevel = level;
}

Logger::Level Logger::getLevel() {
    return _currentLevel;
}

// ---------- API publique sans tag ----------

void Logger::error(const String& message) { log(Level::ERROR, "", message); }
void Logger::warn (const String& message) { log(Level::WARN,  "", message); }
void Logger::info (const String& message) { log(Level::INFO,  "", message); }
void Logger::debug(const String& message) { log(Level::DEBUG, "", message); }
void Logger::trace(const String& message) { log(Level::TRACE, "", message); }

// ---------- API publique avec tag ----------

void Logger::error(const String& tag, const String& message) { log(Level::ERROR, tag, message); }
void Logger::warn (const String& tag, const String& message) { log(Level::WARN,  tag, message); }
void Logger::info (const String& tag, const String& message) { log(Level::INFO,  tag, message); }
void Logger::debug(const String& tag, const String& message) { log(Level::DEBUG, tag, message); }
void Logger::trace(const String& tag, const String& message) { log(Level::TRACE, tag, message); }

// ---------- Implémentation centrale ----------

void Logger::log(Level level, const String& tag, const String& message) {
    if (_output == nullptr) return;
    if (level > _currentLevel) return;

    // Timestamp simple (millis)
    unsigned long timestamp = millis();

    _output->print("[");
    _output->print(timestamp);
    _output->print(" ms] ");

    _output->print(levelToString(level));

    if (!tag.isEmpty()) {
        _output->print(" [");
        _output->print(tag);
        _output->print("]");
    }

    _output->print(" ");
    _output->println(message);
}

const char* Logger::levelToString(Level level) {
    switch (level) {
        case Level::ERROR: return "ERROR";
        case Level::WARN:  return "WARN ";
        case Level::INFO:  return "INFO ";
        case Level::DEBUG: return "DEBUG";
        case Level::TRACE: return "TRACE";
        default:           return "UNKWN";
    }
}
