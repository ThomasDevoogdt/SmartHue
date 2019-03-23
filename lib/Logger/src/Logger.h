#ifndef LOGGER_h
#define LOGGER_h

#include <Arduino.h>
#include <LinkedList.h>

class Logger {
public:
    enum class Severity {
        ERROR,
        WARN,
        INFO,
        DEBUG,
    };

    static const Severity ERROR = Severity::ERROR;
    static const Severity WARN = Severity::WARN;
    static const Severity INFO = Severity::INFO;
    static const Severity DEBUG = Severity::DEBUG;

public:
    Logger();
    Logger(const String& hostName);

    void registerLogger(void (*logger)(const String&), Severity severity = Logger::INFO);

    template <typename Printable>
    void log(const Printable message, const Severity severity = Logger::INFO)
    {
        String logTime;
        getLogTime(logTime);

        String logSeverity;
        getLogSeverity(logSeverity, severity);

        String logMessage = logTime + (m_hostname != "" ? " " + m_hostname : "") + " " + logSeverity + ": " + String(message);
        pushLogToNodes(logMessage, severity);
    }

private:
    void getLogTime(String& msg);
    void getLogSeverity(String& msg, const Severity& severity);
    void pushLogToNodes(String& msg, const Severity& severity);

    struct LoggerNode {
        void (*logger)(const String&);
        Severity severity;
    };

    LinkedList<LoggerNode> m_loggerNodeList;
    String m_hostname;
};

#endif