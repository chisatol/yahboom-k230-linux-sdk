#!/usr/bin/python3
import importlib.util
import os
import pwd
import signal
import shutil
import subprocess
import threading
import time

spec = importlib.util.spec_from_file_location("camera_mode", "/usr/local/sbin/yahboom-camera-mode.py")
camera_mode = importlib.util.module_from_spec(spec)
spec.loader.exec_module(camera_mode)


def desktop_user():
    user = pwd.getpwnam("elaina")
    os.initgroups(user.pw_name, user.pw_gid)
    os.setgid(user.pw_gid)
    os.setuid(user.pw_uid)


def set_microphone_gain(client):
    # This board uses MICR; the current codec mixer mistakenly adjusts MICL twice.
    for _ in range(350):
        if client.poll() is not None:
            return
        try:
            with open("/proc/asound/card0/pcm0c/sub0/status", encoding="ascii") as capture, \
                 open("/proc/asound/card0/pcm0p/sub0/hw_params", encoding="ascii") as playback:
                if "state: RUNNING" in capture.read() and "rate:" in playback.read():
                    time.sleep(0.5)
                    break
        except OSError:
            pass
        time.sleep(0.1)
    else:
        print("Microphone gain: capture device did not start", flush=True)
        return
    try:
        command = ["busybox", "devmem", "0x9140e09c", "32"]
        value = int(subprocess.check_output(command, text=True).strip(), 16)
        desired = (value & ~0x60) | 0x40
        subprocess.run(command + [hex(desired)], check=True)
        if int(subprocess.check_output(command, text=True).strip(), 16) != desired:
            raise ValueError("codec register readback mismatch")
        print("Microphone right-channel gain set", flush=True)
    except (OSError, subprocess.SubprocessError, ValueError) as error:
        print(f"Microphone gain setup failed: {error}", flush=True)


def main():
    signal.signal(signal.SIGTERM, camera_mode.handle_stop)
    signal.signal(signal.SIGINT, camera_mode.handle_stop)
    signal.signal(signal.SIGUSR1, camera_mode.handle_return)
    ui = client = wake = None
    try:
        subprocess.run(["systemctl", "stop", "lightdm.service"], check=True)
        ui = subprocess.Popen(["/opt/xiaozhi/xiaozhi-ui"], cwd="/opt/xiaozhi")
        enrollment = "/home/elaina/.config/xiaozhi-wake.json"
        if os.path.isfile(enrollment):
            wake_command = ["/opt/xiaozhi/xiaozhi-open-wake", "/opt/xiaozhi/open-kws/kws.kmodel",
                            "/opt/xiaozhi/open-kws/assets", enrollment]
        else:
            wake_command = ["/opt/xiaozhi/xiaozhi-wake", "/opt/xiaozhi/kws.kmodel"]
        if not os.path.exists("/run/xiaozhi-enroll-mode"):
            wake = subprocess.Popen(wake_command, cwd="/opt/xiaozhi")
        client = subprocess.Popen(["/opt/xiaozhi/xiaozhi_client", "button"],
                                  cwd="/opt/xiaozhi", preexec_fn=desktop_user,
                                  stdin=subprocess.DEVNULL)
        threading.Thread(target=set_microphone_gain, args=(client,), daemon=True).start()
        camera_mode.wait_for_key1(ui)
    finally:
        if os.path.exists("/run/xiaozhi-enroll-mode"):
            os.unlink("/run/xiaozhi-enroll-mode")
        shutil.rmtree("/run/xiaozhi-enroll", ignore_errors=True)
        if wake is not None:
            camera_mode.stop_camera(wake)
        if client is not None:
            camera_mode.stop_camera(client)
        if ui is not None:
            camera_mode.stop_camera(ui)
        camera_mode.restore_desktop()


if __name__ == "__main__":
    main()
