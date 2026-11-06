import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { checkIpv4Settings } from '../src/index.ts';

// The vectors are shared with the firmware: tests/network_settings/validation.c reads the same file and holds
// the service's rules to every line. A client that disagrees with a line is wrong, since the server decides.
const VECTORS = new URL('../../../tests/network_settings/validation-vectors.tsv', import.meta.url);

/** What the server answers for the settings that the client checked, in the vectors' notation. */
function asServerAnswer(address: string, prefix: string, gateway: string): string {
  const { problems } = checkIpv4Settings({ address, prefix_length: prefix, gateway });
  if (problems.some((problem) => problem.code === 'not_ipv4')) {
    return 'bad_request';
  }
  if (problems.length === 0) {
    return 'ok';
  }
  return problems
    .map((problem) => `${problem.name}:${problem.code === 'gateway_not_usable' ? 'gateway_outside_subnet' : problem.code}`)
    .join(',');
}

const lines = readFileSync(VECTORS, 'utf8')
  .split('\n')
  .filter((line) => line !== '' && !line.startsWith('#'));

test('the vectors are read', () => {
  assert.ok(lines.length >= 40);
});

for (const line of lines) {
  const [address, prefix, gateway, expected] = line.split('\t');
  test(`vector ${address} /${prefix} gateway ${gateway}`, () => {
    assert.equal(asServerAnswer(address ?? '', prefix ?? '', gateway === '-' ? '' : (gateway ?? '')), expected);
  });
}
