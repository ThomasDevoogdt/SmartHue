import os
import sys
import json
import logging
import pytest

repo_name = "SmartHue"
sys.path.append("{}{}/".format(os.path.abspath(__file__).split(repo_name)[0], repo_name))
from smartHuePy.helpers.helpers import *

logging.basicConfig(filename='test.log', filemode='w', level=logging.DEBUG)
devices_config_path = "%s/devices.json" % get_dir_path_of_this_repo()
logging.info("config path: {}".format(devices_config_path))
devices_config = load_json_path(devices_config_path)
devices = devices_config["devices"]["test"]


def check_for_reboot(device, boot_count=0, trigger=False):
    if trigger:
        boot_count = device.get_system_info()["boot_count"]
        device.reboot_device()

    def success():
        system_info = device.get_system_info()
        assert system_info, "could not load system info"
        assert system_info["boot_count"] == boot_count + 1
        assert system_info["reset_reason"] == "Software/System restart"
        assert device.get_relay(1)["value"], "relay 1 should be turned on"
        assert device.get_relay(2)["value"], "relay 2 should be turned on"

    wait_helper(success, "check if rebooted")


def ota_upgrade(device):
    pio_run_upload(device)

    def success():
        system_ota = device.get_ota()
        assert system_ota, "could not fetch ota info"
        assert system_ota["ota"] == "success", "ota failed"
        system_ota = device.get_ota()
        assert not system_ota, "ota info should be deleted"

    wait_helper(
        success, "check if rebooted after firmware update & if succeeded", timeout=120)


def check_version_api(device):
    project_version = get_repo_version()
    device_version = device.get_version()
    assert project_version.git_commit_id == device_version["software"]["git_commit_id"]
    assert project_version.version_string in device_version["software"]["version"]


def check_relay_api(device, relay):
    device.set_relay(relay, False)
    assert not device.get_relay(relay)["value"], "relay should be turned off"
    device.set_relay(relay, True)
    assert device.get_relay(relay)["value"], "relay should be turned on"
    device.set_relay(relay)
    assert not device.get_relay(relay)["value"], "relay should be toggled"
    device.set_relay(relay)
    assert device.get_relay(relay)["value"], "relay should be toggled"


class TestSmartHueApi:
    @classmethod
    def setup_class(cls):
        cls.setup = SetupHelper()

        for dut in devices:
            cls.setup.add(dut)

    @pytest.fixture(params=devices)
    def device(self, request):
        dut = self.setup.get(request.param)
        version = dut.get_version()
        if not version:
            pytest.skip("skipping test: device %s is not up" % dut.hostname)

        return self.setup.get(request.param)

    def test_ota(self, device):
        """
        test the firmware update capabilities of the device
        this test is executed twice to check if the new firmware is also upgradable
        """
        boot_count = device.get_system_info()["boot_count"]

        ota_upgrade(device)
        check_for_reboot(device, boot_count=boot_count)
        check_version_api(device)

        ota_upgrade(device)
        check_for_reboot(device, boot_count=boot_count + 1)
        check_version_api(device)

    def test_reboot(self, device):
        """
        test if the device could be rebooted and if the reboot reason was indeed caused by the api call
        this test also verifies if the relays are turned on after a reboot
        """
        check_for_reboot(device, trigger=True)

    def test_version_api(self, device):
        """
        test if the software version of the device could be fetched
        """
        check_version_api(device)

    def test_relay_api(self, device):
        """
        test the relay set/get api
        """
        check_relay_api(device, 1)
        check_relay_api(device, 2)
