"""Replacement mount guard for the DAPLink virtual drive (hardware safety gate).

macOS mounts the DAPLink USB mass-storage volume of the board's debug probe. Anything
that writes to that volume (Finder, Spotlight, a backup tool) can make the probe try to
program the target. The physical capture therefore requires a guard that vetoes every
mount of exactly that USB device while the capture runs.

This program is the guard. It uses DiskArbitration mount approval, which macOS allows
only to root. It writes the session record and the log that
``capture_m3_network._guard_check`` and the M5 adapter expect. It never writes to the
DAPLink volume, never programs, never touches the probe, and does nothing to any other
disk.

    sudo /usr/bin/python3 tests/target/m5_mount_guard.py start --session S --log L
    python3 tests/target/m5_mount_guard.py probe        # no root, read-only

``start`` sequence (every step fails closed; nothing is left running on failure):
  1. refuse unless root, arguments valid and neither output file exists;
  2. confirm from the I/O registry that exactly one disk and exactly one callout device
     (the reviewed UART) sit below the DAPLink USB device;
  3. detach into the background, then register the mount approval callback;
  4. unmount the volume if it is mounted (``/sbin/umount`` with a timeout; never
     ``diskutil``, which stalls on some hosts);
  5. ask DiskArbitration to mount the disk once and require that the guard vetoed it
     (this is the "actual current-session veto evidence" the check demands);
  6. only then write the session record, and keep vetoing until the deadline or SIGTERM;
  7. retire: log ``STOPPED`` (which makes the repository check refuse the guard) and exit.

A deny rule matches the exact DAPLink USB serial, vendor and product. A disk that reports
the USB protocol but whose serial cannot be read is also denied, since it could be the
probe. Every other disk, including objects without I/O media, is approved untouched.
"""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
import os
import plistlib
import re
import signal
import subprocess
import sys
import m3_clock
from m5_bench import BENCH

SERIAL = BENCH['uart_usb_serial']
VENDOR, PRODUCT = 0x0d28, 0x0204
UART = BENCH['uart']
DENY_STATUS = 0xF8DA0008          # kDAReturnNotPermitted
DEFAULT_LIFETIME, MIN_LIFETIME, MAX_LIFETIME = 7200, 300, 14400
EVIDENCE_WAIT = 20
UMOUNT_TIMEOUT = 20
IOREG = ['/usr/sbin/ioreg', '-p', 'IOService', '-l', '-w0', '-a']
SYSCTL_BOOT = ['/usr/sbin/sysctl', '-n', 'kern.bootsessionuuid']


class GuardError(Exception):
    """Any condition that forbids starting or continuing; the caller exits non-zero."""


def decide(info, serial=SERIAL):
    """Return (deny, kind) for one mount request described by ``info``.

    kind is ``exact`` for the DAPLink device, ``unresolved`` for a disk that reports USB but
    whose identity could not be read, and ``''`` when the disk is not ours and is approved.
    """
    if info.get('serial') == serial and info.get('vid') == VENDOR and info.get('pid') == PRODUCT:
        return True, 'exact'
    if info.get('error') and info.get('protocol') == 'USB':
        return True, 'unresolved'
    return False, ''


def whole_disk(bsd):
    match = re.fullmatch(r'(disk[0-9]+)(?:s[0-9]+)*', bsd or '')
    return match[1] if match else bsd


def usb_disks(tree, serial=SERIAL):
    """Disks and callout devices below the exact DAPLink USB device in an ioreg plist."""
    disks, uarts = [], []

    def walk(value, inside=False):
        if isinstance(value, dict):
            if 'USB Serial Number' in value:
                inside = (value.get('USB Serial Number') == serial and
                          value.get('idVendor') == VENDOR and value.get('idProduct') == PRODUCT)
            if inside and value.get('BSD Name'):
                disks.append(value['BSD Name'])
            if inside and value.get('IOCalloutDevice'):
                uarts.append(value['IOCalloutDevice'])
            for child in value.values():
                walk(child, inside)
        elif isinstance(value, list):
            for child in value:
                walk(child, inside)
    walk(tree)
    return disks, uarts


def mounted_points(mount_text, disk):
    """(device, mount point) for every mounted partition of ``disk``."""
    found = []
    for line in mount_text.splitlines():
        match = re.match(r'^(/dev/' + re.escape(disk) + r'(?:s[0-9]+)*) on (.+) \(', line)
        if match:
            found.append((match[1], match[2]))
    return found


def sha256_file(path):
    with open(path, 'rb') as stream:
        return hashlib.sha256(stream.read()).hexdigest()


