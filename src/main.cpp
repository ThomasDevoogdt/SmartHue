#include "Config/Config.h"
#include "Storage/Storage.h"
#include <ArduinoExtension.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServerSecure.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <Logger.h>
#include <Syslog.h>
#include <Ticker.h>
#include <build/version.h>
#include <secure/pass.h>
#include <secure/ssl.h>

bool setupOTA();
bool setupWiFi(int timeoutConfigAp = 1000 * 600, int timeoutConnection = 1000 * 30);
bool reconnectWiFi(bool begin = false, bool force = false);

void setupDnsServer();
void resetDnsServer();
void serveDnsServer();

void setupWebServer();
void resetWebServer();
void serveWebServer();

const String deviceId = "SmartHue-" + String(ESP.getChipId(), HEX);

unsigned long loopLastTime = micros();
float loopFrequency = 0;

Config config;
Logger logger(deviceId);
Ticker ticker;

std::unique_ptr<DNSServer> dnsServer;
std::unique_ptr<BearSSL::ESP8266WebServerSecure> server;

WiFiUDP syslogUdpClient;
std::unique_ptr<Syslog> syslog;

LinkedList<void (*)()> loopList;

Storage bootTimeStorage("tmp/boot.json");
Storage otaLogStorage("tmp/ota.json");

// relay board pins
const uint8_t pins_arr[2] = { D6, D7 };

bool tryWiFiReconnect = false;

/**
 * Toggle blink led
 *
 */
void tick()
{
    //toggle state
    int state = digitalRead(LED_BUILTIN); // get the current state of GPIO1 pin
    digitalWrite(LED_BUILTIN, !state); // set pin to the opposite state
}

void callLoopList(unsigned long delay = 0)
{
    doWhileLoopDelay(delay, []() {
        // execute all registered loop callbacks
        for (int i = 0; i < loopList.size(); i++) {
            (loopList.get(i))();
        }

        // loop freqency statistics
        unsigned long timeDiff = micros() - loopLastTime;
        if (timeDiff < ULONG_MAX * 0.9) { // skip overflow error
            loopFrequency = 0.95 * loopFrequency + 0.05 / timeDiff;
        }
        loopLastTime = micros();
    });
}

void setLoopListCb(void (*cb)())
{
    for (int i = 0; i < loopList.size(); i++) {
        if (loopList.get(i) == cb) {
            return;
        }
    }
    loopList.add(cb);
}

void removeLoopListCb(void (*cb)())
{
    for (int i = 0; i < loopList.size(); i++) {
        if (loopList.get(i) == cb) {
            loopList.remove(i);
            return;
        }
    }
}

void clearLoopListCb()
{
    loopList.clear();
}

bool setPin(JsonObject& jsonRoot)
{
    if (!jsonRoot.success()) {
        logger.log("[SetPin] parseObject() failed", Logger::ERROR);
        return false;
    }

    if (!jsonRoot.containsKey("relay") || !jsonRoot["relay"].is<int>()
        || (jsonRoot.containsKey("value")
               && (!(jsonRoot["value"].is<int>() || jsonRoot["value"].is<bool>())))) {
        logger.log("[SetPin] The keys \"relay\" (int) and \"value\" (int/bool) are not provided.", Logger::ERROR);
        return false;
    }

    uint8_t relay, pin, value;

    // relay to pin
    relay = (uint8_t)jsonRoot["relay"];
    switch (relay) {
    case 1:
        pin = pins_arr[0];
        break;
    case 2:
        pin = pins_arr[1];
        break;
    default:
        return false;
    }

    // get new value
    if (jsonRoot.containsKey("value")) {
        if (jsonRoot["value"].is<int>()) {
            value = (uint8_t)jsonRoot["value"].as<int>();
        } else {
            value = (uint8_t)jsonRoot["value"].as<bool>();
        }
    } else {
        value = !digitalRead(pin);
    }

    logger.log("[SetPin] " + String(value ? "Open" : "Close") + " relay " + String(relay) + " (GPIO: " + String(pin) + ")");
    pinMode(pin, OUTPUT);
    digitalWrite(pin, value);
    return true;
}

