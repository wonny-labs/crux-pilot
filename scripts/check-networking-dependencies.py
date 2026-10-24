#!/usr/bin/env python3
"""Narrow source/build guard for Issue #28 consumers; not a general C parser."""
import argparse
from pathlib import Path
import re
import sys

STANDARD = {'stdbool.h', 'stddef.h', 'stdint.h', 'limits.h', 'string.h', 'stdio.h',
            'sys/socket.h', 'netinet/in.h', 'arpa/inet.h', 'poll.h', 'fcntl.h',
            'unistd.h', 'errno.h', 'time.h', 'crux/netif.h', 'crux/status.h'}
OWNED = {'client.h', 'runtime_port.h', 'consumer.h'}

def uncomment(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', ' ', text, flags=re.S)

def source_errors(text, *, owned=OWNED):
    text = uncomment(text)
    errors = []
    for directive in re.findall(r'^\s*#\s*include\s+([^\n]+)', text, re.M):
        match = re.fullmatch(r'[<"]([^">]+)[">]\s*', directive)
        if not match or match[1] not in STANDARD | owned:
            errors.append('unapproved or computed include: ' + directive)
    forbidden = r'\b(?:zsock_\w*|net_(?:if|sockaddr|in_addr|socklen_t|context|pkt)\w*|ENET_Type|GPIO_Type|I2C_Type|LPUART_Type|SPI_Type|_GNU_SOURCE|k_\w*|NET_\w*|ZSOCK_\w*|CONFIG_\w*|fsl_\w*|board_\w*|crux_netif_observe|crux_netif_ipv4_eligible|MSG_\w*)\b'
    errors += ['native/private/out-of-profile token: ' + m for m in re.findall(forbidden, text)]
    if re.search(r'^\s*#\s*(?:if|ifdef|ifndef).*\b(?:__ZEPHYR__|__linux__|_WIN32|BOARD_\w*)\b', text, re.M):
        errors.append('platform conditional in consumer')
    if re.search(r'\b(?:bind|listen|accept|sendto|recvfrom|recvmsg|sendmsg|getaddrinfo|setsockopt|shutdown)\s*\(', text):
        errors.append('operation outside qualified client profile')
    return errors

def cmake_errors(text):
    text = re.sub(r'#[^\n]*', '', text)
    requirements = ' '.join(re.findall(r'target_(?:link_libraries|include_directories)\s*\(([^)]*)\)', text, re.S))
    return ['native/private consumer requirement'] if re.search(
        r'\b(?:zephyr_interface|crux_(?:netif|sockets)(?:_\w+)?|crux_time_zephyr)\b|components/[^)\s]*/(?:src|host|zephyr|ports)', requirements) else []

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    sources = ['samples/networking/client.c', 'samples/networking/client.h',
               'tests/networking/runtime_port.c', 'tests/networking/runtime_port.h',
               'tests/networking_consumer/consumer.h',
               'tests/networking_consumer/cpp_probe.cpp',
               'components/netif/include/crux/netif.h']
    failures = []
    for name in sources:
        path = args.root / name
        failures += [name + ': ' + error for error in source_errors(path.read_text())]
    # Private correlated test consumer receives only its own frame header.
    for name in ('tests/networking_physical/transaction.c', 'tests/networking_physical/frame.c',
                 'tests/networking_physical/frame.h'):
        failures += [name + ': ' + error for error in
                     source_errors((args.root / name).read_text(), owned={'frame.h'})]
    name = 'tests/networking_consumer/CMakeLists.txt'
    failures += [name + ': ' + error for error in cmake_errors((args.root / name).read_text())]
    for failure in failures: print(failure, file=sys.stderr)
    if not failures: print('Networking consumers use the qualified standard/public boundary.')
    return bool(failures)

if __name__ == '__main__': sys.exit(main())
