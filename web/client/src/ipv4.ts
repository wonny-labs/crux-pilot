// Client-side checks of IPv4 settings, for usability only. The server remains the authority,
// and it reports its own result with the same field codes.
import type { Ipv4Settings } from './api-types.generated.ts';

export type Ipv4ProblemCode =
  | 'not_ipv4'
  | 'not_unicast'
  | 'prefix_not_usable'
  | 'gateway_outside_subnet'
  // Client only: 0.0.0.0, the address itself, or the subnet's network or broadcast address.
  | 'gateway_not_usable';

export interface Ipv4Problem {
  name: 'address' | 'prefix_length' | 'gateway';
  code: Ipv4ProblemCode;
}

export interface Ipv4Input {
  address: string;
  prefix_length: string;
  gateway: string;
}

/** The address as an unsigned 32-bit number, or null when the text is not a plain dotted quad. */
export function parseIpv4(text: string): number | null {
  if (!/^(0|[1-9][0-9]{0,2})(\.(0|[1-9][0-9]{0,2})){3}$/.test(text)) {
    return null;
  }
  let value = 0;
  for (const part of text.split('.')) {
    const octet = Number(part);
    if (octet > 255) {
      return null;
    }
    value = value * 256 + octet;
  }
  return value;
}

function isUnicastClass(address: number): boolean {
  const first = Math.floor(address / 2 ** 24);
  return first !== 0 && first !== 127 && first < 224;
}

/** The number that a 32-bit address falls in once its host bits are dropped. */
function subnetOf(address: number, prefix: number): number {
  return Math.floor(address / 2 ** (32 - prefix));
}

function hostBits(address: number, prefix: number): number {
  return address % 2 ** (32 - prefix);
}

export function checkIpv4Settings(input: Ipv4Input): { problems: Ipv4Problem[]; settings?: Ipv4Settings } {
  const problems: Ipv4Problem[] = [];
  const addressText = input.address.trim();
  const prefixText = input.prefix_length.trim();
  const gatewayText = input.gateway.trim();

  const prefix = /^[0-9]{1,2}$/.test(prefixText) ? Number(prefixText) : null;
  const usablePrefix = prefix !== null && prefix >= 1 && prefix <= 30 ? prefix : null;

  const address = parseIpv4(addressText);
  if (address === null) {
    problems.push({ name: 'address', code: 'not_ipv4' });
  } else {
    // With no usable prefix there is no subnet to compare with, so only the address class counts.
    const hosts = usablePrefix === null ? 1 : hostBits(address, usablePrefix);
    const broadcast = usablePrefix === null ? 0 : 2 ** (32 - usablePrefix) - 1;
    if (!isUnicastClass(address) || hosts === 0 || hosts === broadcast) {
      problems.push({ name: 'address', code: 'not_unicast' });
    }
  }

  if (usablePrefix === null) {
    problems.push({ name: 'prefix_length', code: 'prefix_not_usable' });
  }

  if (gatewayText !== '') {
    const gateway = parseIpv4(gatewayText);
    if (gateway === null) {
      problems.push({ name: 'gateway', code: 'not_ipv4' });
    } else if (gateway === 0 || gateway === address) {
      problems.push({ name: 'gateway', code: 'gateway_not_usable' });
    } else if (address !== null && usablePrefix !== null) {
      const hosts = hostBits(gateway, usablePrefix);
      if (subnetOf(gateway, usablePrefix) !== subnetOf(address, usablePrefix)) {
        problems.push({ name: 'gateway', code: 'gateway_outside_subnet' });
      } else if (hosts === 0 || hosts === 2 ** (32 - usablePrefix) - 1) {
        problems.push({ name: 'gateway', code: 'gateway_not_usable' });
      }
    }
  }

  if (problems.length > 0 || usablePrefix === null) {
    return { problems };
  }
  const settings: Ipv4Settings = { address: addressText, prefix_length: usablePrefix };
  if (gatewayText !== '') {
    settings.gateway = gatewayText;
  }
  return { problems, settings };
}
