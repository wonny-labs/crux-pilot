// Generate TypeScript types and the operation table from web/api/openapi.yaml.
//
//   node tools/gen-types.ts            write client/src/api-types.generated.ts
//   node tools/gen-types.ts --check    fail if that file is out of date
//
// It handles only the schema forms that the specification uses and refuses any
// other, so a specification change cannot generate a wrong type silently.
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parse } from 'yaml';

type Schema = Record<string, unknown>;

const SCHEMA_PREFIX = '#/components/schemas/';
const METHODS = ['get', 'put', 'post', 'delete', 'patch', 'head', 'options'];
// Keywords that document a schema, with no effect on its TypeScript type.
const NOTES = ['description', 'summary', 'example', 'examples'];
// The keywords that each form of schema may hold. A schema has exactly one form, and any other keyword
// beside it is refused, because a tool would read the combination differently from this generator.
const FORMS: Record<string, string[]> = {
  $ref: ['$ref'],
  oneOf: ['oneOf'],
  allOf: ['allOf'],
  enum: ['enum', 'type'],
  string: ['type', 'format', 'pattern', 'minLength', 'maxLength'],
  integer: ['type', 'format', 'minimum', 'maximum'],
  number: ['type', 'format', 'minimum', 'maximum'],
  boolean: ['type'],
  array: ['type', 'items', 'minItems', 'maxItems'],
  object: ['type', 'properties', 'required', 'additionalProperties'],
};

function literal(value: unknown, where: string): string {
  if (typeof value !== 'string') {
    throw new Error(`${where}: enum value ${String(value)} is not a string`);
  }
  return `'${value.replaceAll('\\', '\\\\').replaceAll("'", "\\'")}'`;
}

/** The schema's form, once its keywords have been checked against that form. */
function formOf(schema: Schema, where: string): string {
  const marked = ['$ref', 'oneOf', 'allOf', 'enum'].filter((keyword) => keyword in schema);
  if (marked.length > 1) {
    throw new Error(`${where}: ${marked.join(' and ')} cannot be used together`);
  }
  const type = schema['type'];
  const form = marked[0] ?? (typeof type === 'string' ? type : undefined);
  const allowed = form === undefined ? undefined : FORMS[form];
  if (form === undefined || allowed === undefined) {
    throw new Error(`${where}: schema has no type that this generator supports`);
  }
  if (form === 'enum' && type !== undefined && type !== 'string') {
    throw new Error(`${where}: an enum on type ${String(type)} is not supported`);
  }
  for (const keyword of Object.keys(schema)) {
    if (!allowed.includes(keyword) && !NOTES.includes(keyword)) {
      throw new Error(`${where}: schema keyword ${keyword} is not supported with ${form}`);
    }
  }
  return form;
}

function members(schema: Schema, where: string): [string, string][] {
  if (schema['additionalProperties'] !== false) {
    throw new Error(`${where}: an object must set additionalProperties to false`);
  }
  const required = new Set((schema['required'] as string[] | undefined) ?? []);
  const properties = (schema['properties'] ?? {}) as Record<string, Schema>;
  for (const name of required) {
    if (!Object.hasOwn(properties, name)) {
      throw new Error(`${where}: required names ${name}, which is not in properties`);
    }
  }
  return Object.entries(properties).map(([name, value]) => [
    `${name}${required.has(name) ? '' : '?'}`,
    typeOf(value, `${where}.${name}`),
  ]);
}

function typeOf(schema: Schema, where: string): string {
  const form = formOf(schema, where);
  switch (form) {
    case '$ref': {
      const ref = schema['$ref'];
      if (typeof ref !== 'string' || !ref.startsWith(SCHEMA_PREFIX)) {
        throw new Error(`${where}: reference ${String(ref)} is outside the schemas`);
      }
      return ref.slice(SCHEMA_PREFIX.length);
    }
    case 'oneOf':
      return (schema['oneOf'] as Schema[]).map((part, index) => typeOf(part, `${where}.oneOf[${index}]`)).join(' | ');
    case 'allOf':
      return (schema['allOf'] as Schema[])
        .map((part, index) => {
          const inner = typeOf(part, `${where}.allOf[${index}]`);
          return inner.includes(' | ') ? `(${inner})` : inner;
        })
        .join(' & ');
    case 'enum':
      return (schema['enum'] as unknown[]).map((value) => literal(value, where)).join(' | ');
    case 'string':
      return 'string';
    case 'integer':
    case 'number':
      return 'number';
    case 'boolean':
      return 'boolean';
    case 'array': {
      const inner = typeOf((schema['items'] ?? {}) as Schema, `${where}.items`);
      return /[|&]/.test(inner) ? `(${inner})[]` : `${inner}[]`;
    }
    default: {
      const parts = members(schema, where).map(([name, type]) => `${name}: ${type}`);
      return parts.length === 0 ? 'Record<string, never>' : `{ ${parts.join('; ')} }`;
    }
  }
}

function declaration(name: string, schema: Schema): string {
  const description = schema['description'];
  const comment =
    typeof description === 'string'
      ? `/** ${description.trim().replace(/\s*\n\s*/g, ' ').replaceAll('*/', '* /')} */\n`
      : '';
  if (formOf(schema, name) === 'object') {
    const lines = members(schema, name).map(([member, type]) => `  ${member}: ${type};\n`);
    return `${comment}export interface ${name} {\n${lines.join('')}}\n`;
  }
  return `${comment}export type ${name} = ${typeOf(schema, name)};\n`;
}

function operationTable(paths: Record<string, Record<string, { operationId?: string }>>): string {
  const rows: string[] = [];
  for (const [path, item] of Object.entries(paths)) {
    for (const method of METHODS) {
      const operation = item[method];
      if (operation) {
        if (!operation.operationId) {
          throw new Error(`${method} ${path} has no operationId`);
        }
        rows.push(`  ${operation.operationId}: { method: '${method.toUpperCase()}', path: '${path}' },\n`);
      }
    }
  }
  return `export const operations = {\n${rows.join('')}} as const;\n\nexport type OperationId = keyof typeof operations;\n`;
}

export function generateTypes(spec: unknown): string {
  const document = spec as {
    paths?: Record<string, Record<string, { operationId?: string }>>;
    components?: { schemas?: Record<string, Schema> };
  };
  const schemas = document.components?.schemas ?? {};
  const header =
    '// Generated from web/api/openapi.yaml by web/tools/gen-types.ts. Do not edit.\n' +
    '// Run `npm run gen:types` after a change to the specification.\n';
  const declarations = Object.entries(schemas).map(([name, schema]) => declaration(name, schema));
  return [header, ...declarations, operationTable(document.paths ?? {})].join('\n');
}

function main(argv: string[]): number {
  const here = dirname(fileURLToPath(import.meta.url));
  const outIndex = argv.indexOf('--out');
  const out = outIndex >= 0 ? argv[outIndex + 1] : join(here, '..', 'client', 'src', 'api-types.generated.ts');
  if (out === undefined) {
    process.stderr.write('gen-types: --out needs a path\n');
    return 2;
  }
  const generated = generateTypes(parse(readFileSync(join(here, '..', 'api', 'openapi.yaml'), 'utf8')));
  if (argv.includes('--check')) {
    let current = '';
    try {
      current = readFileSync(out, 'utf8');
    } catch {
      // A missing file is reported as out of date below.
    }
    if (current !== generated) {
      process.stderr.write(`gen-types: ${out} is out of date. Run npm run gen:types.\n`);
      return 1;
    }
    return 0;
  }
  writeFileSync(out, generated);
  return 0;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  process.exitCode = main(process.argv.slice(2));
}
