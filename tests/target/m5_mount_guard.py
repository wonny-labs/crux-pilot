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
    sudo /usr/bin/python3 tests/target/m5_mount_guard.py start --arm --session S --log L
    python3 tests/target/m5_mount_guard.py probe        # no root, read-only

Two ways to start. The default expects the DAPLink to be present: it unmounts its volume, proves its
veto with a self-test mount, then writes the record. That needs the volume to unmount, and an FSKit FAT
volume of the DAPLink can wedge so that ``umount`` never returns (state U in ps; it cannot be killed).
``--arm`` is for that case: the DAPLink USB is unplugged first, the guard registers its veto and waits,
the owner replugs, macOS tries to mount the new volume, the guard vetoes that real request (the veto
evidence), checks the ancestry and writes the record. It never touches a mount in this mode.

``start`` sequence (every step fails closed; nothing is left running on failure):
  1. refuse unless root, arguments valid and neither output file exists;
  2. confirm from the I/O registry that exactly one disk and exactly one callout device
     (the reviewed UART) sit below the DAPLink USB device;
  3. detach into the background, then register the mount approval callback;
  4. unmount the volume if it is mounted (``/sbin/umount`` with a timeout; never
     ``diskutil``, which stalls on some hosts);
  5. ask DiskArbitration to mount the disk once, read-only and hidden (``rdonly``,
     ``nobrowse``), and require that the guard vetoed it: the mount completion must report
     the veto, not a mount (this is the "actual current-session veto evidence" the check
     demands). If the volume did mount, it is unmounted again and the start fails saying
     whether that worked;
  6. only then write the session record, and keep vetoing until the deadline or SIGTERM;
  7. retire: log ``STOPPED`` (which makes the repository check refuse the guard) and exit.
     A failure at any point retires the log, removes a record this run wrote and exits.

A deny rule matches the exact DAPLink USB serial. A disk that reports
the USB protocol but whose serial cannot be read is also denied, since it could be the
probe. Every other disk, including objects without I/O media, is approved untouched.

Limits: DiskArbitration vetoes mounts requested through it. A mount made directly by root
(``mount_msdos``, ``mount(2)``) or a raw write to the device is not covered. The record
binds the hash of this script only; run it from a checkout only the owner can write.
Run the capture with ``LC_ALL=C`` so the process identity text matches the guard's.
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
MOUNT_ARGUMENTS = ('rdonly', 'nobrowse')   # the self-test mount, if it ever happens, is read-only and hidden
REPORT_TIMEOUT = 90                       # seconds the parent waits for READY or FAILED
ARM_WAIT, MIN_ARM_WAIT, MAX_ARM_WAIT = 600, 60, 1800   # seconds to wait for the replugged DAPLink's mount request
ANCESTRY_WAIT = 20                        # seconds for the replugged device to show its disk and serial port
TERMINAL = ('READY', 'FAILED')
DEFAULT_LIFETIME, MIN_LIFETIME, MAX_LIFETIME = 7200, 300, 14400
EVIDENCE_WAIT = 20
UMOUNT_TIMEOUT = 20
IOREG = ['/usr/sbin/ioreg', '-p', 'IOService', '-l', '-w0', '-a']
SYSCTL_BOOT = ['/usr/sbin/sysctl', '-n', 'kern.bootsessionuuid']


class GuardError(Exception):
    """Any condition that forbids starting or continuing; the caller exits non-zero."""


def decide(info, serial=SERIAL):
    """Return (deny, kind) for one mount request described by ``info``.

    kind is ``exact`` for the DAPLink device (any disk whose USB serial is the DAPLink's), ``unresolved`` for a disk that reports USB but
    whose identity could not be read, and ``''`` when the disk is not ours and is approved.
    """
    if info.get('serial') == serial:
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


def usb_present(tree, serial=SERIAL):
    """True if any node of the I/O registry is the DAPLink USB device."""
    if isinstance(tree, dict):
        if tree.get('USB Serial Number') == serial:
            return True
        return any(usb_present(child, serial) for child in tree.values())
    if isinstance(tree, list):
        return any(usb_present(child, serial) for child in tree)
    return False


