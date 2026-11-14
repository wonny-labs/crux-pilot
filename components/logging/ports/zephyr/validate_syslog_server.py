#!/usr/bin/env python3
"""Validate the numeric UDP server address accepted by Zephyr's NET logger."""

import ipaddress
import sys


class ConfigurationError(Exception):
    pass


def validate(value: str, ipv4_enabled: bool, ipv6_enabled: bool) -> None:
    if not value:
        raise ConfigurationError("requires a numeric Syslog server address")
    if value.startswith("tcp://"):
        raise ConfigurationError("does not support TCP Syslog")

    port = 514
    bracketed = False
    if value.startswith("["):
        bracketed = True
        closing = value.find("]")
        if closing < 0:
            raise ConfigurationError("requires a numeric Syslog server address")
        host = value[1:closing]
        suffix = value[closing + 1 :]
        if suffix:
            if not suffix.startswith(":"):
                raise ConfigurationError("requires a numeric Syslog server address")
            port_text = suffix[1:]
            if not port_text.isascii() or not port_text.isdigit():
                raise ConfigurationError("requires a numeric Syslog server address with a decimal UDP port")
            port = int(port_text)
    elif value.count(":") == 1:
        host, port_text = value.rsplit(":", 1)
        if not port_text.isascii() or not port_text.isdigit():
            raise ConfigurationError("requires a numeric Syslog server address with a decimal UDP port")
        port = int(port_text)
    else:
        host = value

    address = ipaddress.ip_address(host)
    if bracketed and address.version != 6:
        raise ConfigurationError("bracketed Syslog addresses must use IPv6")
    if address.version == 6 and "%" in host:
        raise ConfigurationError("scoped IPv6 Syslog addresses are unsupported")
    if (address.version == 4 and not ipv4_enabled) or (address.version == 6 and not ipv6_enabled):
        raise ConfigurationError("Syslog server address family does not match enabled CONFIG_NET_IPV4/CONFIG_NET_IPV6")
    if not 1 <= port <= 65535:
        raise ConfigurationError("requires a numeric Syslog server address with UDP port 1..65535")


if __name__ == "__main__":
    try:
        if len(sys.argv) != 4 or sys.argv[2] not in {"0", "1"} or sys.argv[3] not in {"0", "1"}:
            raise ConfigurationError("requires a numeric Syslog server address and enabled IP-family flags")
        validate(sys.argv[1], sys.argv[2] == "1", sys.argv[3] == "1")
    except ConfigurationError as error:
        print(str(error))
        sys.exit(1)
    except ValueError:
        print("requires a numeric Syslog server address using Zephyr's IPv4 or IPv6 syntax")
        sys.exit(1)