bool getPin(JsonObject& jsonRoot)
{
    if (!jsonRoot.success()) {
        logger.log("[GetPin] parseObject() failed", Logger::ERROR);
        return false;
    }

    if (!jsonRoot.containsKey("relay")
        || !jsonRoot["relay"].is<int>()) {
        logger.log("[SetPin] The key \"relay\" (int) is not provided.", Logger::ERROR);
        return false;
    }

    uint8_t relay, pin;
    relay = (uint8_t)jsonRoot["relay"];
    switch (relay) {
    case 1:
        pin = pins_arr[0];
        break;
    case 2:
        pin = pins_arr[1];
        break;
    default:
        return false;
    }

    jsonRoot.set("value", (bool)digitalRead(pin));
    return true;
}

void showSystemInfo()
{
    logger.log("[sysinfo] system build date: " + String(__DATE__));
    logger.log("[sysinfo] system build time: " + String(__TIME__));
    logger.log("[sysinfo] system version: " + String(version::VERSION_STRING));
    logger.log("[sysinfo] system commit id: " + String(version::GIT_COMMIT_ID));
}

void showConfig()
{
    logger.log("[config] load config");
    logger.log("[config] version: " + config.getConfigVersion());

    String wifi_ssid, wifi_pass;
    config.getWifiConfig(wifi_ssid, wifi_pass);
    String wifi_mask = String(wifi_pass.length() ? wifi_pass[0] : '*');
    for (size_t i = 1; i < wifi_pass.length(); i++) {
        wifi_mask += '*';
    }
    logger.log("[config] wifi ssid: " + wifi_ssid);
    logger.log("[config] wifi pass: " + wifi_mask);

    String syslog_server;
    int syslog_port;
    config.getSyslogConfig(syslog_server, syslog_port);
    logger.log("[config] syslog ip: " + syslog_server);
    logger.log("[config] syslog port: " + String(syslog_port));
}

void setupDnsServer()
{
    logger.log("[Setup DNS server]");
    dnsServer.reset(new DNSServer());
    dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer->start(53, "*", IPAddress(10, 0, 1, 1));
    setLoopListCb(serveDnsServer);
}

void resetDnsServer()
{
    logger.log("[Reset DNS server]");
    dnsServer.reset();
    removeLoopListCb(serveDnsServer);
}

void serveDnsServer()
{
    dnsServer->processNextRequest();
}

