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
// Keywords that only document or constrain a value, with no effect on its TypeScript type.
const IGNORED = new Set([
  'description',
  'summary',
  'example',
  'examples',
  'format',
  'pattern',
  'minLength',
  'maxLength',
  'minimum',
  'maximum',
  'minItems',
  'maxItems',
]);
const SHAPE = new Set([
  '$ref',
  'oneOf',
  'allOf',
  'enum',
  'type',
  'items',
  'properties',
  'required',
  'additionalProperties',
]);

function literal(value: unknown): string {
  if (typeof value !== 'string') {
    throw new Error(`enum value ${String(value)} is not a string`);
  }
  return `'${value.replaceAll('\\', '\\\\').replaceAll("'", "\\'")}'`;
}

function checkKeywords(schema: Schema, where: string): void {
  for (const keyword of Object.keys(schema)) {
    if (!IGNORED.has(keyword) && !SHAPE.has(keyword)) {
      throw new Error(`${where}: schema keyword ${keyword} is not supported`);
    }
  }
}

function members(schema: Schema, where: string): [string, string][] {
  if (schema['additionalProperties'] !== false) {
    throw new Error(`${where}: an object must set additionalProperties to false`);
  }
  const required = new Set((schema['required'] as string[] | undefined) ?? []);
  const properties = (schema['properties'] ?? {}) as Record<string, Schema>;
  return Object.entries(properties).map(([name, value]) => [
    `${name}${required.has(name) ? '' : '?'}`,
    typeOf(value, `${where}.${name}`),
  ]);
}

function typeOf(schema: Schema, where: string): string {
  checkKeywords(schema, where);
  const ref = schema['$ref'];
  if (typeof ref === 'string') {
    if (!ref.startsWith(SCHEMA_PREFIX)) {
      throw new Error(`${where}: reference ${ref} is outside the schemas`);
    }
    return ref.slice(SCHEMA_PREFIX.length);
  }
  const oneOf = schema['oneOf'] as Schema[] | undefined;
  if (oneOf) {
    return oneOf.map((part, index) => typeOf(part, `${where}.oneOf[${index}]`)).join(' | ');
  }
  const allOf = schema['allOf'] as Schema[] | undefined;
  if (allOf) {
    return allOf
      .map((part, index) => {
        const inner = typeOf(part, `${where}.allOf[${index}]`);
        return inner.includes(' | ') ? `(${inner})` : inner;
      })
      .join(' & ');
  }
  const values = schema['enum'] as unknown[] | undefined;
  if (values) {
    return values.map(literal).join(' | ');
  }
  switch (schema['type']) {
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
    case 'object': {
      const parts = members(schema, where).map(([name, type]) => `${name}: ${type}`);
      return parts.length === 0 ? 'Record<string, never>' : `{ ${parts.join('; ')} }`;
    }
    default:
      throw new Error(`${where}: schema has no type that this generator supports`);
  }
}

function declaration(name: string, schema: Schema): string {
  const description = schema['description'];
  const comment =
    typeof description === 'string'
      ? `/** ${description.trim().replace(/\s*\n\s*/g, ' ').replaceAll('*/', '* /')} */\n`
      : '';
  checkKeywords(schema, name);
  if (schema['type'] === 'object' && !schema['enum']) {
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
