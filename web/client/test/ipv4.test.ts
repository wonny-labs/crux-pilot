import assert from 'node:assert/strict';
import { test } from 'node:test';
import { checkIpv4Settings, parseIpv4 } from '../src/index.ts';

test('parseIpv4 accepts dotted quads', () => {
  assert.equal(parseIpv4('192.0.2.10'), 0xc000020a);
  assert.equal(parseIpv4('0.0.0.0'), 0);
  assert.equal(parseIpv4('255.255.255.255'), 0xffffffff);
});

for (const text of ['', '1.2.3', '1.2.3.4.5', '256.0.0.1', '1.2.3.x', '01.2.3.4', '1.2.3.4 ', ' 1.2.3.4', '1..2.3', '-1.2.3.4']) {
  test(`parseIpv4 refuses ${JSON.stringify(text)}`, () => {
    assert.equal(parseIpv4(text), null);
  });
}

function problems(address: string, prefix: string, gateway = '') {
  return checkIpv4Settings({ address, prefix_length: prefix, gateway }).problems;
}

test('valid settings give the request body, with an empty gateway left out', () => {
  assert.deepEqual(checkIpv4Settings({ address: '192.0.2.20', prefix_length: '24', gateway: '' }), {
    problems: [],
    settings: { address: '192.0.2.20', prefix_length: 24 },
  });
  assert.deepEqual(checkIpv4Settings({ address: '192.0.2.20', prefix_length: '24', gateway: '192.0.2.1' }), {
    problems: [],
    settings: { address: '192.0.2.20', prefix_length: 24, gateway: '192.0.2.1' },
  });
});

test('surrounding spaces are trimmed before checking', () => {
  assert.deepEqual(checkIpv4Settings({ address: ' 192.0.2.20 ', prefix_length: ' 24 ', gateway: ' ' }), {
    problems: [],
    settings: { address: '192.0.2.20', prefix_length: 24 },
  });
});

test('an empty or malformed address is a format problem', () => {
  assert.deepEqual(problems('', '24'), [{ name: 'address', code: 'not_ipv4' }]);
  assert.deepEqual(problems('192.0.2', '24'), [{ name: 'address', code: 'not_ipv4' }]);
});

for (const address of ['0.0.0.1', '127.0.0.1', '224.0.0.1', '240.0.0.1', '255.255.255.255']) {
  test(`${address} is not a unicast address`, () => {
    assert.deepEqual(problems(address, '24'), [{ name: 'address', code: 'not_unicast' }]);
  });
}

test('the network and broadcast addresses of the subnet are not usable', () => {
  assert.deepEqual(problems('192.0.2.0', '24'), [{ name: 'address', code: 'not_unicast' }]);
  assert.deepEqual(problems('192.0.2.255', '24'), [{ name: 'address', code: 'not_unicast' }]);
  assert.deepEqual(problems('192.0.2.1', '30'), []);
});

for (const prefix of ['', '0', '31', '32', '24.5', 'x', '-1']) {
  test(`prefix length ${JSON.stringify(prefix)} is not usable`, () => {
    assert.deepEqual(problems('192.0.2.20', prefix), [{ name: 'prefix_length', code: 'prefix_not_usable' }]);
  });
}

test('a gateway must be well formed and inside the subnet', () => {
  assert.deepEqual(problems('192.0.2.20', '24', 'x'), [{ name: 'gateway', code: 'not_ipv4' }]);
  assert.deepEqual(problems('192.0.2.20', '24', '198.51.100.1'), [
    { name: 'gateway', code: 'gateway_outside_subnet' },
  ]);
  assert.deepEqual(problems('192.0.2.20', '24', '0.0.0.0'), [
    { name: 'gateway', code: 'gateway_outside_subnet' },
  ]);
  assert.deepEqual(problems('10.1.2.3', '8', '10.200.0.1'), []);
});

test('the subnet arithmetic holds for a short prefix with the top bit set', () => {
  assert.deepEqual(problems('200.1.2.3', '1', '129.0.0.1'), []);
  assert.deepEqual(problems('200.1.2.3', '1', '100.0.0.1'), [{ name: 'gateway', code: 'gateway_outside_subnet' }]);
});

test('every problem is reported at once', () => {
  assert.deepEqual(problems('', '40', 'x'), [
    { name: 'address', code: 'not_ipv4' },
    { name: 'prefix_length', code: 'prefix_not_usable' },
    { name: 'gateway', code: 'not_ipv4' },
  ]);
});
