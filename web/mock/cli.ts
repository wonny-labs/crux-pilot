// Run the mock by hand: node mock/cli.ts [--port 8080] [--static dir] [--inline-styles] [--trusted-types]
// A person can drive failures with POST /__mock/control. See web/README.md.
import { parseArgs } from 'node:util';
import { startMock } from './server.ts';
import type { MockOptions } from './server.ts';

const { values } = parseArgs({
  options: {
    port: { type: 'string', default: '8080' },
    static: { type: 'string' },
    'inline-styles': { type: 'boolean', default: false },
    'trusted-types': { type: 'boolean', default: false },
  },
});

const options: MockOptions = {
  port: Number(values.port),
  httpControl: true,
  stylePolicy: values['inline-styles'] ? 'inline' : 'strict',
  trustedTypes: values['trusted-types'],
};
if (values.static !== undefined) {
  options.staticDir = values.static;
}
const mock = await startMock(options);
process.stdout.write(`mock server on ${mock.url}. Accounts: operator, viewer, writer, guest, newuser, disabled.\n`);
process.stdout.write('Password for each: example-password-not-real\n');
