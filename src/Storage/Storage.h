#ifndef Storage_h
#define Storage_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include "LittleFS.h"

class Storage {

public:
    Storage(String path = "", Print *logger = &Serial) :
        m_storagePath(path),
        p_logger(logger),
        p_fs(&LittleFS)
    {
        begin();
    }

    void setLogger(Print *logger) {
        p_logger = logger;
    }

    bool reset()
    {
        if (m_storagePath == "") {
            return false;
        } else {
            return p_fs->remove(m_storagePath);
        }
    }

    template <class JsonBuffer>
    JsonObject& loadJson(JsonBuffer& jsonBuffer) const
    {
        if (m_storagePath == "") {
            return jsonBuffer.createObject();
        } else {
            return Storage::loadJson(jsonBuffer, m_storagePath, p_logger, p_fs);
        }
    }

    template <class JsonBuffer>
    static JsonObject& loadJson(JsonBuffer& jsonBuffer, String path, Print *logger = &Serial, FS *fs = &LittleFS)
    {
        if (!fs->exists(path)) {
            return jsonBuffer.createObject();
        }

        File file = fs->open(path, "r");
        if (!file) {
            logger->println("[Storage] file open failed");
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
            return Storage::writeJson(jsonObject, m_storagePath, p_logger, p_fs);
        }
    }

    static bool writeJson(JsonObject& jsonObject, String path, Print *logger = &Serial, FS *fs = &LittleFS)
    {
        if (!jsonObject.success()) {
            return false;
        }

        File file = fs->open(path, "w");
        if (!file) {
            logger->println("[Storage] file open failed");
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
    void begin() {
        p_fs->begin();
    }

    String m_storagePath;
    Print *p_logger;
    FS *p_fs;
};

#endif