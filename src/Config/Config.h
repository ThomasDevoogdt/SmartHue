#ifndef Config_h
#define Config_h

#include "../Storage/Storage.h"
#include <Arduino.h>
#include <ArduinoJson.h>

#define CONFIG_PATH "/config/system.json"

class Config {
public:
    Config(Print *logger = &Serial)
        : m_storage(Storage(CONFIG_PATH, logger))
    {
        setup();
    }

    void reset()
    {
        m_storage.reset();
        setup();
    }

    String getConfigVersion() const
    {
        DynamicJsonBuffer jsonBuffer;
        return m_storage.loadJson(jsonBuffer).get<String>("version");
    }

    void setWifiConfig(const String& ssid, const String& pass)
    {
        String oldSsid, oldPass;
        getWifiConfig(oldSsid, oldPass);
        if (oldSsid == ssid && oldPass == pass) {
            return; // only write new data!
        }

        DynamicJsonBuffer jsonBuffer;
        JsonObject& jsonObjectRoot = m_storage.loadJson(jsonBuffer);
        JsonObject& jsonobjectWifi = jsonObjectRoot.get<JsonVariant>("wifi").as<JsonObject>();
        jsonobjectWifi.set("ssid", ssid);
        jsonobjectWifi.set("pass", pass);
        m_storage.writeJson(jsonObjectRoot); // only write new data!
    }

    void getWifiConfig(String& ssid, String& pass) const
    {
        DynamicJsonBuffer jsonBuffer;
        JsonObject& jsonobjectWifi = m_storage.loadJson(jsonBuffer).get<JsonVariant>("wifi").as<JsonObject>();
        ssid = jsonobjectWifi.get<String>("ssid");
        pass = jsonobjectWifi.get<String>("pass");
    }

    void setMqttConfig(const String& ip, const int& port)
    {
        String oldIp;
        int oldPort;
        getMqttConfig(oldIp, oldPort);
        if (oldIp == ip && oldPort == port) {
            return; // only write new data!
        }

        DynamicJsonBuffer jsonBuffer;
        JsonObject& jsonObjectRoot = m_storage.loadJson(jsonBuffer);
        JsonObject& jsonobjectMqtt = jsonObjectRoot.get<JsonVariant>("mqtt").as<JsonObject>();
        jsonobjectMqtt.set("ip", ip);
        jsonobjectMqtt.set("port", port);
        m_storage.writeJson(jsonObjectRoot);
    }

    void getMqttConfig(String& ip, int& port) const
    {
        DynamicJsonBuffer jsonBuffer;
        JsonObject& jsonobjectMqtt = m_storage.loadJson(jsonBuffer).get<JsonVariant>("mqtt").as<JsonObject>();
        ip = jsonobjectMqtt.get<String>("ip");
        port = jsonobjectMqtt.get<int>("port");
    }

    void setSyslogConfig(const String& ip, const int& port)
    {
        String oldIp;
        int oldPort;
        getSyslogConfig(oldIp, oldPort);
        if (oldIp == ip && oldPort == port) {
            return; // only write new data!
        }

        DynamicJsonBuffer jsonBuffer;
        JsonObject& jsonObjectRoot = m_storage.loadJson(jsonBuffer);
        JsonObject& jsonobjectSyslog = jsonObjectRoot.get<JsonVariant>("syslog").as<JsonObject>();
        jsonobjectSyslog.set("ip", ip);
        jsonobjectSyslog.set("port", port);
        m_storage.writeJson(jsonObjectRoot);
    }

    void getSyslogConfig(String& ip, int& port) const
    {
        DynamicJsonBuffer jsonBuffer;
        JsonObject& jsonobjectSyslog = m_storage.loadJson(jsonBuffer).get<JsonVariant>("syslog").as<JsonObject>();
        ip = jsonobjectSyslog.get<String>("ip");
        port = jsonobjectSyslog.get<int>("port");
    }

private:
    bool setup()
    {
        bool newConfig = false;
        DynamicJsonBuffer jsonBuffer;
        JsonObject& jsonObjectRoot = m_storage.loadJson(jsonBuffer);
        if (!jsonObjectRoot.containsKey("version")) {
            jsonObjectRoot.set("version", "2.0");
            newConfig = true;
        }

        // read wifi config or create default wifi config
        JsonObject& jsonobjectWifi = !jsonObjectRoot.containsKey("wifi")
            ? jsonObjectRoot.createNestedObject("wifi")
            : jsonObjectRoot.get<JsonVariant>("wifi").as<JsonObject>();

        if (!jsonobjectWifi.containsKey("ssid")) {
            jsonobjectWifi.set("ssid", "");
            newConfig = true;
        }
        if (!jsonobjectWifi.containsKey("pass")) {
            jsonobjectWifi.set("pass", "");
            newConfig = true;
        }

        // read mqtt config or create default mqtt config
        JsonObject& jsonobjectMqtt = !jsonObjectRoot.containsKey("mqtt")
            ? jsonObjectRoot.createNestedObject("mqtt")
            : jsonObjectRoot.get<JsonVariant>("mqtt").as<JsonObject>();

        if (!jsonobjectMqtt.containsKey("ip")) {
            jsonobjectMqtt.set("ip", "");
            newConfig = true;
        }
        if (!jsonobjectMqtt.containsKey("port")) {
            jsonobjectMqtt.set("port", 1883);
            newConfig = true;
        }

        // read syslog config or create default syslog config
        JsonObject& jsonobjectSyslog = !jsonObjectRoot.containsKey("syslog")
            ? jsonObjectRoot.createNestedObject("syslog")
            : jsonObjectRoot.get<JsonVariant>("syslog").as<JsonObject>();

        if (!jsonobjectSyslog.containsKey("ip")) {
            jsonobjectSyslog.set("ip", "");
            newConfig = true;
        }
        if (!jsonobjectSyslog.containsKey("port")) {
            jsonobjectSyslog.set("port", 514);
            newConfig = true;
        }

        if (newConfig) {
            return m_storage.writeJson(jsonObjectRoot); // only write new data!
        }

        return true;
    }

    Storage m_storage;
};

#endif