# The mount table is read with getfsstat64(MNT_NOWAIT), which answers from the kernel's cached entries and never
# calls into a file system. `/sbin/mount` asks every file system and blocks on a wedged one. It runs in its own
# short-lived interpreter so that even a kernel stall cannot hold the guard.
MOUNT_READER = '''
import ctypes, ctypes.util
libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
class Statfs(ctypes.Structure):
    _fields_ = [("f_bsize", ctypes.c_uint32), ("f_iosize", ctypes.c_int32), ("f_blocks", ctypes.c_uint64),
                ("f_bfree", ctypes.c_uint64), ("f_bavail", ctypes.c_uint64), ("f_files", ctypes.c_uint64),
                ("f_ffree", ctypes.c_uint64), ("f_fsid", ctypes.c_int32 * 2), ("f_owner", ctypes.c_uint32),
                ("f_type", ctypes.c_uint32), ("f_flags", ctypes.c_uint32), ("f_fssubtype", ctypes.c_uint32),
                ("f_fstypename", ctypes.c_char * 16), ("f_mntonname", ctypes.c_char * 1024),
                ("f_mntfromname", ctypes.c_char * 1024), ("f_flags_ext", ctypes.c_uint32),
                ("f_reserved", ctypes.c_uint32 * 7)]
assert ctypes.sizeof(Statfs) == 2168
libc.getfsstat64.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
count = libc.getfsstat64(None, 0, 2)
assert count > 0
buffer = (Statfs * (count + 16))()   # room for mounts added between the two calls
filled = libc.getfsstat64(buffer, ctypes.sizeof(buffer), 2)
assert 0 < filled < len(buffer)
for entry in buffer[:filled]:
    print("%s on %s (%s)" % (entry.f_mntfromname.decode(), entry.f_mntonname.decode(), entry.f_fstypename.decode()))
'''


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

    def run(self, command, timeout=10, env=None):
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True, env=env)
        try:
            out, err = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except OSError:
                pass
            raise GuardError('host command timed out: ' + ' '.join(command[:3]))
        return process.returncode, out.decode(errors='replace'), err.decode(errors='replace')

    def ioreg(self):
        code, out, err = self.run(IOREG, timeout=30)
        if code != 0:
            raise GuardError('ioreg failed')
        return plistlib.loads(out.encode())

    def mounts(self):
        code, out, err = self.run([sys.executable, '-I', '-S', '-c', MOUNT_READER])
        if code != 0 or ' on / (' not in out:
            raise GuardError('mount table could not be read: ' + err.strip()[-200:])
        return out

    def umount(self, point):
        return self.run(['/sbin/umount', point], timeout=UMOUNT_TIMEOUT)

    def identity(self, pid):
        # Fixed environment: the repository check runs the same command, so the capture is run with LC_ALL=C too.
        code, out, _ = self.run(['ps', '-p', str(pid), '-o', 'pid=,comm=,lstart='], env=dict(PATH='/bin:/usr/bin:/usr/sbin:/sbin', LC_ALL='C'))
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
                 executable=None, interpreter=None, evidence_wait=EVIDENCE_WAIT, serial=SERIAL, arm=False, arm_wait=ARM_WAIT):
        self.da, self.host, self.clock = da, host, clock
        self.arm, self.arm_wait, self.vetoed, self.progress = arm, arm_wait, [], (lambda text: None)
        self.session_path, self.log_path = session_path, log_path
        self.lifetime, self.evidence_wait, self.serial = lifetime, evidence_wait, serial
        self.executable = executable or os.path.abspath(__file__)
        self.interpreter = interpreter or sys.executable
        self.log = None
        self.denied = 0
        self.stop_reason = None
        self.disk = None
        self.deadline = None
        self.session_written = False

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
        if self.arm and not (type(self.arm_wait) is int and MIN_ARM_WAIT <= self.arm_wait <= MAX_ARM_WAIT):
            raise GuardError('arm wait must be %d..%d seconds' % (MIN_ARM_WAIT, MAX_ARM_WAIT))
        tree = self.host.ioreg()
        if self.arm:
            if usb_present(tree, self.serial):
                raise GuardError('the DAPLink is present: unplug its USB first for arm mode, or use the normal start')
            return
        disks, uarts = usb_disks(tree, self.serial)
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
            self.vetoed.append(whole_disk(info.get('bsd')))
            self.say('DENIED exact_DAPLink_USB_serial=%s disk=%s mount_target=%s' % (self.serial, bsd, info.get('bsd')))
        else:
            self.say('DENIED %s_usb_identity disk=%s mount_target=%s detail=%s' % (kind, bsd, info.get('bsd'), info.get('error') or info.get('internal')))
        return 'Mount vetoed by the Crux physical-test guard'

    # ---- start ---------------------------------------------------------------------
    def start(self):
        self.validate()
        self.log = os.open(self.log_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_APPEND, 0o644)
        os.fchmod(self.log, 0o644)
        self.deadline = self.clock() + self.lifetime
        try:
            self.say('GUARD_STARTING mode=%s serial=%s disk=%s pid=%d lifetime=%d' % ('arm' if self.arm else 'present', self.serial, self.disk, os.getpid(), self.lifetime))
            self.da.register_mount_approval(self.approve)
            self.say('APPROVAL_REGISTERED')
            if self.arm:
                self.arm_and_wait()
                self.deadline = self.clock() + self.lifetime      # the lifetime counts from READY, not from the start of the wait
                self.write_session()
                self.say('READY disk=%s deadline=%.3f' % (self.disk, self.deadline))
                return
            self.da.run_for(0)                  # keep answering approvals between slow host commands
            self.unmount_if_mounted()
            self.da.run_for(0)
            self.selftest()
            self.write_session()
            self.say('READY disk=%s deadline=%.3f' % (self.disk, self.deadline))
        except BaseException as error:
            self.say('START_FAILED ' + str(error))
            self.discard_session()
            self.retire('start_failed')
            raise

    WEDGED = ('The DAPLink volume does not answer (the FSKit FAT volume is wedged). Unplug the DAPLink USB, wait for the '
              'mount to disappear, then start again with --arm. Cause: ')

    def unmount_if_mounted(self):
        mounted = mounted_points(self.host.mounts(), self.disk)
        for device, point in mounted:
            self.say('UNMOUNT device=%s point=%s' % (device, point))
            try:
                code, _, err = self.host.umount(point)
            except GuardError as error:
                if 'timed out' in str(error):
                    raise GuardError(self.WEDGED + str(error))
                raise
            if code != 0:
                raise GuardError('unmount of %s failed (%s); close anything using the volume and retry' % (point, err.strip()))
        if mounted_points(self.host.mounts(), self.disk):
            raise GuardError('DAPLink volume is still mounted after unmount')

    def arm_and_wait(self):
        """Armed before the replug: the veto of macOS's own mount request is the evidence."""
        self.progress('ARMED veto registered for the DAPLink serial; plug the DAPLink USB in now (waiting up to %d s)' % self.arm_wait)
        self.say('ARMED waiting up to %d s for the DAPLink to be plugged in' % self.arm_wait)
        waited = 0.0
        while not self.vetoed and waited < self.arm_wait:
            self.da.run_for(0.5)
            waited += 0.5
        if not self.vetoed:
            raise GuardError('no mount request for the DAPLink within %d s: it was not plugged in, or macOS did not try to mount it' % self.arm_wait)
        self.disk = self.vetoed[0]
        for _ in range(4):                      # let a second request for another partition arrive before judging
            self.da.run_for(0.5)
        deadline = self.clock() + ANCESTRY_WAIT
        while True:
            disks, uarts = usb_disks(self.host.ioreg(), self.serial)
            if disks == [self.disk] and uarts == [UART]:
                break
            if self.clock() >= deadline:
                raise GuardError('after the replug the DAPLink ancestry is not exactly the vetoed disk and the reviewed UART: disks=%r uarts=%r' % (disks, uarts))
            self.da.run_for(1.0)
        if mounted_points(self.host.mounts(), self.disk):
            raise GuardError('the DAPLink volume is mounted although its mount request was vetoed')
        self.evidence = 'armed_replug_veto'

    evidence = 'selftest_mount_vetoed'

    def selftest(self):
        """Ask for one read-only mount and require the guard's veto in the completion."""
        before = self.denied
        outcome = []
        self.say('SELFTEST read-only mount request for ' + self.disk)
        self.da.request_mount(self.disk, outcome.append)
        waited = 0.0
        while not outcome and waited < self.evidence_wait:
            self.da.run_for(0.5)
            waited += 0.5
        for _ in range(4):                      # a late completion must not slip past the check below
            self.da.run_for(0.5)
        vetoed = bool(outcome) and outcome[0] == DENY_STATUS and self.denied > before
        mounted = mounted_points(self.host.mounts(), self.disk)
        if vetoed and not mounted:
            return
        problem = ('self-test mount was vetoed but the volume is mounted' if vetoed else
                   'no veto evidence: the self-test mount completed with %s' % (outcome[0] if outcome else 'no answer in time'))
        failed = []
        for _, point in mounted:
            code, _, err = self.host.umount(point)
            if code != 0:
                failed.append('%s (%s)' % (point, err.strip()))
        if mounted_points(self.host.mounts(), self.disk):
            problem += '; THE DAPLINK VOLUME IS STILL MOUNTED read-only (could not unmount %s): run /sbin/umount before anything else' % ', '.join(failed or [self.disk])
        elif mounted:
            problem += '; the volume had mounted and was unmounted again'
        raise GuardError(problem)

    def write_session(self):
        pid = os.getpid()
        record = dict(
            schema=1, clock=m3_clock.CLOCK_NAME, serial=self.serial, disk='/dev/' + self.disk,
            boot_session_uuid=self.host.boot_session(),
            executable=self.executable, sha256=sha256_file(self.executable),
            interpreter=self.interpreter, interpreter_sha256=sha256_file(os.path.realpath(self.interpreter)),
            deadline=self.deadline, lifetime=self.lifetime, pid=pid,
            process_identity=self.host.identity(pid), log=self.log_path,
            evidence=self.evidence, mode='arm' if self.arm else 'present', vendor_id=VENDOR, product_id=PRODUCT, uart=UART)
        temporary = self.session_path + '.tmp%d' % pid
        handle = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
        try:
            try:
                os.fchmod(handle, 0o644)
                os.write(handle, json.dumps(record, indent=2).encode() + b'\n')
            finally:
                os.close(handle)
            os.link(temporary, self.session_path)        # fails if the file appeared meanwhile: never overwrite
            self.session_written = True
        finally:
            os.unlink(temporary)

    def discard_session(self):
        """Remove the record only when this run wrote it (a failed or abandoned start)."""
        if self.session_written:
            try:
                os.unlink(self.session_path)
            except OSError:
                pass
            self.session_written = False

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
                (da.DADiskMountWithArguments, None, [vp, vp, u32, vp, vp, vp]),
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
                try:
                    message = handler(self.describe(disk))
                except Exception:       # fail closed
                    message = 'Mount vetoed by the Crux physical-test guard (internal error)'
                if message is None:
                    return None
                return self.da.DADissenterCreate(None, DENY_STATUS, self.string(message))
            except Exception:           # even building the answer failed: still try to veto
                return self.da.DADissenterCreate(None, DENY_STATUS, None)
        callback = self.approve_type(approval)
        self.keep.append(callback)
        self.da.DARegisterDiskMountApprovalCallback(self.session, None, ctypes.cast(callback, ctypes.c_void_p), None)

    def request_mount(self, bsd, done):
        """Request one read-only, hidden mount; ``done(status)`` gets the dissenter status or 0 on success."""
        disk = self.da.DADiskCreateFromBSDName(None, self.session, bsd.encode())
        if not disk:
            raise GuardError('DiskArbitration has no disk ' + bsd)

        def completed(_disk, dissenter, _context):
            done(self.da.DADissenterGetStatus(dissenter) & 0xFFFFFFFF if dissenter else 0)
        callback = self.mount_type(completed)
        self.keep.append(callback)
        strings = [self.string(argument) for argument in MOUNT_ARGUMENTS]
        arguments = (ctypes.c_void_p * (len(strings) + 1))(*strings, None)
        self.keep.append((arguments, strings))
        self.da.DADiskMountWithArguments(disk, None, 0, ctypes.cast(callback, ctypes.c_void_p), None, ctypes.cast(arguments, ctypes.c_void_p))

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


