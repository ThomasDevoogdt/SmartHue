#ifndef Storage_h
#define Storage_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include "LittleFS.h"

class Storage {

public:
    Storage()
    {
        m_storagePath = "";
        LittleFS.begin();
    };
    Storage(String path)
    {
        m_storagePath = path;
        LittleFS.begin();
    }

    bool remove()
    {
        if (m_storagePath == "") {
            return false;
        } else {
            return LittleFS.remove(m_storagePath);
        }
    }

    template <class JsonBuffer>
    JsonObject& loadJson(JsonBuffer& jsonBuffer) const
    {
        if (m_storagePath == "") {
            return jsonBuffer.createObject();
        } else {
            return Storage::loadJson(jsonBuffer, m_storagePath);
        }
    }

    template <class JsonBuffer>
    static JsonObject& loadJson(JsonBuffer& jsonBuffer, String path)
    {
        if (!LittleFS.exists(path)) {
            return jsonBuffer.createObject();
        }

        File file = LittleFS.open(path, "r");
        if (!file) {
            Serial.println("file open failed");
            return jsonBuffer.createObject();
        }

        JsonObject& jsonObjectRoot = jsonBuffer.parseObject(file);
        file.close();

        if (!jsonObjectRoot.success()) {
            return jsonBuffer.createObject();
        }

        return jsonObjectRoot;
    }

    bool writeJson(JsonObject& jsonObject)
    {
        if (m_storagePath == "") {
            return false;
        } else {
            return Storage::writeJson(jsonObject, m_storagePath);
        }
    }

    static bool writeJson(JsonObject& jsonObject, String path)
    {
        if (!jsonObject.success()) {
            return false;
        }

        LittleFS.remove(path); // delete existing file, otherwise the configuration is appended to the file

        File file = LittleFS.open(path, "w");
        if (!file) {
            Serial.println("file open failed");
            return false;
        }

        if (!jsonObject.printTo(file)) {
            file.close();
            return false;
        }

        file.close();
        return true;
    }

private:
    String m_storagePath;
};

#endif