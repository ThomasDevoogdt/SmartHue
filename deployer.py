import argparse
import json
import logging

from smartHuePy.helpers.helpers import SetupHelper, pio_run_upload, wait_helper, get_repo_version

logging.basicConfig(level=logging.INFO)


def parse_args():
    parser = argparse.ArgumentParser(description='SmartHue Deplorer')
    parser.add_argument('-d', '--devices', type=argparse.FileType('r'))
    return parser.parse_args()


def get_config(config):
    config_load = json.loads(config.read())
    config.close()
    return config_load


def main():
    args = parse_args()
    config = get_config(args.devices)
    hostnames = config["devices"]["deploy"]

    setup = SetupHelper()
    for name in hostnames:
        setup.add(name)

    for name in hostnames:
        device = setup.get(name)

        version = device.get_version()
        if not version:
            logging.info("%s: device is not up" % device.hostname)
            continue

        logging.info("%s: start upload" % device.hostname)
        boot_count = device.get_system_info()["boot_count"]
        pio_run_upload(device)

        def success():
            try:
                system_ota = device.get_ota()
                assert system_ota, "could not fetch ota info"
                assert system_ota["ota"] == "success", "ota failed"

                project_version = get_repo_version()
                device_version = device.get_version()
                assert project_version.git_commit_id == device_version["software"]["git_commit_id"]
                assert project_version.version_string in device_version["software"]["version"]

                assert device.get_system_info()["boot_count"] == boot_count + 1
            except AssertionError:
                raise

        try:
            wait_helper(success, "check update", timeout=120)
            logging.info("%s: update succeeded" % device.hostname)
        except:
            logging.info("%s: update failed" % device.hostname)


if __name__ == "__main__":
    main()