void setupWebServer()
{
    logger.log("[Setup Webserver]");
    server.reset(new BearSSL::ESP8266WebServerSecure(443));
    server->setRSACert(new BearSSLX509List(ssl::serverCert), new BearSSLPrivateKey(ssl::serverKey));

    server->on("/api/version", HTTP_GET, []() {
        logger.log("[Webserver] serve /api/version");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& rootObject = jsonBuffer.createObject();

        JsonObject& jsonObjectApi = rootObject.createNestedObject("api");
        jsonObjectApi.set("version", config.getConfigVersion());

        JsonObject& jsonObjectVersion = rootObject.createNestedObject("software");
        jsonObjectVersion.set(F("version"), version::VERSION_STRING);
        jsonObjectVersion.set(F("git_tag"), version::GIT_TAG_NAME);
        jsonObjectVersion.set(F("git_commits_since_tag"), version::GIT_COMMITS_SINCE_TAG);
        jsonObjectVersion.set(F("git_commit_id"), version::GIT_COMMIT_ID);
        jsonObjectVersion.set(F("modified_since_commit"), version::MODIFIED_SINCE_COMMIT);
        jsonObjectVersion.set(F("is_dev_version"), version::IS_DEV_VERSION);
        jsonObjectVersion.set(F("is_stable_version"), version::IS_STABLE_VERSION);
        jsonObjectVersion.set(F("build_date"), __DATE__);
        jsonObjectVersion.set(F("build_time"), __TIME__);

        String response;
        rootObject.prettyPrintTo(response);
        server->send(200, "application/json", response);
    });

    server->on("/api/reboot", HTTP_GET, []() {
        if (!server->authenticate(pass::WWW_USER, pass::WWW_PASS)) {
            return server->requestAuthentication();
        }
        logger.log("[Webserver] serve /api/reboot");
        server->send(200, "text/html", "ok");
        server->client().flush();
        resetDnsServer();
        resetWebServer();
        ESP.restart();
    });

    server->on("/api/systeminfo", HTTP_GET, []() {
        logger.log("[Webserver] serve /api/systeminfo");
        DynamicJsonBuffer jsonBuffer;
        int bootCount = bootTimeStorage.loadJson(jsonBuffer)["bootcount"];

        JsonObject& rootObject = jsonBuffer.createObject();
        rootObject.set(F("chip_id"), ESP.getChipId());
        rootObject.set(F("power_voltage"), (float)ESP.getVcc() / 1024.00f);
        rootObject.set(F("boot_mode"), ESP.getBootMode());
        rootObject.set(F("boot_version"), ESP.getBootVersion());
        rootObject.set(F("boot_count"), bootCount);
        rootObject.set(F("core_version"), ESP.getCoreVersion());
        rootObject.set(F("cpu_freq_mhz"), ESP.getCpuFreqMHz());
        rootObject.set(F("cycle_count"), ESP.getCycleCount());
        rootObject.set(F("flash_chip_id"), ESP.getFlashChipId());
        rootObject.set(F("flash_chip_mode"), ESP.getFlashChipMode());
        rootObject.set(F("flash_chip_real_size"), ESP.getFlashChipRealSize());
        rootObject.set(F("flash_chip_size"), ESP.getFlashChipSize());
        rootObject.set(F("flash_chip_size_by_chip_id"), ESP.getFlashChipSizeByChipId());
        rootObject.set(F("flash_chip_speed"), ESP.getFlashChipSpeed());
        rootObject.set(F("free_heap"), ESP.getFreeHeap());
        rootObject.set(F("free_sketch_space"), ESP.getFreeSketchSpace());
        rootObject.set(F("reset_info"), ESP.getResetInfo());
        rootObject.set(F("reset_info_depc"), ESP.getResetInfoPtr()->depc);
        rootObject.set(F("reset_info_epc1"), ESP.getResetInfoPtr()->epc1);
        rootObject.set(F("reset_info_epc2"), ESP.getResetInfoPtr()->epc2);
        rootObject.set(F("reset_info_epc3"), ESP.getResetInfoPtr()->epc3);
        rootObject.set(F("reset_info_exccause"), ESP.getResetInfoPtr()->exccause);
        rootObject.set(F("reset_info_excvaddr"), ESP.getResetInfoPtr()->excvaddr);
        rootObject.set(F("reset_info_reason"), ESP.getResetInfoPtr()->reason);
        rootObject.set(F("reset_reason"), ESP.getResetReason());
        rootObject.set(F("sdk_version"), ESP.getSdkVersion());
        rootObject.set(F("sketch_md5"), ESP.getSketchMD5());
        rootObject.set(F("sketch_size"), ESP.getSketchSize());
        rootObject.set(F("loop_freq_mhz"), loopFrequency);
        rootObject.set(F("ip_address"), WiFi.localIP().toString());
        rootObject.set(F("device_id"), deviceId);
        String response;
        rootObject.prettyPrintTo(response);
        server->send(200, "application/json", response);
    });

    server->on("/api/config/reset", HTTP_GET, []() {
        if (!server->authenticate(pass::WWW_USER, pass::WWW_PASS)) {
            return server->requestAuthentication();
        }
        logger.log("[Webserver] serve /api/config/reset");
        server->send(200, "text/html", "ok");
        server->client().flush();
        config.reset();
        WiFi.disconnect(true);
        resetDnsServer();
        resetWebServer();
        bootTimeStorage.remove();
        showConfig();
        ESP.reset();
    });

    server->on("/api/config/reload", HTTP_GET, []() {
        if (!server->authenticate(pass::WWW_USER, pass::WWW_PASS)) {
            return server->requestAuthentication();
        }
        logger.log("[Webserver] serve /api/config/reload");
        server->send(200, "text/html", "ok");
        tryWiFiReconnect = true;
    });

    server->on("/api/config", HTTP_POST, []() {
        if (!server->authenticate(pass::WWW_USER, pass::WWW_PASS)) {
            return server->requestAuthentication();
        }
        logger.log("[Webserver] serve /api/config");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& rootObject = jsonBuffer.parseObject(server->arg("plain"));
        if (!rootObject.success()) {
            server->send(400, "text/html", "invalid json object");
            return;
        }

        bool success = false;
        if (rootObject.containsKey("wifi")) {
            JsonObject& jsonobjectWifi = rootObject.get<JsonVariant>("wifi").as<JsonObject>();
            String ssid = jsonobjectWifi["ssid"] | "";
            String pass = jsonobjectWifi["pass"] | "";
            config.setWifiConfig(ssid, pass);
            success = true;
        }

        if (rootObject.containsKey("syslog")) {
            JsonObject& jsonobjectSyslog = rootObject.get<JsonVariant>("syslog").as<JsonObject>();
            String ip = jsonobjectSyslog["ip"] | "";
            int port = jsonobjectSyslog["port"] | 514;
            config.setSyslogConfig(ip, port);
            success = true;
        }

        if (success) {
            showConfig();
            server->send(200, "text/html", "ok");
        } else {
            server->send(400, "text/html", "no valid config found");
        }
    });

    server->on("/api/config", HTTP_GET, []() {
        if (!server->authenticate(pass::WWW_USER, pass::WWW_PASS)) {
            return server->requestAuthentication();
        }
        logger.log("[Webserver] serve /api/config");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& rootObject = jsonBuffer.createObject();

        rootObject.set("version", config.getConfigVersion());

        String wifi_ssid, wifi_pass;
        config.getWifiConfig(wifi_ssid, wifi_pass);
        String wifi_mask = String(wifi_pass.length() ? wifi_pass[0] : '*');
        for (size_t i = 1; i < wifi_pass.length(); i++) {
            wifi_mask += '*';
        }
        JsonObject& jsonobjectWifi = rootObject.createNestedObject("wifi");
        jsonobjectWifi.set("ssid", wifi_ssid);
        jsonobjectWifi.set("pass", wifi_mask);

        String syslog_server;
        int syslog_port;
        config.getSyslogConfig(syslog_server, syslog_port);
        JsonObject& jsonobjectSyslog = rootObject.createNestedObject("syslog");
        jsonobjectSyslog.set("ip", syslog_server);
        jsonobjectSyslog.set("port", syslog_port);

        String response;
        rootObject.prettyPrintTo(response);
        server->send(200, "application/json", response);
    });

    server->on("/api/set", HTTP_POST, []() {
        if (!server->authenticate(pass::WWW_USER, pass::WWW_PASS)) {
            return server->requestAuthentication();
        }
        logger.log("[Webserver] serve /api/set");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& rootObject = jsonBuffer.parseObject(server->arg("plain"));

        if (setPin(rootObject)) {
            server->send(200, "text/html", "ok");
        } else {
            server->send(400, "text/html", "no valid json set object");
        }
    });

    server->on("/api/get", HTTP_GET, []() {
        logger.log("[Webserver] serve /api/get");
        DynamicJsonBuffer jsonBuffer;
        // JsonObject& rootObject = jsonBuffer.parseObject(server->arg("plain"));
        JsonObject& rootObject = jsonBuffer.createObject();
        rootObject.set("relay", server->arg("relay").toInt());

        if (getPin(rootObject)) {
            String response;
            rootObject.prettyPrintTo(response);
            server->send(200, "application/json", response);
        } else {
            server->send(400, "text/html", "no valid get request");
        }
    });

    server->on("/api/ota", HTTP_GET, []() {
        logger.log("[Webserver] serve /api/ota");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& rootObject = otaLogStorage.loadJson(jsonBuffer);
        otaLogStorage.remove();
        String response;
        rootObject.prettyPrintTo(response);
        server->send(200, "application/json", response);
    });

    server->begin();
    setLoopListCb(serveWebServer);
}

