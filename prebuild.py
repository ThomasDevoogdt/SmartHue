import sys
import importlib
import json

# set git version
sys.path.insert(0, "smartHuePy/version/gitversion/src")
git_version_module = importlib.import_module("gitversionbuilder.main")
git_version_module.create_version_file(
    git_directory=".",
    output_file="src/build/version.h",
    lang="cpp"
)

# set security
with open("config.json", "r") as config_file:
    security = json.loads(config_file.read())["security"]
security_file_body = '''#pragma once
#ifndef ESP_PASS_H
#define ESP_PASS_H

namespace pass {{
constexpr const char* SOFT_AP_PASS = \"{soft_ap_pass}\";
constexpr const char* OTA_AP_PASS = \"{ota_ap_pass}\";

constexpr const char* WWW_USER = \"{www_user}\";
constexpr const char* WWW_PASS = \"{www_pass}\";
}}

#endif
'''.format(**security)
with open("src/secure/pass.h", "w") as security_file:
    security_file.write(security_file_body)
