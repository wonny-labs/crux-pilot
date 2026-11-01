"""Focused one-boot M5 network adapter: DAPLink VCOM, peer processes and host NIC link control.

Inert until constructed. It never programs, erases, resets or power-cycles the board. The only
host state it changes is the dedicated NIC en5 (administrative down/up) through the exact
non-interactive command `sudo -n /sbin/ifconfig en5 down|up`; without a credential cached by the
operator in the same terminal that command fails and the run stops. It never reads a password.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import select
import subprocess
import termios
import time
import m3_clock as shared_clock
import capture_m5_network as runner
from m5_capture_adapter import CaptureAdapter, ProcessBackend, parse_direct_route, parse_interface

BANNER = runner.BANNER_PREFIX
ASSIGN = ['sudo', '-n', '/sbin/ifconfig', 'en5', 'inet', '192.168.77.1', 'netmask', '255.255.255.0', 'up']
MAC = '00:0a:cd:41:89:cc'
CARRIER_TIMEOUT = 15
SETTLE_TIMEOUT = 5


class HostLink:
    """Dedicated-NIC control with independent witnesses; every step is bounded."""

    def __init__(self, run=subprocess.run, clock=shared_clock.monotonic, sleep=shared_clock.sleep):
        self.run = run
        self.clock = clock
        self.sleep = sleep

    def command(self, argv):
        try:
            result = self.run(argv, capture_output=True, text=True, timeout=5)
        except subprocess.TimeoutExpired as error:
            raise ValueError('Host command exceeded its deadline: ' + ' '.join(argv)) from error
        runner.oracle.require(result.returncode == 0, 'Host command failed (a sudo credential not cached by the operator stops here): ' + ' '.join(argv))
        return result.stdout

    def observe(self):
        return parse_interface(self.command(['ifconfig', 'en5']))

    def identity(self):
        """The dedicated adapter by MAC; macOS drops the static address while the link is inactive."""
        text = self.command(['ifconfig', 'en5'])
        runner.oracle.require(re.findall(r'(?m)^[ \t]*ether ([0-9a-f:]+)[ \t]*$', text) == [MAC], 'en5 is not the dedicated adapter')

    def switch(self, up):
        # macOS also drops the manual address while the link is inactive, so `up` reassigns it.
        self.command(ASSIGN if up else ['sudo', '-n', '/sbin/ifconfig', 'en5', 'down'])

    def configure(self):
        """Assign the static address and wait until the carrier is active with that address present."""
        deadline = self.clock() + CARRIER_TIMEOUT
        self.command(ASSIGN)
        while True:
            try:
                observed = self.observe()
            except ValueError:
                observed = None
                self.command(ASSIGN)
            if observed is not None and observed['active']:
                return observed
            runner.oracle.require(self.clock() < deadline, 'en5 did not become active with the static address')
            self.sleep(.1)

    def carrier(self):
        """Carrier state of the dedicated adapter; needs no address (the host may have dropped it)."""
        text = self.command(['ifconfig', 'en5'])
        runner.oracle.require(re.findall(r'(?m)^[ \t]*ether ([0-9a-f:]+)[ \t]*$', text) == [MAC], 'en5 is not the dedicated adapter')
        statuses = re.findall(r'(?m)^[ \t]*status:[ \t]*(\S+)[ \t]*$', text)
        runner.oracle.require(statuses in (['active'], ['inactive']), 'NIC carrier status missing/unknown/ambiguous')
        return statuses == ['active']

    def wait_carrier(self, active, limit):
        deadline = self.clock() + limit
        while True:
            if self.carrier() == active:
                return
            runner.oracle.require(self.clock() < deadline, 'NIC carrier did not become ' + ('active' if active else 'inactive'))
            self.sleep(.1)

    def down(self):
        """host_down: the administrative change returned; receipt: carrier observed inactive."""
        self.switch(False)
        down = dict(event='host_down', monotonic=self.clock(), interface='en5')
        self.wait_carrier(False, SETTLE_TIMEOUT)
        return [down, dict(event='cable_disconnect_receipt', monotonic=self.clock())]

    def up(self):
        """connect receipt: carrier observed active; host_up: fresh interface check; then route."""
        self.switch(True)
        self.wait_carrier(True, CARRIER_TIMEOUT)
        connect = dict(event='cable_connect_receipt', monotonic=self.clock())
        self.wait_carrier(True, SETTLE_TIMEOUT)
        observed = self.observe()  # strict: the static address must be back
        up = dict(event='host_up', monotonic=self.clock(), interface='en5')
        route = parse_direct_route(self.command(['route', '-n', 'get', '192.168.77.2']), observed['source'])
        route['monotonic'] = self.clock()
        return [connect, up, route]


class RawPort:
    """Minimal 115200 8N1 raw tty with bounded reads and writes (no pyserial dependency)."""
    timeout = .02
    write_timeout = .25

    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            attrs = termios.tcgetattr(self.fd)
            attrs[0] = attrs[1] = attrs[3] = 0
            attrs[2] = (attrs[2] & ~(termios.CSIZE | termios.PARENB | termios.CSTOPB | getattr(termios, 'CRTSCTS', 0))) | termios.CS8 | termios.CREAD | termios.CLOCAL
            attrs[4] = attrs[5] = termios.B115200
            attrs[6][termios.VMIN] = 0
            attrs[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        except BaseException:
            os.close(self.fd)
            raise

    def read(self, size):
        ready, _, _ = select.select([self.fd], [], [], self.timeout)
        if not ready:
            return b''
        try:
            return os.read(self.fd, size)
        except BlockingIOError:
            return b''

    def write(self, data):
        deadline = time.monotonic() + self.write_timeout
        sent = 0
        while sent < len(data):
            _, ready, _ = select.select([], [self.fd], [], max(0, deadline - time.monotonic()))
            runner.oracle.require(ready, 'UART write timeout')
            sent += os.write(self.fd, data[sent:])
        return sent

    def close(self):
        if self.fd is not None:
            fd, self.fd = self.fd, None
            os.close(fd)


class _NoNIC:
    def close(self):
        pass


class FocusedAdapter(CaptureAdapter):
    """CaptureAdapter for one boot: no approval plan, no reset, automatic host link control."""

    def __init__(self, nonce, directory, uart_factory, *, receiver_factory=None, process_backend=None, link=None, clock=shared_clock.monotonic, software_only=False):
        runner.oracle.require(type(nonce) is int and 0 < nonce <= 0xffffffff, 'Invalid boot nonce')
        self.nonce = nonce
        self.boot = 1
        self.clock = clock
        self.factory = uart_factory
        self.safety = None
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=False)
        self.backend = process_backend or ProcessBackend()
        self.process = None
        self.phase = None
        self.peer_path = None
        self.tail = {}
        self.pending = b''
        self.line_offset = 0
        self.reset_offset = None
        self.qualifying_started = False
        self.boot_seen = False
        self.closed = False
        self.nic = _NoNIC()
        self.nic_want = None
        self.nic_due = 0
        self.nic_running = False
        self.operator_path = self.directory / 'operator.jsonl'  # never created: not a channel here
        self.link = link or HostLink()
        self.queued = []
        self.link_down = False
        self.backlog = []
        self.prefix = b''
        self.cut = False
        self.port = None
        self.receiver = None
        try:
            self.port = uart_factory()
            runner.oracle.require(0 <= self.port.timeout <= .02 and 0 < self.port.write_timeout <= .25, 'UART read/write bounds differ from approved adapter')
            if receiver_factory is None:
                from capture_m3 import Receiver
                receiver_factory = Receiver
            self.receiver = receiver_factory(self.port, self.directory)
            self.receiver.begin()
            self.emit('adapter.jsonl', dict(event='capture_started', boot=1, nonce=nonce, software_only=software_only))
        except BaseException as primary:
            try:
                self.close()
            except BaseException as cleanup:
                primary.evidence_failure = repr(cleanup)
            raise

    def target_events(self):
        """Cut retained pre-banner bytes at the first Zephyr banner; parse strictly from there."""
        result = []
        for event in self.lines(self.directory / 'receive.jsonl'):
            chunk = bytes.fromhex(event['hex'])
            if not self.cut:
                self.prefix += chunk
                at = self.prefix.find(BANNER)
                head = self.prefix if at < 0 else self.prefix[:at]
                runner.oracle.require(len(head) <= runner.PREFIX_LIMIT and b'M5' not in head, 'Unexpected/oversized pre-banner output')
                if at < 0:
                    continue
                (self.directory / 'focused-prefix.json').write_text(json.dumps(runner.prefix_record(head)) + '\n')
                chunk = self.prefix[at:]
                self.cut = True
            self.pending += chunk
            while b'\n' in self.pending:
                line, self.pending = self.pending.split(b'\n', 1)
                if line.endswith(b'\r'):
                    line = line[:-1]
                line = line.decode('ascii')
                if line.startswith('M5_BOOT '):
                    match = re.fullmatch(r'M5_BOOT source=([0-9a-f]{12})', line)
                    runner.oracle.require(match and not self.boot_seen, 'Malformed/duplicate M5 source boot')
                    self.boot_seen = True
                    row, kind = dict(source=match[1]), 'boot'
                elif line.startswith('M5 '):
                    row, kind = runner.oracle.parse_record(line), 'target'
                else:
                    runner.oracle.require(not self.boot_seen and (line == '' or re.fullmatch(r'\*\*\* Booting Zephyr OS build [A-Za-z0-9.\-]+ \*\*\*', line)), 'Unexpected raw target output')
                    continue
                result.append(dict(kind=kind, monotonic=event['monotonic'], record=row))
        return result

    def poll(self, timeout):
        runner.oracle.require(not self.operator_path.exists(), 'operator.jsonl is not a channel in the focused adapter')
        events = CaptureAdapter.poll(self, timeout)
        events += [dict(kind='control', monotonic=row['monotonic'], record=row) for row in self.queued]
        self.queued = []
        events = self.backlog + events
        self.backlog = []
        return sorted(events, key=lambda event: event['monotonic'])

    def wait_for_boot(self, *, deadline, sleep):
        """Block (bounded) until the operator's power cycle makes the target print its banner."""
        while True:
            events = self.poll(0)
            if events:
                runner.oracle.require(events[0]['kind'] == 'boot', 'Unexpected traffic before the boot banner')
                self.backlog.extend(events)
                return events[0]
            if self.clock() >= deadline:
                return None
            sleep(.02)

    def queue(self, rows):
        for row in rows:
            self.emit('controls.jsonl', row)
            self.queued.append(row)

    def notify_operator(self, row):
        runner.oracle.require(row.get('event') == 'cable_disconnect_cue', 'Only the loss cue may change the host link')
        self.emit('controls.jsonl', row)
        self.emit('operator-cues.jsonl', dict(**row, operation='focused_host_nic_down'))
        self.link_down = True
        self.queue(self.link.down())

    def restore_link(self):
        self.emit('operator-cues.jsonl', dict(event='reconnect', operation='focused_host_nic_up'))
        self.queue(self.link.up())
        self.link_down = False