void resetWebServer()
{
    logger.log("[Reset Webserver]");
    server.reset();
    removeLoopListCb(serveWebServer);
}

void serveWebServer()
{
    server->handleClient();
    server->client().flush();
}

void setupConfigAp()
{
    logger.log("[Setup config AP]");
    delay(100);
    if (!WiFi.isConnected()) {
        WiFi.persistent(false);
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        WiFi.persistent(true);
    } else {
        WiFi.mode(WIFI_AP_STA);
    }

    logger.log("[Setup config AP] configuring access point... ");
    WiFi.softAPConfig(
        IPAddress(10, 0, 1, 1), // local ip
        IPAddress(10, 0, 1, 1), // gateway
        IPAddress(255, 255, 255, 0)); // subnet

    WiFi.softAP(
        deviceId.c_str(), // ssid
        pass::SOFT_AP_PASS, // pass
        1, false); // channel = 1, hidden = false

    delay(500);
    logger.log("[Config config AP] IP address: " + WiFi.softAPIP().toString());
}

void resetConfigAp()
{
    logger.log("[Reset config AP] reset config AP");
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
}

bool reconnectWiFi(bool begin, bool force)
{
    logger.log("[WiFi] attempting WiFi connection...", Logger::DEBUG);

    if (begin) {
        String ssid, pass;
        config.getWifiConfig(ssid, pass);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
    }

    if (force) {
        WiFi.reconnect();
    }

    if (WiFi.isConnected()) {
        logger.log("[WIFI] connected, IP address: " + WiFi.localIP().toString());
        return true;
    } else {
        return false;
    }
}