class SystemHost:
    """Real host commands; every one has a timeout. Replaced by a fake in the tests."""

    def euid(self):
        return os.geteuid()

    def run(self, command, timeout=10):
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        try:
            out, err = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except OSError:
                pass
            raise GuardError('host command timed out: ' + ' '.join(command))
        return process.returncode, out.decode(errors='replace'), err.decode(errors='replace')

    def ioreg(self):
        code, out, err = self.run(IOREG, timeout=30)
        if code != 0:
            raise GuardError('ioreg failed')
        return plistlib.loads(out.encode())

    def mounts(self):
        code, out, _ = self.run(['/sbin/mount'])
        if code != 0:
            raise GuardError('mount listing failed')
        return out

    def umount(self, point):
        return self.run(['/sbin/umount', point], timeout=UMOUNT_TIMEOUT)

    def identity(self, pid):
        code, out, _ = self.run(['ps', '-p', str(pid), '-o', 'pid=,comm=,lstart='])
        if code != 0 or not out.strip():
            raise GuardError('cannot read own process identity')
        return out

    def boot_session(self):
        code, out, _ = self.run(SYSCTL_BOOT)
        if code != 0 or not out.strip():
            raise GuardError('cannot read boot session id')
        return out


class Guard:
    """The guard core. ``da`` offers register_mount_approval, request_mount and run_for."""

    def __init__(self, da, host, clock, session_path, log_path, lifetime=DEFAULT_LIFETIME,
                 executable=None, interpreter=None, evidence_wait=EVIDENCE_WAIT, serial=SERIAL):
        self.da, self.host, self.clock = da, host, clock
        self.session_path, self.log_path = session_path, log_path
        self.lifetime, self.evidence_wait, self.serial = lifetime, evidence_wait, serial
        self.executable = executable or os.path.abspath(__file__)
        self.interpreter = interpreter or sys.executable
        self.log = None
        self.denied = 0
        self.stop_reason = None
        self.disk = None
        self.deadline = None

    # ---- logging -----------------------------------------------------------------
    def say(self, text):
        if self.log is None:
            return
        os.write(self.log, ('%.3f %s\n' % (self.clock(), text)).encode())

    def retire(self, reason):
        """The only place that writes the retirement marker."""
        if self.log is not None:
            self.say('STOPPED reason=' + reason)
            os.close(self.log)
            self.log = None

    # ---- validation, before any file or DiskArbitration object exists -------------
    def validate(self):
        if self.host.euid() != 0:
            raise GuardError('must run as root: DiskArbitration mount approval is root only')
        if type(self.lifetime) is not int or not (MIN_LIFETIME <= self.lifetime <= MAX_LIFETIME):
            raise GuardError('lifetime must be %d..%d seconds' % (MIN_LIFETIME, MAX_LIFETIME))
        for name, path in (('session', self.session_path), ('log', self.log_path)):
            if not os.path.isabs(path) or path != os.path.normpath(path):
                raise GuardError(name + ' path must be absolute and normalised')
            if os.path.lexists(path):
                raise GuardError(name + ' file already exists; refusing to overwrite: ' + path)
            if not os.path.isdir(os.path.dirname(path)):
                raise GuardError(name + ' directory does not exist')
        disks, uarts = usb_disks(self.host.ioreg(), self.serial)
        if len(disks) != 1 or uarts != [UART]:
            raise GuardError('DAPLink ancestry is not exactly one disk and the reviewed UART: disks=%r uarts=%r' % (disks, uarts))
        self.disk = disks[0]

    # ---- approval --------------------------------------------------------------------
    def approve(self, info):
        """Called by DiskArbitration for every mount request. Return a message to veto, else None."""
        try:
            deny, kind = decide(info, self.serial)
        except Exception as error:      # fail closed on any bug
            deny, kind = True, 'internal_error'
            info = dict(info or {}, internal=repr(error))
        if not deny:
            return None
        bsd = whole_disk(info.get('bsd'))
        if kind == 'exact':
            self.denied += 1
            self.say('DENIED exact_DAPLink_USB_serial=%s disk=%s mount_target=%s' % (self.serial, bsd, info.get('bsd')))
        else:
            self.say('DENIED %s_usb_identity disk=%s mount_target=%s detail=%s' % (kind, bsd, info.get('bsd'), info.get('error') or info.get('internal')))
        return 'Mount vetoed by the Crux physical-test guard'

    # ---- start ---------------------------------------------------------------------
    def start(self):
        self.validate()
        self.log = os.open(self.log_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_APPEND, 0o644)
        self.deadline = self.clock() + self.lifetime
        try:
            self.say('GUARD_STARTING serial=%s disk=%s pid=%d lifetime=%d' % (self.serial, self.disk, os.getpid(), self.lifetime))
            self.da.register_mount_approval(self.approve)
            self.say('APPROVAL_REGISTERED')
            self.unmount_if_mounted()
            self.selftest()
            self.write_session()
            self.say('READY disk=%s deadline=%.3f' % (self.disk, self.deadline))
        except BaseException as error:
            self.say('START_FAILED ' + str(error))
            self.retire('start_failed')
            raise

    def unmount_if_mounted(self):
        mounted = mounted_points(self.host.mounts(), self.disk)
        for device, point in mounted:
            self.say('UNMOUNT device=%s point=%s' % (device, point))
            code, _, err = self.host.umount(point)
            if code != 0:
                raise GuardError('unmount of %s failed (%s); close anything using the volume and retry' % (point, err.strip()))
        if mounted_points(self.host.mounts(), self.disk):
            raise GuardError('DAPLink volume is still mounted after unmount')

    def selftest(self):
        before = self.denied
        self.say('SELFTEST mount request for ' + self.disk)
        self.da.request_mount(self.disk)
        waited = 0.0
        while self.denied == before and waited < self.evidence_wait:
            self.da.run_for(0.5)
            waited += 0.5
        if self.denied == before:
            # The veto did not happen. If the volume mounted, put it back.
            if mounted_points(self.host.mounts(), self.disk):
                for _, point in mounted_points(self.host.mounts(), self.disk):
                    self.host.umount(point)
            raise GuardError('no veto evidence: the self-test mount was not denied by the guard')
        if mounted_points(self.host.mounts(), self.disk):
            raise GuardError('self-test mount was denied but the volume is mounted')

    def write_session(self):
        pid = os.getpid()
        record = dict(
            schema=1, clock=m3_clock.CLOCK_NAME, serial=self.serial, disk='/dev/' + self.disk,
            boot_session_uuid=self.host.boot_session(),
            executable=self.executable, sha256=sha256_file(self.executable),
            interpreter=self.interpreter, interpreter_sha256=sha256_file(os.path.realpath(self.interpreter)),
            deadline=self.deadline, lifetime=self.lifetime, pid=pid,
            process_identity=self.host.identity(pid), log=self.log_path,
            evidence='selftest_mount_vetoed', vendor_id=VENDOR, product_id=PRODUCT, uart=UART)
        temporary = self.session_path + '.tmp%d' % pid
        handle = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
        try:
            os.write(handle, json.dumps(record, indent=2).encode() + b'\n')
        finally:
            os.close(handle)
        try:
            os.link(temporary, self.session_path)        # fails if the file appeared meanwhile: never overwrite
        finally:
            os.unlink(temporary)

    # ---- run and retire --------------------------------------------------------------
    def run(self):
        """Keep vetoing until the deadline or a stop request; always retire."""
        try:
            while self.stop_reason is None and self.clock() < self.deadline:
                self.da.run_for(1.0)
            self.retire(self.stop_reason or 'deadline')
        except BaseException:
            self.retire('error')
            raise

    def request_stop(self, reason):
        self.stop_reason = reason


