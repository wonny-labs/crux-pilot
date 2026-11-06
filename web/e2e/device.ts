// A native_sim image of the services composition with an example packaged in it (Issue #220), started as a
// device for the browser cases. Each case gets a fresh device, as each mock case gets a fresh mock: the image
// boots once on a blank disk, which creates the accounts, and every case then boots a copy of that disk. The
// configuration values are not saved (no Configuration provider, decision C6), so each boot starts from the
// defaults. The listener serves the declared test certificate, whose key is public. Nothing here operates
// hardware, and nothing here is device timing.
import { spawn } from 'node:child_process';
import type { ChildProcess } from 'node:child_process';
import { createHash, X509Certificate } from 'node:crypto';
import { copyFileSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { connect } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { webRoot } from './harness.ts';

export const DEVICE_PORT = 18220;
export const DEVICE_HOST = 'localhost';
export const DEVICE_URL = `https://${DEVICE_HOST}:${DEVICE_PORT}`;
export const TEST_CERTIFICATE = join(webRoot, '..', 'tests', 'native', 'tls_offload_probe', 'credentials', 'test-server.crt');

export interface Device {
  url: string;
  /** What the image printed, for a failure's report. */
  log(): string;
  close(): Promise<void>;
}

/**
 * The base64 SHA-256 of the test certificate's public key, for Chrome's --ignore-certificate-errors-spki-list.
 * That accepts this one key and no other, for the test only: a browser that meets any other certificate still
 * refuses it. Nothing else turns certificate checking off.
 */
export function testCertificateSpki(): string {
  const certificate = new X509Certificate(readFileSync(TEST_CERTIFICATE));
  const spki = certificate.publicKey.export({ type: 'spki', format: 'der' });
  return createHash('sha256').update(spki).digest('base64');
}

/** Refuses to start while something else holds the port, which would answer in the image's place. */
async function portFree(): Promise<void> {
  const held = await new Promise<boolean>((resolve) => {
    const socket = connect({ host: '127.0.0.1', port: DEVICE_PORT });
    socket.once('connect', () => {
      socket.destroy();
      resolve(true);
    });
    socket.once('error', () => resolve(false));
  });
  if (held) {
    throw new Error(`something already listens on port ${DEVICE_PORT}: stop it, or its answers would be taken for the image's`);
  }
}

function boot(image: string, disk: string, seed: number): { child: ChildProcess; output: () => string } {
  let text = '';
  const child = spawn(image, [`--flash=${disk}`, `--seed=${seed}`], { stdio: ['ignore', 'pipe', 'pipe'] });
  child.stdout?.on('data', (data: Buffer) => (text += data.toString('latin1')));
  child.stderr?.on('data', (data: Buffer) => (text += data.toString('latin1')));
  return { child, output: () => text };
}

async function stop(child: ChildProcess): Promise<void> {
  if (child.exitCode !== null || child.signalCode !== null) {
    return;
  }
  const exited = new Promise<void>((resolve) => child.once('exit', () => resolve()));
  child.kill('SIGTERM');
  const timer = setTimeout(() => child.kill('SIGKILL'), 10_000);
  await exited;
  clearTimeout(timer);
}

async function ready(child: ChildProcess, output: () => string, limitMs: number): Promise<string> {
  const started = Date.now();
  while (Date.now() - started < limitMs) {
    const text = output();
    const line = /^CRUX_E2E_READY .*$/m.exec(text);
    if (line) {
      return line[0];
    }
    if (/CRUX_E2E_START_FAILED/.test(text) || child.exitCode !== null) {
      break;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`the image did not become ready:\n${output().slice(-4000)}`);
}

/** Boots the image on a blank disk once, which creates the accounts, and keeps the disk as the template. */
export class DeviceFactory {
  private readonly directory = mkdtempSync(join(tmpdir(), 'crux-e2e-220-'));
  private readonly template = join(this.directory, 'template.bin');
  private prepared: Promise<void> | undefined;
  private count = 0;
  /** The boot time of each device, in milliseconds, from spawn to its ready line. */
  readonly bootMs: number[] = [];

  private readonly image: string;

  constructor(image: string) {
    this.image = image;
  }

  private async prepare(): Promise<void> {
    await portFree();
    const { child, output } = boot(this.image, this.template, 2200);
    try {
      const line = await ready(child, output, 240_000);
      if (!line.includes('first=1')) {
        throw new Error(`the template boot did not start on a blank disk: ${line}`);
      }
    } finally {
      await stop(child);
    }
  }

  async start(): Promise<Device> {
    await (this.prepared ??= this.prepare());
    const disk = join(this.directory, `case-${++this.count}.bin`);
    copyFileSync(this.template, disk);
    await portFree();
    const begun = Date.now();
    const { child, output } = boot(this.image, disk, 2200 + this.count);
    try {
      const line = await ready(child, output, 120_000);
      if (!line.includes('first=0')) {
        throw new Error(`a case's device did not find the accounts: ${line}`);
      }
    } catch (cause) {
      await stop(child);
      throw cause;
    }
    this.bootMs.push(Date.now() - begun);
    return {
      url: DEVICE_URL,
      log: output,
      async close() {
        await stop(child);
        rmSync(disk, { force: true });
      },
    };
  }

  close(): void {
    rmSync(this.directory, { recursive: true, force: true });
  }
}
