"""Stopping a tool this user may not signal. Touches no device. Run as root in a throwaway container.

The tools stay root and this process drops to another user, as a tool run through sudo would look.

usage: python3 stop-probe-unsignallable.py <crux>/tools
"""
import os
import platform
import signal
import subprocess
import sys
import time

sys.path.insert(0, sys.argv[1])
from device_control import run  # noqa: E402


def signal_group(label, process):
    for number, name in ((signal.SIGKILL, "SIGKILL"), (0, "0")):
        try:
            os.killpg(process.pid, number)
            print(f"{label}, killpg {name}: no error")
        except OSError as error:
            print(f"{label}, killpg {name}: {type(error).__name__} errno {error.errno}")


MIXED = ("import os, time\nif os.fork() == 0:\n    os.setresuid(65534, 65534, 65534)\n    time.sleep(30)\n"
         "    os._exit(0)\ntime.sleep(30)\n")


def give_up(number, frame):
    raise TimeoutError


print(platform.platform(), platform.python_version())
alive = subprocess.Popen(["sleep", "30"], start_new_session=True)
stop_alive = subprocess.Popen(["sleep", "30"], start_new_session=True)
exited = subprocess.Popen(["true"], start_new_session=True)
stop_exited = subprocess.Popen(["true"], start_new_session=True)
left_child = subprocess.Popen(["sh", "-c", "sleep 30 & exit 0"], start_new_session=True)
stop_left_child = subprocess.Popen(["sh", "-c", "sleep 30 & exit 0"], start_new_session=True)
# A root leader with a member the later user may signal.
mixed = subprocess.Popen([sys.executable, "-c", MIXED], start_new_session=True)
time.sleep(3.0)
os.setresuid(65534, 65534, 65534)
print("uid now", os.getuid())

signal_group("root tool alive", alive)
print("root tool alive, poll:", alive.poll())
signal_group("root tool exited, not reaped", exited)
print("root tool exited, not reaped, poll:", exited.poll())
signal_group("root tool exited, root child alive", left_child)
print("root tool exited, root child alive, poll:", left_child.poll())
signal_group("root tool reaped, root child alive", left_child)
signal_group("root tool reaped, no child", exited)

for label, process in (("root tool alive", stop_alive), ("root tool exited, not reaped", stop_exited),
                       ("root tool exited, root child alive", stop_left_child)):
    print(f"run._stop, {label}: {run._stop(process)}, returncode {process.returncode}")

signal.signal(signal.SIGALRM, give_up)
signal.alarm(5)
try:
    print(f"run._stop, root leader with a member this user may signal: {run._stop(mixed)}, returncode {mixed.returncode}")
except TimeoutError:
    print(f"run._stop, root leader with a member this user may signal: still waiting after 5 s, returncode {mixed.returncode}")
