import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { generateTypes } from '../gen-types.ts';

const tool = join(dirname(fileURLToPath(import.meta.url)), '..', 'gen-types.ts');

function spec(schemas: Record<string, unknown>, paths: Record<string, unknown> = {}) {
  return { openapi: '3.0.3', paths, components: { schemas } };
}

test('an object schema gives an interface with required and optional members', () => {
  const out = generateTypes(
    spec({
      Sample: {
        type: 'object',
        additionalProperties: false,
        required: ['a'],
        properties: { a: { type: 'string' }, b: { type: 'integer' }, c: { type: 'boolean' } },
      },
    }),
  );
  assert.match(out, /export interface Sample \{\n {2}a: string;\n {2}b\?: number;\n {2}c\?: boolean;\n\}/);
});

test('an enum gives a union of literals', () => {
  const out = generateTypes(spec({ Kind: { type: 'string', enum: ['one', 'two'] } }));
  assert.match(out, /export type Kind = 'one' \| 'two';/);
});

test('oneOf gives a union and allOf an intersection of references', () => {
  const out = generateTypes(
    spec({
      A: { type: 'string', enum: ['a', 'b'] },
      B: { type: 'object', additionalProperties: false, required: [], properties: {} },
      Either: { oneOf: [{ $ref: '#/components/schemas/A' }, { $ref: '#/components/schemas/B' }] },
      Both: { allOf: [{ $ref: '#/components/schemas/A' }, { type: 'string', enum: ['a'] }] },
    }),
  );
  assert.match(out, /export type Either = A \| B;/);
  assert.match(out, /export type Both = A & 'a';/);
});

test('a union inside allOf keeps its grouping', () => {
  const out = generateTypes(
    spec({
      A: { type: 'string', enum: ['a', 'b', 'c'] },
      Both: { allOf: [{ $ref: '#/components/schemas/A' }, { type: 'string', enum: ['a', 'b'] }] },
    }),
  );
  assert.match(out, /export type Both = A & \('a' \| 'b'\);/);
});

test('an array gives a member array and a nested object stays inline', () => {
  const out = generateTypes(
    spec({
      List: {
        type: 'object',
        additionalProperties: false,
        required: ['items'],
        properties: {
          items: { type: 'array', items: { $ref: '#/components/schemas/Row' } },
          inner: { type: 'object', additionalProperties: false, required: ['x'], properties: { x: { type: 'string' } } },
        },
      },
      Row: { type: 'string' },
    }),
  );
  assert.match(out, /items: Row\[\];/);
  assert.match(out, /inner\?: \{ x: string \};/);
});

for (const [name, schema] of Object.entries({
  anyOf: { anyOf: [{ type: 'string' }] },
  nullable: { type: 'string', nullable: true },
  openObject: { type: 'object', additionalProperties: true, properties: {} },
  noType: { description: 'nothing to go on' },
  foreignRef: { $ref: '#/components/responses/Busy' },
})) {
  test(`a schema using ${name} is refused, so a spec change cannot generate a wrong type silently`, () => {
    assert.throws(() => generateTypes(spec({ Bad: schema })), /Bad/);
  });
}

test('the operation table lists each operation with its method and path', () => {
  const out = generateTypes(
    spec(
      {},
      {
        '/api/v1/a': {
          get: { operationId: 'getA' },
          put: { operationId: 'putA' },
        },
        '/api/v1/b/{id}': { post: { operationId: 'postB' } },
      },
    ),
  );
  assert.match(out, /getA: \{ method: 'GET', path: '\/api\/v1\/a' \}/);
  assert.match(out, /putA: \{ method: 'PUT', path: '\/api\/v1\/a' \}/);
  assert.match(out, /postB: \{ method: 'POST', path: '\/api\/v1\/b\/\{id\}' \}/);
  assert.match(out, /export type OperationId = keyof typeof operations;/);
});

test('the output is stable for the real specification', () => {
  const directory = mkdtempSync(join(tmpdir(), 'crux-gen-'));
  const first = join(directory, 'first.ts');
  const second = join(directory, 'second.ts');
  execFileSync(process.execPath, [tool, '--out', first]);
  execFileSync(process.execPath, [tool, '--out', second]);
  assert.equal(readFileSync(first, 'utf8'), readFileSync(second, 'utf8'));
  assert.match(readFileSync(first, 'utf8'), /export type SessionAttempt =/);
});

test('--check passes on a current file and fails on a stale one', () => {
  const directory = mkdtempSync(join(tmpdir(), 'crux-gen-'));
  const file = join(directory, 'types.ts');
  execFileSync(process.execPath, [tool, '--out', file]);
  assert.equal(spawnSync(process.execPath, [tool, '--check', '--out', file]).status, 0);
  writeFileSync(file, `${readFileSync(file, 'utf8')}// edited\n`);
  const stale = spawnSync(process.execPath, [tool, '--check', '--out', file]);
  assert.equal(stale.status, 1);
  assert.match(stale.stderr.toString(), /npm run gen:types/);
});