def read_report(descriptor, timeout, clock=m3_clock.monotonic, on_line=None):
    """Read the child's report lines until a READY or FAILED line, EOF or the timeout; returns (text, finished).

    Progress lines before the final one (ARMED, ...) go to ``on_line`` as they arrive, so the owner sees them at once.
    """
    import select
    data, deadline, shown = b'', clock() + timeout, 0
    while True:
        lines = data.decode(errors='replace').split('\n')
        complete = lines[:-1]
        for line in complete[shown:]:
            if line.startswith(TERMINAL):
                return line.strip(), True
            if on_line and line.strip():
                on_line(line.strip())
        shown = len(complete)
        remaining = deadline - clock()
        if remaining <= 0:
            return data.decode(errors='replace').strip(), False
        ready, _, _ = select.select([descriptor], [], [], min(remaining, 1.0))
        if ready:
            chunk = os.read(descriptor, 4096)
            data += chunk
            if not chunk:                      # the child closed the pipe: whatever it sent last is the answer
                tail = data.decode(errors='replace').strip().splitlines()
                return (tail[-1] if tail else ''), True


def run_child(guard, report, install_signals=None):
    """The detached guard's life: start, report READY, run, retire. Returns the exit code.

    ``report(text, final)`` carries progress lines (final False) and the one final line. A failure at any point
    (including the report failing because the caller went away) retires the log with STOPPED and removes a
    session record this run wrote.
    """
    try:
        if install_signals:
            install_signals(guard)
        guard.progress = lambda text: report(text, False)
        guard.start()
        report('READY pid=%d disk=%s session=%s log=%s deadline_in=%ds' % (os.getpid(), guard.disk, guard.session_path, guard.log_path, guard.lifetime), True)
        guard.run()
        return 0
    except BaseException as error:
        guard.discard_session()
        guard.retire('child_failed')
        try:
            report('FAILED ' + str(error), True)
        except BaseException:
            pass
        return 1


