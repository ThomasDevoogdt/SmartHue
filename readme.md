# SmartHue esp8266

Since a while, I wanted to create my own smart light. It's not a new concept, many similar repositories are already out there. But I couldn't find a firmware that also provides security and deployment.

I know, this is kind of free advertisement, but if you really want to know on what board I was developing: https://www.electrodragon.com/product/wifi-iot-relay-board-based-esp8266/


Clone this repo:
```
git clone --recurse-submodules https://github.com/ThomasDevoogdt/SmartHue.git
```

## 1. Deploy

##### Install PlatformIO Core (CLI)

Have a look at the well-documentated page of [platformio](https://platformio.org/):

    https://docs.platformio.org/en/latest/installation.html

##### Edid the ```config.json``` file:
Probably, You'll also need to change the ```ota_ap_pass``` in the ```platformio.ini``` file.
```json
{
    "security": {
        "soft_ap_pass": "4jBqXvQuEx7TBWfC",
        "ota_ap_pass": "QLZWyG9Q9JNmth9t",
        "www_user": "admin",
        "www_pass": "a2XEnxmsq7zm2B9P"
    }
}
```

### 1.1 Deploy: Using the serial interface. (The first time.)
```bash
platformio run --target upload
```
### 1.2. Deploy: Using OTA. (This can only be done if there's already a previous installation.)

Of course, its possible to manually upgrade every device with the ```platformio``` cli, but the preferred way is the usage of the deployment module.

##### Setup python environment:
```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

##### Edid the ```devices.json``` file.
```json
{
    "devices": {
        "deploy": [
            "SmartHue-12345a",
            "SmartHue-12345c",
            "SmartHue-12345d"
        ]
    }
}
```

##### Run the deploy script:
```bash
python deployer.py -d devices.json
```

## 2. Usage

Super, you've now managed to upload the firmware to the esp8266. The esp8266 will boot and create a WiFi hotspot. The SSID is equal than the hostname of the device. Write it down, maybe you'll need it for an OTA update. Now you can test the device by calling https://10.0.1.1/api/systeminfo.

For security reasons, the AP/hotspot disables itself after 10 minutes. A power cycle is needed to re-enable the AP.

## 2.1 Device API

- Base url: [https://SmartHue-12345a.local/](https://SmartHue-12345a.local/)
- User: ```admin```
- Pass: ```a2XEnxmsq7zm2B9P```

##### GET /api/version 
no authentification needed

response: json body

##### GET /api/reboot
response: plain text: ok

##### GET /api/systeminfo
no authentification needed

response: json body

##### GET/POST /api/config

response/body: json body
```json
{
    "wifi": {
        "ssid": "SSID",
        "pass": "PASS"
    },
    "syslog": {
        "ip": "SYSLOG-IP"
    }
}
```
The syslog key is optional.

##### GET /api/config/reload
Only needed to apply changes after ```/api/config```

response: plain text: ok

##### GET /api/config/reset
Reset the current config to its default settings.

response: plain text: ok

##### GET /api/get [optional: ?relay=1]
no authentification needed

response: json body
```json
{
  "relay": 1,
  "value": true
}
```

##### POST /api/set
body:
```json
{
  "relay": 1,
  "value": true
}
```

response: plain text: ok

## License

I'm not a juridical expert but please, be compliant with the license. If you like the project or write about it, please mention it. A simple link to this repo is enough.