# ---------------------------------------------------------------------------------------
# Real DiskArbitration binding (ctypes; no installs). Exercised read-only by `probe`;
# the mount approval path needs root and is covered by the fake in the tests.
# ---------------------------------------------------------------------------------------
class DiskArbitration:
    UTF8 = 0x08000100

    def __init__(self):
        def load(name):
            path = ctypes.util.find_library(name)
            if not path:
                raise GuardError('framework not found: ' + name)
            return ctypes.cdll.LoadLibrary(path)
        cf, da, io = load('CoreFoundation'), load('DiskArbitration'), load('IOKit')
        vp, u32, c_long = ctypes.c_void_p, ctypes.c_uint32, ctypes.c_long
        for function, restype, argtypes in (
                (cf.CFRunLoopGetCurrent, vp, []),
                (cf.CFRunLoopRunInMode, ctypes.c_int32, [vp, ctypes.c_double, ctypes.c_ubyte]),
                (cf.CFStringCreateWithCString, vp, [vp, ctypes.c_char_p, u32]),
                (cf.CFDictionaryGetValue, vp, [vp, vp]),
                (cf.CFGetTypeID, ctypes.c_ulong, [vp]),
                (cf.CFStringGetTypeID, ctypes.c_ulong, []),
                (cf.CFNumberGetTypeID, ctypes.c_ulong, []),
                (cf.CFStringGetCString, ctypes.c_ubyte, [vp, ctypes.c_char_p, c_long, u32]),
                (cf.CFNumberGetValue, ctypes.c_ubyte, [vp, ctypes.c_int, vp]),
                (cf.CFRelease, None, [vp]),
                (da.DASessionCreate, vp, [vp]),
                (da.DASessionScheduleWithRunLoop, None, [vp, vp, vp]),
                (da.DARegisterDiskMountApprovalCallback, None, [vp, vp, vp, vp]),
                (da.DARegisterDiskAppearedCallback, None, [vp, vp, vp, vp]),
                (da.DADiskGetBSDName, ctypes.c_char_p, [vp]),
                (da.DADiskCopyDescription, vp, [vp]),
                (da.DADiskCopyIOMedia, u32, [vp]),
                (da.DADiskCopyWholeDisk, vp, [vp]),
                (da.DADiskCreateFromBSDName, vp, [vp, vp, ctypes.c_char_p]),
                (da.DADiskMount, None, [vp, vp, u32, vp, vp]),
                (da.DADissenterCreate, vp, [vp, u32, vp]),
                (da.DADissenterGetStatus, ctypes.c_int32, [vp]),
                (io.IORegistryEntrySearchCFProperty, vp, [u32, ctypes.c_char_p, vp, vp, u32]),
                (io.IOObjectRelease, ctypes.c_int32, [u32])):
            function.restype, function.argtypes = restype, argtypes
        self.cf, self.da, self.io = cf, da, io
        self.mode = vp.in_dll(cf, 'kCFRunLoopDefaultMode')
        self.session = da.DASessionCreate(None)
        if not self.session:
            raise GuardError('DiskArbitration session could not be created')
        da.DASessionScheduleWithRunLoop(self.session, cf.CFRunLoopGetCurrent(), self.mode)
        self.keep = []
        self.key_protocol = self.string('DADeviceProtocol')
        self.key_serial = self.string('USB Serial Number')
        self.key_vendor = self.string('idVendor')
        self.key_product = self.string('idProduct')
        self.approve_type = ctypes.CFUNCTYPE(vp, vp, vp)
        self.appeared_type = ctypes.CFUNCTYPE(None, vp, vp)
        self.mount_type = ctypes.CFUNCTYPE(None, vp, vp, vp)

    def string(self, text):
        return self.cf.CFStringCreateWithCString(None, text.encode(), self.UTF8)

    def text_of(self, value):
        if not value or self.cf.CFGetTypeID(value) != self.cf.CFStringGetTypeID():
            return None
        buffer = ctypes.create_string_buffer(256)
        return buffer.value.decode() if self.cf.CFStringGetCString(value, buffer, 256, self.UTF8) else None

    def number_of(self, value):
        if not value or self.cf.CFGetTypeID(value) != self.cf.CFNumberGetTypeID():
            return None
        out = ctypes.c_int32()
        return out.value if self.cf.CFNumberGetValue(value, 3, ctypes.byref(out)) else None

    def describe(self, disk):
        """Identity of the USB device a disk lives on; ``error`` is set when it cannot be read."""
        info = dict(bsd=None, protocol=None, serial=None, vid=None, pid=None, error=None)
        name = self.da.DADiskGetBSDName(disk)
        info['bsd'] = name.decode() if name else None
        description = self.da.DADiskCopyDescription(disk)
        if description:
            info['protocol'] = self.text_of(self.cf.CFDictionaryGetValue(description, self.key_protocol))
            self.cf.CFRelease(description)
        media = self.da.DADiskCopyIOMedia(disk)
        if not media:
            info['error'] = 'no IOMedia'
            return info
        try:
            def search(key):
                value = self.io.IORegistryEntrySearchCFProperty(media, b'IOService', key, None, 3)   # recursive + parents
                return value
            serial, vendor, product = search(self.key_serial), search(self.key_vendor), search(self.key_product)
            info['serial'], info['vid'], info['pid'] = self.text_of(serial), self.number_of(vendor), self.number_of(product)
            for value in (serial, vendor, product):
                if value:
                    self.cf.CFRelease(value)
            if info['protocol'] == 'USB' and info['serial'] is None:
                info['error'] = 'USB serial not readable'
        finally:
            self.io.IOObjectRelease(media)
        return info

    def register_mount_approval(self, handler):
        def approval(disk, _context):
            try:
                message = handler(self.describe(disk))
            except Exception:       # fail closed
                message = 'Mount vetoed by the Crux physical-test guard (internal error)'
            if message is None:
                return None
            return self.da.DADissenterCreate(None, DENY_STATUS, self.string(message))
        callback = self.approve_type(approval)
        self.keep.append(callback)
        self.da.DARegisterDiskMountApprovalCallback(self.session, None, ctypes.cast(callback, ctypes.c_void_p), None)

    def request_mount(self, bsd):
        disk = self.da.DADiskCreateFromBSDName(None, self.session, bsd.encode())
        if not disk:
            raise GuardError('DiskArbitration has no disk ' + bsd)

        def done(_disk, dissenter, _context):
            pass            # the veto evidence is the approval callback's own log line
        callback = self.mount_type(done)
        self.keep.append(callback)
        self.da.DADiskMount(disk, None, 0, ctypes.cast(callback, ctypes.c_void_p), None)

    def run_for(self, seconds):
        self.cf.CFRunLoopRunInMode(self.mode, seconds, 0)

    def appeared(self, seconds=3.0):
        """Describe every disk DiskArbitration reports (read-only; no root needed)."""
        found = []

        def seen(disk, _context):
            found.append(self.describe(disk))
        callback = self.appeared_type(seen)
        self.keep.append(callback)
        self.da.DARegisterDiskAppearedCallback(self.session, None, ctypes.cast(callback, ctypes.c_void_p), None)
        self.run_for(seconds)
        return found


