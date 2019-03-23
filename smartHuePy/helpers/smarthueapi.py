import requests
import json
import ssl
import urllib3

urllib3.disable_warnings(
    urllib3.exceptions.InsecureRequestWarning)


class SmartHueApi:
    def __init__(self, name, www_user, www_pass):
        self.hostname = name
        self.www_user = www_user
        self.www_pass = www_pass
        self.ssl_cert = None
        self.ssl_key = None

    def setSSLContext(self, certfile, keyfile):
        self.ssl_cert = certfile
        self.ssl_key = keyfile
        return self

    def reboot_device(self):
        return self._request_api("/api/reboot", "GET")

    def get_reload(self):
        return self._request_api("/api/config/reload", "GET")

    def get_reset(self):
        return self._request_api("/api/config/reset", "GET")

    def get_version(self):
        return self._request_api("/api/version", "GET")

    def get_system_info(self):
        return self._request_api("/api/systeminfo", "GET")

    def get_config(self):
        return self._request_api("/api/config", "GET")

    def post_config(self, config):
        return self._request_api("/api/config", "POST", config)

    def set_relay(self, relay, value=None):
        body = {"relay": relay}
        if value is not None:
            body["value"] = value
        return self._request_api("/api/set", "POST", body)

    def get_relay(self, relay):
        return self._request_api("/api/get?relay=%d" % relay, "GET")

    def get_ota(self):
        return self._request_api("/api/ota", "GET")

    def _request_api(self, path, method="GET", body=None):
        uri = "https://%s.local%s" % (self.hostname.lower(), path)
        request_args = {}
        request_args["timeout"] = 15
        request_args["auth"] = requests.auth.HTTPBasicAuth(
            self.www_user, self.www_pass)
        request_args["verify"] = False
        if body:
            request_args["data"] = json.dumps(body)

        try:
            if method == "GET":
                r = requests.get(uri, **request_args)
            if method == "POST":
                r = requests.post(uri, **request_args)
        except:
            # not sure if the device is up
            pass
        finally:
            if "r" in locals() and r.status_code == 200:
                try:
                    return r.json()
                except:
                    # not all api calls do return a json
                    return True
            else:
                return False
