#include "Config/Config.h"
#include "Storage/Storage.h"
#include <ArduinoExtension.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServerSecure.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiAP.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <Logger.h>
#include <Syslog.h>
#include <Ticker.h>
#include <build/version.h>
#include <secure/pass.h>
#include <secure/ssl.h>



bool setupOTA();
bool setupWiFi(int timeoutConfigAp = 1000 * 600, int timeoutConnection = 1000 * 120);
bool beginWiFi();
bool reconnectWiFi(bool force = false);

void setupConfigAp();
void resetConfigAp();

void setupDnsServer();
void resetDnsServer();
void serveDnsServer();

void setupMDNS();
void serveMDNS();

void setupWebServer();
void resetWebServer();
void serveWebServer();

const String deviceId = "SmartHue-" + String(ESP.getChipId(), HEX);
const String repoUrl = "https://github.com/ThomasDevoogdt/SmartHue";

#define LOG_ID deviceId.c_str()

#define WIFI_AP_SSID deviceId.c_str()
#define WIFI_AP_PASS pass::SOFT_AP_PASS
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_HIDDEN false

#define OTA_HOSTNAME deviceId.c_str()
#define OTA_PASS pass::OTA_AP_PASS

#define MDNS_ID deviceId.c_str()

#define WWW_USER pass::WWW_USER
#define WWW_PASS pass::WWW_PASS

#define LOCAL_IP IPAddress(10, 0, 1, 1)
#define GATEWAY_IP IPAddress(10, 0, 1, 1)
#define SUBNET_IP IPAddress(255, 255, 255, 0)

unsigned long loopLastTime = micros();
float loopFrequency = 0;

class GlobalVar {
public:
    GlobalVar() :
        logger(LOG_ID),
        config(&logger),
        bootTimeStorage("/tmp/boot.json", &logger),
        otaLogStorage("/tmp/ota.json", &logger) {}

    Logger logger;
    Config config;
    Ticker ticker;

    std::unique_ptr<DNSServer> dnsServer;
    std::unique_ptr<BearSSL::ESP8266WebServerSecure> server;

    WiFiUDP syslogUdpClient;
    std::unique_ptr<Syslog> syslog;

    LinkedList<void (*)()> loopList;

    Storage bootTimeStorage;
    Storage otaLogStorage;
} *p_var;

// relay board pins
const uint8_t pins_arr[2] = { D6, D7 };

bool tryWiFiReconnect = false;

String macToString(const uint8 *mac) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

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
        auto &loopList = p_var->loopList;
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
    auto &loopList = p_var->loopList;
    for (int i = 0; i < loopList.size(); i++) {
        if (loopList.get(i) == cb) {
            return;
        }
    }
    loopList.add(cb);
}

void removeLoopListCb(void (*cb)())
{
    auto &loopList = p_var->loopList;
    for (int i = 0; i < loopList.size(); i++) {
        if (loopList.get(i) == cb) {
            loopList.remove(i);
            return;
        }
    }
}

void clearLoopListCb()
{
    auto &loopList = p_var->loopList;
    loopList.clear();
}

bool setPin(JsonObject& jsonRoot)
{
    auto &logger = p_var->logger;
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
    auto &logger = p_var->logger;
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
    auto &logger = p_var->logger;
    logger.log("[sysinfo] system build date: " + String(__DATE__));
    logger.log("[sysinfo] system build time: " + String(__TIME__));
    logger.log("[sysinfo] system version: " + String(version::VERSION_STRING));
    logger.log("[sysinfo] system commit id: " + String(version::GIT_COMMIT_ID));
}

void showConfig()
{
    auto &logger = p_var->logger;
    auto &config = p_var->config;
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
    auto &logger = p_var->logger;
    auto &dnsServer = p_var->dnsServer;
    logger.log("[Setup DNS server]");
    dnsServer.reset(new DNSServer());
    dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer->start(53, "*", LOCAL_IP);
    setLoopListCb(serveDnsServer);
}

void resetDnsServer()
{
    auto &logger = p_var->logger;
    auto &dnsServer = p_var->dnsServer;
    logger.log("[Reset DNS server]");
    dnsServer.reset();
    removeLoopListCb(serveDnsServer);
}

void serveDnsServer()
{
    auto &dnsServer = p_var->dnsServer;
    dnsServer->processNextRequest();
}

