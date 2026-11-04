"""What stopping a tool's process group reports on this host. Touches no device.

usage: python3 stop-probe.py <crux>/tools
"""
import os
import platform
import signal
import subprocess
import sys
import time

sys.path.insert(0, sys.argv[1])
from device_control import run  # noqa: E402

CHILD = "import subprocess, sys; subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(5)'])"


def leader(code):
    return subprocess.Popen([sys.executable, "-c", code], start_new_session=True)


def signal_group(label, process):
    try:
        os.killpg(process.pid, signal.SIGKILL)
        print(f"killpg, {label}: no error")
    except OSError as error:
        print(f"killpg, {label}: {type(error).__name__} errno {error.errno}")


print(platform.platform(), platform.python_version())

process = leader("pass")
time.sleep(1.0)
signal_group("leader exited, not reaped", process)
process.wait()

process = leader(CHILD)
time.sleep(1.0)
signal_group("leader exited, not reaped, a child still running", process)
process.wait()

process = leader("pass")
process.wait()
signal_group("leader reaped", process)

process = leader("pass")
time.sleep(1.0)
try:
    run._stop(process)
    print(f"run._stop, tool exited, not reaped: returned, returncode {process.returncode}")
except OSError as error:
    print(f"run._stop, tool exited, not reaped: {type(error).__name__} errno {error.errno}, "
          f"returncode {process.returncode}, DeviceControlError {isinstance(error, run.DeviceControlError)}")
process.wait()