# ---------------------------------------------------------------------------------------
def probe(host=None, da=None, out=print):
    """Read-only: show what the guard would match, without root and without any veto."""
    host = host or SystemHost()
    disks, uarts = usb_disks(host.ioreg())
    out(json.dumps(dict(ioreg_disks=disks, ioreg_uarts=uarts, reviewed_uart=UART, ancestry_ok=len(disks) == 1 and uarts == [UART]), indent=2))
    da = da or DiskArbitration()
    rows = [dict(row, decision=('deny:' + decide(row)[1]) if decide(row)[0] else 'approve') for row in da.appeared()]
    out(json.dumps(rows, indent=2))
    mounted = mounted_points(host.mounts(), disks[0]) if len(disks) == 1 else []
    out(json.dumps(dict(daplink_mounted=mounted), indent=2))
    return rows


def start_detached(args, host=None):
    """Validate, detach, run the guard in the child, and report its readiness to the caller."""
    host = host or SystemHost()
    Guard(None, host, m3_clock.monotonic, args.session, args.log, args.lifetime).validate()   # fail before forking
    reader, writer = os.pipe()
    pid = os.fork()
    if pid:
        os.close(writer)
        data = b''
        os.set_blocking(reader, True)
        while True:
            chunk = os.read(reader, 4096)
            if not chunk:
                break
            data += chunk
        text = data.decode().strip()
        print(text or 'guard child exited without a report')
        return 0 if text.startswith('READY') else 1
    os.close(reader)
    os.setsid()
    devnull = os.open(os.devnull, os.O_RDWR)
    for descriptor in (0, 1, 2):
        os.dup2(devnull, descriptor)
    guard = None
    try:
        guard = Guard(DiskArbitration(), host, m3_clock.monotonic, args.session, args.log, args.lifetime)
        signal.signal(signal.SIGTERM, lambda *_: guard.request_stop('sigterm'))
        signal.signal(signal.SIGINT, lambda *_: guard.request_stop('sigint'))
        guard.start()
        os.write(writer, ('READY pid=%d disk=%s session=%s log=%s deadline_in=%ds\n' % (os.getpid(), guard.disk, args.session, args.log, args.lifetime)).encode())
        os.close(writer)
        guard.run()
        os._exit(0)
    except BaseException as error:
        try:
            os.write(writer, ('FAILED ' + str(error) + '\n').encode())
        finally:
            os._exit(1)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='mode', required=True)
    start = sub.add_parser('start', help='run as root; detaches and prints READY or FAILED')
    start.add_argument('--session', required=True)
    start.add_argument('--log', required=True)
    start.add_argument('--lifetime', type=int, default=DEFAULT_LIFETIME, help='seconds, %d..%d' % (MIN_LIFETIME, MAX_LIFETIME))
    sub.add_parser('probe', help='read-only, no root: show the DAPLink disk, its mount state and the guard decision')
    args = parser.parse_args(argv)
    try:
        if args.mode == 'probe':
            probe()
            return 0
        return start_detached(args)
    except GuardError as error:
        print('REFUSED ' + str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