bool setupWiFi(int timeoutConfigAp, int timeoutConnection)
{
    bool (*tryToConnect)(unsigned long) = [](unsigned long timeout) {
        reconnectWiFi(true); // begin WiFi
        return doWhileLoopDelay(timeout, []() {
            delay(500);
            return reconnectWiFi(false);
        });
    };

    logger.log("[Wifi Setup]");
    if (tryToConnect(timeoutConnection)) {
        logger.log("[Wifi Setup] direct connection, return");
        return true;
    }

    setupConfigAp();
    setupDnsServer();

    doWhileLoopDelay(timeoutConfigAp, []() {
        callLoopList();
        return tryWiFiReconnect;
    });
    tryWiFiReconnect = false;

    logger.log("[Wifi Setup] try wifi reconnect");
    callLoopList();
    resetConfigAp();
    resetDnsServer();
    return tryToConnect(timeoutConnection);
}

bool setupSyslog()
{
    logger.log("[Syslog Setup]");
    String syslog_server;
    int syslog_port;
    config.getSyslogConfig(syslog_server, syslog_port);
    IPAddress ipAddress;
    ipAddress.fromString(syslog_server);
    syslog.reset(new Syslog(syslogUdpClient, ipAddress, syslog_port, deviceId.c_str()));
    return true;
}

