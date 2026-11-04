import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { generateTypes } from '../gen-types.ts';
import { tempFolder } from './support.ts';

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

const objectShape = { additionalProperties: false, required: ['a'], properties: { a: { type: 'string' } } };
const reference = { $ref: '#/components/schemas/Row' };

for (const [name, schema] of Object.entries({
  anyOf: { anyOf: [{ type: 'string' }] },
  nullable: { type: 'string', nullable: true },
  openObject: { type: 'object', additionalProperties: true, properties: {} },
  noType: { description: 'nothing to go on' },
  foreignRef: { $ref: '#/components/responses/Busy' },
  'allOf with properties and required': { allOf: [reference], ...objectShape },
  'oneOf with properties': { oneOf: [reference], properties: { a: { type: 'string' } } },
  '$ref with enum': { ...reference, enum: ['a'] },
  '$ref with type': { ...reference, type: 'string' },
  '$ref with properties': { ...reference, properties: { a: { type: 'string' } } },
  'type object with oneOf': { type: 'object', ...objectShape, oneOf: [reference] },
  'type object with allOf': { type: 'object', ...objectShape, allOf: [reference] },
  'type object with items': { type: 'object', ...objectShape, items: { type: 'string' } },
  'type object with enum': { type: 'object', ...objectShape, enum: ['a'] },
  'type string with properties': { type: 'string', properties: { a: { type: 'string' } } },
  'type string with required': { type: 'string', required: ['a'] },
  'type string with items': { type: 'string', items: { type: 'string' } },
  'type array with properties': { type: 'array', items: { type: 'string' }, properties: {} },
  'type boolean with additionalProperties': { type: 'boolean', additionalProperties: false },
  'oneOf with allOf': { oneOf: [reference], allOf: [reference] },
  'oneOf with enum': { oneOf: [reference], enum: ['a'] },
  'required naming an absent member': { type: 'object', additionalProperties: false, required: ['b'], properties: { a: { type: 'string' } } },
  'required naming a built-in property': { type: 'object', additionalProperties: false, required: ['constructor'], properties: { a: { type: 'string' } } },
  'type integer with a string enum': { type: 'integer', enum: ['one', 'two'] },
  'type boolean with an enum': { type: 'boolean', enum: [true] },
  'an enum holding a number': { type: 'string', enum: [1] },
  'an enum with properties': { type: 'string', enum: ['a'], properties: {} },
})) {
  test(`a schema using ${name} is refused, so a spec change cannot generate a wrong type silently`, () => {
    assert.throws(() => generateTypes(spec({ Row: { type: 'string' }, Bad: schema })), /Bad/);
  });
}

test('a refused form inside a member is refused too', () => {
  const nested = {
    type: 'object',
    additionalProperties: false,
    required: ['a'],
    properties: { a: { type: 'integer', enum: ['x'] } },
  };
  assert.throws(() => generateTypes(spec({ Bad: nested })), /Bad\.a/);
});

test('a description beside a reference, a oneOf or an allOf is allowed, because it changes no type', () => {
  const out = generateTypes(
    spec({
      Row: { type: 'string' },
      Ref: { description: 'text', ...reference },
      Either: { description: 'text', oneOf: [reference] },
      Both: { description: 'text', allOf: [reference] },
    }),
  );
  assert.match(out, /export type Ref = Row;/);
});

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

test('the output is stable for the real specification', (t) => {
  const directory = tempFolder(t);
  const first = join(directory, 'first.ts');
  const second = join(directory, 'second.ts');
  execFileSync(process.execPath, [tool, '--out', first]);
  execFileSync(process.execPath, [tool, '--out', second]);
  assert.equal(readFileSync(first, 'utf8'), readFileSync(second, 'utf8'));
  assert.match(readFileSync(first, 'utf8'), /export type SessionAttempt =/);
});

test('--check passes on a current file and fails on a stale one', (t) => {
  const directory = tempFolder(t);
  const file = join(directory, 'types.ts');
  execFileSync(process.execPath, [tool, '--out', file]);
  assert.equal(spawnSync(process.execPath, [tool, '--check', '--out', file]).status, 0);
  writeFileSync(file, `${readFileSync(file, 'utf8')}// edited\n`);
  const stale = spawnSync(process.execPath, [tool, '--check', '--out', file]);
  assert.equal(stale.status, 1);
  assert.match(stale.stderr.toString(), /npm run gen:types/);
});
