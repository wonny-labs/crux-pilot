// Check one HTTP answer against the specification. It returns a list of problems, empty when the
// answer conforms. The firmware adapter can be checked against the same function in M11.
import { Ajv } from 'ajv';
import type { ValidateFunction } from 'ajv';
import type { Spec } from './spec.ts';

export interface ResponseRecord {
  path: string;
  method: string;
  /** The matched operation, or null for an answer outside any operation. */
  operationId: string | null;
  status: number;
  contentType: string | null;
  /** Lower-case names, leaving out Set-Cookie. */
  headers: Record<string, string>;
  setCookie: string[];
  bodyText: string;
}

const COMMON_HEADERS = ['content-security-policy', 'x-content-type-options', 'referrer-policy', 'cache-control'];

const ajv = new Ajv({ strict: false, allErrors: true });
ajv.addFormat('ipv4', /^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}$/);
ajv.addFormat('password', true);

const compiled = new WeakMap<object, ValidateFunction>();

function validatorFor(spec: Spec, schema: Record<string, unknown>): ValidateFunction {
  let validate = compiled.get(schema);
  if (!validate) {
    // Component references use #/components/..., so the schema carries the components with it.
    validate = ajv.compile({ ...schema, components: spec.components });
    compiled.set(schema, validate);
  }
  return validate;
}

function checkCommonHeaders(record: ResponseRecord, problems: string[]): void {
  for (const header of COMMON_HEADERS) {
    if (record.headers[header] === undefined) {
      problems.push(`missing header ${header}`);
    }
  }
}

function checkOutsideOperation(record: ResponseRecord, problems: string[]): void {
  if (record.contentType?.includes('json')) {
    // The JSON 404 for a path under a registered route.
    if (record.status !== 404 || record.bodyText !== '{"code":"not_found"}') {
      problems.push(`unexpected JSON answer outside the operations: ${record.bodyText}`);
    }
    checkCommonHeaders(record, problems);
    return;
  }
  const plain: Record<number, string> = { 404: 'Not Found', 405: 'Method Not Allowed' };
  if (plain[record.status] !== record.bodyText) {
    problems.push(`not one of the server's plain answers: ${record.status} ${record.bodyText}`);
  }
}

export function checkResponse(spec: Spec, record: ResponseRecord): string[] {
  const problems: string[] = [];
  if (record.operationId === null) {
    checkOutsideOperation(record, problems);
    return problems;
  }
  const operation = spec.operations.get(record.operationId);
  if (!operation) {
    return [`unknown operation ${record.operationId}`];
  }
  const response = operation.responses.get(record.status);
  if (!response) {
    return [`status ${record.status} is not declared`];
  }
  if (record.status === 409) {
    // The server's own answer: no body and none of the API's headers.
    if (record.bodyText !== '') {
      problems.push('a 409 must have no body');
    }
    return problems;
  }
  checkCommonHeaders(record, problems);
  for (const header of response.requiredHeaders) {
    const value = record.headers[header];
    if (value === undefined) {
      problems.push(`missing required header ${header}`);
    } else if (header === 'retry-after' && !/^[1-9][0-9]*$/.test(value)) {
      problems.push(`Retry-After is not whole seconds: ${value}`);
    }
  }
  if (!response.schema) {
    if (record.bodyText !== '') {
      problems.push('this response has no body in the specification');
    }
    return problems;
  }
  if (!record.contentType?.includes('application/json')) {
    problems.push(`content type is ${record.contentType ?? 'absent'}, not JSON`);
    return problems;
  }
  let body: unknown;
  try {
    body = JSON.parse(record.bodyText);
  } catch {
    problems.push('the body is not JSON');
    return problems;
  }
  const validate = validatorFor(spec, response.schema);
  if (!validate(body)) {
    for (const error of validate.errors ?? []) {
      problems.push(`${error.instancePath || '/'} ${error.message ?? 'is invalid'}`);
    }
  }
  return problems;
}