bool setupOTA()
{
    ArduinoOTA.setHostname(deviceId.c_str());
    ArduinoOTA.setPassword(pass::OTA_AP_PASS);
    ArduinoOTA.begin();
    ArduinoOTA.onStart([]() {
        ticker.attach(0.2, tick);
        logger.log("[OTA] start");
        otaLogStorage.remove();
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        logger.log("[OTA] progress: " + String(progress / (total / 100)));
    });
    ArduinoOTA.onEnd([]() {
        ticker.detach();
        logger.log("[OTA] end");

        DynamicJsonBuffer jsonBuffer;
        JsonObject& rootObject = jsonBuffer.createObject();
        rootObject.set("ota", "success");
        otaLogStorage.writeJson(rootObject);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        logger.log("[OTA] error (code: " + String(error) + ")", Logger::ERROR);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& rootObject = jsonBuffer.createObject();
        switch (error) {
        case OTA_AUTH_ERROR:
            logger.log("[OTA] Auth Failed", Logger::ERROR);
            rootObject.set("ota", "Auth Failed");
            break;
        case OTA_BEGIN_ERROR:
            logger.log("[OTA] Begin Failed", Logger::ERROR);
            rootObject.set("ota", "Begin Failed");
            break;
        case OTA_CONNECT_ERROR:
            logger.log("[OTA] Connect Failed", Logger::ERROR);
            rootObject.set("ota", "Connect Failed");
            break;
        case OTA_RECEIVE_ERROR:
            logger.log("[OTA] Receive Failed", Logger::ERROR);
            rootObject.set("ota", "Receive Failed");
            break;
        case OTA_END_ERROR:
            logger.log("[OTA] End Failed", Logger::ERROR);
            rootObject.set("ota", "End Failed");
            break;
        }
        otaLogStorage.writeJson(rootObject);
    });
    setLoopListCb([]() {
        ArduinoOTA.handle();
    });
    return true;
}

bool setupMDNS()
{
    MDNS.begin(deviceId.c_str());
    MDNS.addService("http", "tcp", 80); // ota
    MDNS.addService("https", "tcp", 443);
    return true;
}

void setup()
{
    // first things first ...
    // forces a closed relay,
    // this guarantees an equal working when a user turns the light on
    logger.log("[setup] initialize and close the relays");
    pinMode(pins_arr, OUTPUT);
    digitalWrite(pins_arr, HIGH);

    // update boot count
    DynamicJsonBuffer jsonBuffer;
    JsonObject& jsonObjectRoot = bootTimeStorage.loadJson(jsonBuffer);
    jsonObjectRoot.set("bootcount", 1 + (jsonObjectRoot["bootcount"] | 0));
    bootTimeStorage.writeJson(jsonObjectRoot);

    // start serial session
    Serial.begin(115200);
    Serial.println();

    // register loggers
    logger.registerLogger(
        [](const String& message) {
            Serial.println(message);
        },
        Logger::DEBUG);
    logger.log("[setup] serial log registered");
    logger.registerLogger(
        [](const String& message) {
            if (syslog) {
                syslog->log(LOG_INFO, message);
            }
        },
        Logger::DEBUG);
    logger.log("[setup] syslog log registered");

    logger.log("[setup] starting Up ...");
    logger.log("[setup] esp serial number: " + deviceId);

    system_update_cpu_freq(SYS_CPU_160MHZ);

    // create a setup phase identification
    logger.log("[setup] attach builtin led ticker");
    pinMode(LED_BUILTIN, OUTPUT);
    ticker.attach(0.6, tick);

    showConfig();
    setupMDNS();
    setupWebServer();
    setupOTA();

    if (!setupWiFi()) {
        // could not connect to wifi
        // for security reasons, stop also hosting config portal.
        logger.log("[setup] wifi setup: could not connect during setup", Logger::ERROR);
        logger.log("[setup] wifi setup: go into endless loop", Logger::WARN);
        while (true) {
            delay(1000);
        }
    }

    setupSyslog();

    // keep LED on
    ticker.detach();
    digitalWrite(LED_BUILTIN, LOW);

    setLoopListCb([]() {
        // keep WiFi active
        if (!WiFi.isConnected()) {
            reconnectWiFi();
        }
    });

    showSystemInfo();
    logger.log("[setup] Setup done! Entering loop mode ...");
}

void loop()
{
    callLoopList();
}