void setupWebServer()
{
    auto &logger = p_var->logger;
    auto &server = p_var->server;
    logger.log("[Setup Webserver]");
    server.reset(new BearSSL::ESP8266WebServerSecure(443));
    // server->getServer().setRSACert(new BearSSL::X509List(ssl::serverCert), new BearSSL::PrivateKey(ssl::serverKey));
    server->getServer().setECCert(new BearSSL::X509List(ssl::serverCert), BR_KEYTYPE_EC, new BearSSL::PrivateKey(ssl::serverKey));

    server->on("/", HTTP_GET, []() {
        auto &logger = p_var->logger;
        auto &server = p_var->server;
        logger.log("[Webserver] serve /");
        String response =
            "<!DOCTYPE html>"
            "<html>"
            "<body>"
            "<h1>" + deviceId + "</h1>"
            "<p>Welcome to " + deviceId + ", please have a look at <a href=\"" + repoUrl + "\">" + repoUrl + "</a> for more information.</p>"
            "<h2>GET</h2>"
            "<ul>"
            "<li><a href=\"https://" + deviceId + ".local/api/version\">/api/version</a></li>"
            "<li><a href=\"https://" + deviceId + ".local/api/systeminfo\">/api/systeminfo</a></li>"
            "<li><a href=\"https://" + deviceId + ".local/api/config\">/api/config</a></li>"
            "</ul>"
            "</body>"
            "</html>";
        server->send(200, "text/html", response);
    });

    server->on("/api/version", HTTP_GET, []() {
        auto &logger = p_var->logger;
        auto &server = p_var->server;
        auto &config = p_var->config;
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
        auto &logger = p_var->logger;
        auto &server = p_var->server;
        if (!server->authenticate(WWW_USER, WWW_PASS)) {
            return server->requestAuthentication();
        }
        logger.log("[Webserver] serve /api/reboot");
        server->send(200, "text/html", "ok");
        server->client().flush();
        resetDnsServer();
        resetWebServer();
        digitalWrite(pins_arr, LOW); // prevent to fast flicker by early power-off
        delay(2000);
        ESP.restart();
    });

    server->on("/api/systeminfo", HTTP_GET, []() {
        auto &logger = p_var->logger;
        auto &server = p_var->server;
        auto &bootTimeStorage = p_var->bootTimeStorage;
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
        auto &logger = p_var->logger;
        auto &server = p_var->server;
        auto &config = p_var->config;
        auto &bootTimeStorage = p_var->bootTimeStorage;
        if (!server->authenticate(WWW_USER, WWW_PASS)) {
            return server->requestAuthentication();
        }
        logger.log("[Webserver] serve /api/config/reset");
        server->send(200, "text/html", "ok");
        server->client().flush();
        config.reset();
        WiFi.disconnect(true);
        resetDnsServer();
        resetWebServer();
        bootTimeStorage.reset();
        showConfig();
        ESP.reset();
    });

    server->on("/api/config/reload", HTTP_GET, []() {
        auto &logger = p_var->logger;
        auto &server = p_var->server;
        if (!server->authenticate(WWW_USER, WWW_PASS)) {
            return server->requestAuthentication();
        }
        logger.log("[Webserver] serve /api/config/reload");
        server->send(200, "text/html", "ok");
        tryWiFiReconnect = true;
    });

    server->on("/api/config", HTTP_POST, []() {
        auto &logger = p_var->logger;
        auto &server = p_var->server;
        auto &config = p_var->config;
        if (!server->authenticate(WWW_USER, WWW_PASS)) {
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
        auto &logger = p_var->logger;
        auto &server = p_var->server;
        auto &config = p_var->config;
        if (!server->authenticate(WWW_USER, WWW_PASS)) {
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
        auto &logger = p_var->logger;
        auto &server = p_var->server;
        if (!server->authenticate(WWW_USER, WWW_PASS)) {
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
        auto &logger = p_var->logger;
        auto &server = p_var->server;
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
        auto &logger = p_var->logger;
        auto &server = p_var->server;
        auto &otaLogStorage = p_var->otaLogStorage;
        logger.log("[Webserver] serve /api/ota");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& rootObject = otaLogStorage.loadJson(jsonBuffer);
        otaLogStorage.reset();
        String response;
        rootObject.prettyPrintTo(response);
        server->send(200, "application/json", response);
    });

    server->begin();
    setLoopListCb(serveWebServer);
}

void resetWebServer()
{
    auto &logger = p_var->logger;
    auto &server = p_var->server;
    logger.log("[Reset Webserver]");
    server.reset();
    removeLoopListCb(serveWebServer);
}

void serveWebServer()
{
    auto &server = p_var->server;
    server->handleClient();
    server->client().flush();
}

void setupConfigAp()
{
    auto &logger = p_var->logger;
    logger.log(F("[Setup config AP]"));
    delay(100);
    WiFi.persistent(false);
    WiFi.disconnect();
    WiFi.softAPdisconnect();
    WiFi.mode(WIFI_AP);

    logger.log(F("[Setup config AP] configuring access point... "));
    WiFi.softAPConfig(LOCAL_IP, GATEWAY_IP, SUBNET_IP); 
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHANNEL, WIFI_AP_HIDDEN);
    WiFi.onSoftAPModeProbeRequestReceived([](const WiFiEventSoftAPModeProbeRequestReceived &dst) {
        auto &logger = p_var->logger;
        logger.log("[Connect config AP] mac = " + macToString(dst.mac) + ", rssi = " + dst.rssi + ": probe request received");
    });
    WiFi.onSoftAPModeStationConnected([](const WiFiEventSoftAPModeStationConnected &dst) {
        auto &logger = p_var->logger;
        logger.log("[Connect config AP] mac = " + macToString(dst.mac) + ", aid = " + dst.aid + ": station connected");
    });
    WiFi.onSoftAPModeStationDisconnected([](const WiFiEventSoftAPModeStationDisconnected &dst) {
        auto &logger = p_var->logger;
        logger.log("[Connect config AP] mac = " + macToString(dst.mac) + ", aid = " + dst.aid + ": station disconnected");
    });
    WiFi.begin();

    delay(500);
    logger.log("[Config config AP] IP address: " + WiFi.softAPIP().toString());
}

void resetConfigAp()
{
    auto &logger = p_var->logger;
    logger.log(F("[Reset config AP] reset config AP"));
    WiFi.disconnect();
    WiFi.softAPdisconnect();
    WiFi.mode(WIFI_STA);
    WiFi.persistent(true);
}

bool beginWiFi() 
{
    auto &logger = p_var->logger;
    auto &config = p_var->config;
    logger.log(F("[WiFi] begin WiFi connection..."), Logger::DEBUG);

    String ssid, pass;
    config.getWifiConfig(ssid, pass);
    if (ssid.isEmpty()) {
        logger.log(F("[WiFi] no SSID configured yet, skipping connection"), Logger::WARN);
        return false;
    }

    logger.log(F("[WiFi] SSID found, begin connection attempt"), Logger::DEBUG);
    WiFi.persistent(false); // 2.2.0 Exception (3): #1997
    WiFi.disconnect(true);
    // Begin wifi after disconnecting, to solve a weird bug reported at
    // https://github.com/esp8266/Arduino/issues/1997#issuecomment-436673828
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    return true; 
}

bool reconnectWiFi(bool force)
{
    auto &logger = p_var->logger;
    if (WiFi.isConnected()) {
        logger.log("[WIFI] connected, IP address: " + WiFi.localIP().toString());
        return true;
    } 

    logger.log(F("[WiFi] attempting WiFi connection..."), Logger::DEBUG);
    if (force) {
        WiFi.reconnect();
    }

    if (doWhileLoopDelay(10000, []() { return WiFi.isConnected(); })) {
        logger.log("[WIFI] connected, IP address: " + WiFi.localIP().toString());
        return true;
    } else {
        return false;
    }
}

bool setupWiFi(int timeoutConfigAp, int timeoutConnection)
{
    auto &logger = p_var->logger;
    bool (*tryToConnect)(unsigned long) = [](unsigned long timeout) {
        if(!beginWiFi()){
            return false;
        }
        return doWhileLoopDelay(timeout / 2, []() {
            return reconnectWiFi(false);
        });
    };

    logger.log(F("[Wifi Setup]"));
    if (tryToConnect(timeoutConnection)) {
        logger.log(F("[Wifi Setup] direct connection, return"));
        return true;
    }

    setupConfigAp();
    setupDnsServer();

    do {
        callLoopList();
    } while (!tryWiFiReconnect);
    tryWiFiReconnect = false;

    logger.log(F("[Wifi Setup] try wifi reconnect"));
    callLoopList();
    resetConfigAp();
    resetDnsServer();
    return tryToConnect(timeoutConnection);
}

bool setupSyslog()
{
    auto &logger = p_var->logger;
    auto &config = p_var->config;
    auto &syslog = p_var->syslog;
    auto &syslogUdpClient = p_var->syslogUdpClient;
    logger.log(F("[Syslog Setup]"));
    String syslog_server;
    int syslog_port;
    config.getSyslogConfig(syslog_server, syslog_port);
    IPAddress ipAddress;
    ipAddress.fromString(syslog_server);
    syslog.reset(new Syslog(syslogUdpClient, ipAddress, syslog_port, LOG_ID));
    return true;
}

bool setupOTA()
{
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASS);
    ArduinoOTA.begin();
    ArduinoOTA.onStart([]() {
        auto &logger = p_var->logger;
        auto &ticker = p_var->ticker;
        auto &otaLogStorage = p_var->otaLogStorage;
        ticker.attach(0.2, tick);
        logger.log("[OTA] start: " + String(ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem"));
        otaLogStorage.reset();
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        auto &logger = p_var->logger;
        logger.log("[OTA] progress: " + String(progress / (total / 100)));
    });
    ArduinoOTA.onEnd([]() {
        auto &logger = p_var->logger;
        auto &ticker = p_var->ticker;
        auto &otaLogStorage = p_var->otaLogStorage;
        ticker.detach();
        logger.log("[OTA] end");

        DynamicJsonBuffer jsonBuffer;
        JsonObject& rootObject = jsonBuffer.createObject();
        rootObject.set("ota", "success");
        otaLogStorage.writeJson(rootObject);

        digitalWrite(pins_arr, LOW); // prevent to fast flicker by early power-off
        delay(2000);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        auto &logger = p_var->logger;
        auto &otaLogStorage = p_var->otaLogStorage;
        logger.log("[OTA] error (code: " + String(error) + ")", Logger::ERROR);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& rootObject = jsonBuffer.createObject();
        switch (error) {
        case OTA_AUTH_ERROR:
            logger.log(F("[OTA] Auth Failed"), Logger::ERROR);
            rootObject.set(F("ota"), F("Auth Failed"));
            break;
        case OTA_BEGIN_ERROR:
            logger.log(F("[OTA] Begin Failed"), Logger::ERROR);
            rootObject.set(F("ota"), F("Begin Failed"));
            break;
        case OTA_CONNECT_ERROR:
            logger.log(F("[OTA] Connect Failed"), Logger::ERROR);
            rootObject.set(F("ota"), F("Connect Failed"));
            break;
        case OTA_RECEIVE_ERROR:
            logger.log(F("[OTA] Receive Failed"), Logger::ERROR);
            rootObject.set(F("ota"), F("Receive Failed"));
            break;
        case OTA_END_ERROR:
            logger.log(F("[OTA] End Failed"), Logger::ERROR);
            rootObject.set(F("ota"), F("End Failed"));
            break;
        }
        otaLogStorage.writeJson(rootObject);
    });
    setLoopListCb([]() {
        ArduinoOTA.handle();
    });
    return true;
}

void setupMDNS()
{
    auto &logger = p_var->logger;
    String id = MDNS_ID;
    id.toLowerCase();
    logger.log("[mDNS] setup local dns: https://" + id + ".local/");
    MDNS.begin(id);
    MDNS.addService("http", "tcp", 80); // ota
    MDNS.addService("https", "tcp", 443);
    setLoopListCb(serveMDNS);
}

void serveMDNS()
{
    if(!MDNS.isRunning()) {
        String id = MDNS_ID;
        id.toLowerCase();
        MDNS.begin(id);
        MDNS.announce();
    }
    MDNS.update();
}

void setup()
{
    // first things first ...
    // forces a closed relay,
    // this guarantees an equal working when a user turns the light on
    pinMode(pins_arr, OUTPUT);
    digitalWrite(pins_arr, HIGH);

    // start serial session
    Serial.begin(115200);
    Serial.println("[setup] setup workspace");
    p_var = new GlobalVar();
    auto &logger = p_var->logger;
    logger.log("[setup] initialize and close the relays done!");

    // if a crash occurs, then do not immediately try to restart,
    // otherwise the releys are flickering all the time
    rst_info *reset_info = ESP.getResetInfoPtr();
    switch (reset_info ? reset_info->reason : rst_reason::REASON_DEFAULT_RST) {
    case rst_reason::REASON_WDT_RST:
    case rst_reason::REASON_EXCEPTION_RST:
    case rst_reason::REASON_SOFT_WDT_RST:
        logger.log("[setup] exceptional reset occurred: " + ESP.getResetReason() + ", I'm going to sleep ... ");
        while (true) {
            delay(1000);
        }
        break;
    default:
        break;
    }

    // update boot count
    DynamicJsonBuffer jsonBuffer;
    JsonObject& jsonObjectRoot = p_var->bootTimeStorage.loadJson(jsonBuffer);
    jsonObjectRoot.set("bootcount", 1 + (jsonObjectRoot["bootcount"] | 0));
    p_var->bootTimeStorage.writeJson(jsonObjectRoot);

    // register loggers
    logger.resetDefaultLogger();
    logger.registerLogger(
        [](const String& message) {
            Serial.println(message);
        },
        Logger::DEBUG);
    logger.log("[setup] serial log registered");
    logger.registerLogger(
        [](const String& message) {
            if (p_var->syslog) {
                p_var->syslog->log(LOG_INFO, message);
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
    p_var->ticker.attach(0.6, tick);

    showConfig();
    showSystemInfo();

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
    p_var->ticker.detach();
    digitalWrite(LED_BUILTIN, LOW);

    setLoopListCb([]() {
        // keep WiFi active
        if (!WiFi.isConnected()) {
            reconnectWiFi(true);
        }
    });

    showSystemInfo(); // show it again because we're now connected to Syslog
    setupMDNS(); // do this setup again to ensure a properly working system

    logger.log("[setup] Setup done! Entering loop mode ...");
}

void loop()
{
    callLoopList();
}