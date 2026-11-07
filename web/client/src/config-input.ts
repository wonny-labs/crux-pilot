// The text of a configuration field as a JSON value. It checks only that the text can be that type, and
// leaves every range and rule to the device, whose answer is the validation that counts.
import type { ConfigItem } from './api-types.generated.ts';

export type ConfigInputProblem = 'empty' | 'not_integer' | 'not_text';

export type ConfigInputResult = { ok: true; value: number | string } | { ok: false; problem: ConfigInputProblem };

export function parseConfigInput(type: ConfigItem['type'], text: string): ConfigInputResult {
  if (type === 'bool') {
    return { ok: false, problem: 'not_text' };
  }
  if (type === 'string') {
    return { ok: true, value: text };
  }
  const trimmed = text.trim();
  if (trimmed === '') {
    return { ok: false, problem: 'empty' };
  }
  // Digits with an optional minus. A number that a double cannot hold exactly would reach the device as
  // other digits than the ones typed, so it is refused here.
  if (!/^-?[0-9]+$/.test(trimmed) || !Number.isSafeInteger(Number(trimmed))) {
    return { ok: false, problem: 'not_integer' };
  }
  return { ok: true, value: Number(trimmed) };
}
