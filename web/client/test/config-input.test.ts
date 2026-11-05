import assert from 'node:assert/strict';
import { test } from 'node:test';
import { parseConfigInput } from '../src/index.ts';

test('a u32 is a whole number in text, and the device decides the range', () => {
  assert.deepEqual(parseConfigInput('u32', '10'), { ok: true, value: 10 });
  assert.deepEqual(parseConfigInput('u32', ' 0 '), { ok: true, value: 0 });
  // A sign is sent on, so that the device answers out_of_range and this code holds no range rule.
  assert.deepEqual(parseConfigInput('u32', '-1'), { ok: true, value: -1 });
  assert.deepEqual(parseConfigInput('u32', '4294967296'), { ok: true, value: 4294967296 });
});

test('text that is no whole number is refused before any request', () => {
  for (const text of ['', '  ', 'abc', '1.5', '1e3', '0x10', '+5', '1 2', '١٢']) {
    assert.deepEqual(parseConfigInput('u32', text), { ok: false, problem: text.trim() === '' ? 'empty' : 'not_integer' }, text);
  }
});

test('a number too large to hold exactly is refused, because its JSON form would not be the typed digits', () => {
  assert.deepEqual(parseConfigInput('u32', '9007199254740993'), { ok: false, problem: 'not_integer' });
});

test('a string is sent as it was typed, with its spaces, and the device decides what it accepts', () => {
  assert.deepEqual(parseConfigInput('string', ' a b '), { ok: true, value: ' a b ' });
  assert.deepEqual(parseConfigInput('string', ''), { ok: true, value: '' });
});

test('a bool has no text form', () => {
  assert.deepEqual(parseConfigInput('bool', 'true'), { ok: false, problem: 'not_text' });
});
