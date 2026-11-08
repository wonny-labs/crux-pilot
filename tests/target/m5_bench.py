"""The one reviewed identity block of the dedicated M5 physical bench.

The M5 identity gates (runner plan, approval binding, host NIC and route checks,
service check, UART lease, series replay) read these values and keep no copy. Values
that did not change stay where they were: the probe serial in the J-Link identity
pattern and lock file name, the peer address and ports, and the M3 helpers' USB serial.
A value changes only with an independent review and the owner's approval of the new
identity, because this block is what makes the gates fail closed on any
other adapter, port or board.

The values below come from the read-only host inventory of 2026-11-07 (uhubctl
listing, system_profiler, ifconfig, networksetup, ls /dev/cu.*). The accepted M3
bench used interface en5, adapter MAC 00:0a:cd:41:89:cc and /dev/cu.usbmodem11302;
the shared M3 helpers keep those values as their defaults and are unchanged.

``service`` is the macOS network service of the dedicated adapter. Its name is a
label and may be renumbered by macOS; the check therefore also requires the
adapter MAC in the ``networksetup -getinfo`` output and stops on any difference.
"""

BENCH = dict(
    nic_interface='en13',
    nic_model='AX88179',
    nic_mac='00:24:9b:6e:f7:7f',
    service='AX88179 3',
    host='192.168.77.1',
    target='192.168.77.2',
    prefix=24,
    uart='/dev/cu.usbmodem11402',
    uart_usb_serial='02290000325182c300000000000000000000000097969905',
    probe_serial='602003106',
)


def only_process_owns(listing, pid):
    """True when an ``lsof -Fp`` listing names exactly this process and nothing else.

    ``lsof -Fp`` prints one ``p<pid>`` line per owning process and, after it, one
    ``f<descriptor>`` line per open file (the first real run stopped because the
    descriptor lines were not expected). Any other line, any other pid, or an empty
    listing is not proof that this process is the only owner.
    """
    import re
    lines = listing.splitlines()
    owners = [line for line in lines if line.startswith('p')]
    return (bool(owners) and all(line == 'p%d' % pid for line in owners) and
            all(re.fullmatch(r'p\d+|f\S+', line) for line in lines))