def make_reporter(writer, sent):
    """Progress lines keep the pipe open; the one final line closes it so the caller is never kept waiting."""
    def report(text, final=True):
        if sent:
            return
        if final:
            sent.append(text)
        try:
            os.write(writer, (text + '\n').encode())
        finally:
            if final:
                os.close(writer)
    return report


def start_detached(args, host=None):
    """Validate, detach, run the guard in the child, and report its progress and readiness to the caller."""
    host = host or SystemHost()
    arm = getattr(args, 'arm', False)
    arm_wait = getattr(args, 'arm_wait', ARM_WAIT)
    Guard(None, host, m3_clock.monotonic, args.session, args.log, args.lifetime, arm=arm, arm_wait=arm_wait).validate()   # fail before forking
    reader, writer = os.pipe()
    pid = os.fork()
    if pid:
        os.close(writer)
        wait = REPORT_TIMEOUT + (arm_wait if arm else 0)
        text, finished = read_report(reader, wait, on_line=print)
        if not finished:
            print('NO REPORT within %d s from guard pid %d: read %s; stop it with: sudo kill %d' % (wait, pid, args.log, pid))
            return 1
        print(text or 'guard child exited without a report')
        return 0 if text.startswith('READY') else 1
    os.close(reader)
    code = 1
    sent = []
    try:
        os.setsid()
        os.umask(0o022)
        devnull = os.open(os.devnull, os.O_RDWR)
        for descriptor in (0, 1, 2):
            os.dup2(devnull, descriptor)

        report = make_reporter(writer, sent)

        def install(guard):
            signal.signal(signal.SIGTERM, lambda *_: guard.request_stop('sigterm'))
            signal.signal(signal.SIGINT, lambda *_: guard.request_stop('sigint'))
        code = run_child(Guard(DiskArbitration(), host, m3_clock.monotonic, args.session, args.log, args.lifetime, arm=arm, arm_wait=arm_wait), report, install)
    except BaseException as error:
        try:
            if not sent:
                report('FAILED ' + str(error))
        except BaseException:
            pass
    finally:
        os._exit(code)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='mode', required=True)
    start = sub.add_parser('start', help='run as root; detaches and prints READY or FAILED')
    start.add_argument('--session', required=True)
    start.add_argument('--log', required=True)
    start.add_argument('--lifetime', type=int, default=DEFAULT_LIFETIME, help='seconds, %d..%d' % (MIN_LIFETIME, MAX_LIFETIME))
    start.add_argument('--arm', action='store_true', help='DAPLink unplugged: veto first, then replug it (use when the volume is wedged)')
    start.add_argument('--arm-wait', type=int, default=ARM_WAIT, help='seconds to wait for the replug, %d..%d' % (MIN_ARM_WAIT, MAX_ARM_WAIT))
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
