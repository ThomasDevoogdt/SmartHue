import git
import importlib
import json
import logging
import os
import shlex
import subprocess
import sys
import time

from smartHuePy.helpers.smarthueapi import SmartHueApi


def load_json_path(path):
    return json.loads(open(path).read())


def get_dir_path_of_this_file():
    return os.path.dirname(os.path.realpath(__file__))


def get_dir_path_of_this_repo():
    git_repo = git.Repo(__file__, search_parent_directories=True)
    git_root = git_repo.git.rev_parse("--show-toplevel")
    return os.path.realpath(git_root)


def pio_run_upload(device):
    try:
        flags = [
            "--silent",
            "--target upload",
            "--environment esp-12F-ota",
            "--upload-port %s.local" % device.hostname.lower()
        ]
        run_subprocess("platformio run %s" % ' '.join(flags), cwd='../../')
    except subprocess.CalledProcessError:
        pass  # platformio sometimes returns error code 1 while the upload is actually succeeded


def wait_helper(cb, message="wait for success", timeout=60, sleep=5, *args, **kwargs):
    time_now = time.time()
    while time.time() - time_now < timeout:
        try:
            logging.info("call callback for: %s" % message)
            cb_out = cb(*args, **kwargs)
            logging.info("wait succeeded: %s" % message)
            return cb_out
        except Exception as e:
            logging.error(e)
            time.sleep(sleep)
    logging.error("wait exception raised: %s" % message)
    raise Exception("wait exception raised: %s" % message)


def run_subprocess(system_command, **kwargs):
    popen = subprocess.Popen(
        shlex.split(system_command),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
        **kwargs)
    lines = []
    for stdout_line in iter(popen.stdout.readline, ""):
        line = stdout_line.strip()
        logging.debug(line)
        lines.append(line)
    popen.stdout.close()
    return_code = popen.wait()
    if return_code:
        raise subprocess.CalledProcessError(return_code, system_command)
    return lines


def get_repo_version():
    sys.path.insert(0, "%s/smartHuePy/version/gitversion/src" %
                    get_dir_path_of_this_repo())
    versioninforeader = importlib.import_module(
        "gitversionbuilder.versioninforeader")
    return versioninforeader.from_git(".")


class SetupHelper:
    def __init__(self):
        self.devices = {}

        config = load_json_path("%s/config.json" % get_dir_path_of_this_repo())

        self.www_user = config["security"]["www_user"]
        self.www_pass = config["security"]["www_pass"]

    def add(self, device):
        self.devices[device] = SmartHueApi(
            device, self.www_user, self.www_pass)
        return self

    def get(self, device):
        return self.devices[device]
