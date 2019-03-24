#include "Logger.h"
#include "Arduino.h"

Logger::Logger()
    : m_hostname("")
{
}

Logger::Logger(const String& hostname)
    : m_hostname(hostname)
{
}

void Logger::registerLogger(void (*logger)(const String&), Severity severity)
{
    m_loggerNodeList.add({ logger, severity });
}

void Logger::getLogTime(String& msg)
{
    msg = "[";
    String time = String(millis() / 1000.0, 3);
    for (int i = time.length(); i < 12; i++) {
        msg += ' ';
    }
    msg += time + "]";
}

void Logger::getLogSeverity(String& msg, const Severity& severity)
{
    switch (severity) {
    case Severity::ERROR:
        msg = "ERROR";
        return;
    case Severity::WARN:
        msg = "WARN";
        return;
    case Severity::INFO:
        msg = "INFO";
        return;
    case Severity::DEBUG:
        msg = "DEBUG";
        return;
    default:
        msg = "UNKNOWN";
        return;
    }
}

void Logger::pushLogToNodes(String& msg, const Severity& severity)
{
    for (int i = 0; i < m_loggerNodeList.size(); i++) {
        LoggerNode loggerNode = m_loggerNodeList.get(i);
        if (loggerNode.severity < severity) {
            continue;
        }

        loggerNode.logger(msg);
    }
}