// Read web/api/openapi.yaml into the form that the mock server and the conformance check use.
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parse } from 'yaml';

export interface SpecExample {
  value: unknown;
  setCookie?: string[];
}

export interface SpecResponse {
  status: number;
  schema?: Record<string, unknown>;
  examples: Map<string, SpecExample>;
  /** The response's own `x-crux-set-cookie`, on a response with no body. */
  setCookie?: string[];
  /** Lower-case names of headers that the response must carry. */
  requiredHeaders: string[];
  /** The example value of Retry-After, where the response has one. */
  retryAfterExample?: number;
}

export interface SpecOperation {
  id: string;
  method: string;
  path: string;
  pattern: RegExp;
  access: string;
  requestExamples: Map<string, unknown>;
  responses: Map<number, SpecResponse>;
}

export interface Spec {
  operations: Map<string, SpecOperation>;
  components: Record<string, unknown>;
}

type Node = Record<string, unknown>;

const METHODS = ['get', 'put', 'post', 'delete'];

function resolve(document: Node, node: Node): Node {
  const ref = node['$ref'];
  if (typeof ref !== 'string') {
    return node;
  }
  let current: unknown = document;
  for (const part of ref.slice(2).split('/')) {
    current = (current as Node)[part];
  }
  return resolve(document, current as Node);
}

function readResponse(document: Node, status: number, raw: Node): SpecResponse {
  const node = resolve(document, raw);
  const response: SpecResponse = { status, examples: new Map(), requiredHeaders: [] };
  const setCookie = node['x-crux-set-cookie'];
  if (Array.isArray(setCookie)) {
    response.setCookie = setCookie as string[];
  }
  const content = (node['content'] as Record<string, Node> | undefined)?.['application/json'];
  if (content) {
    response.schema = content['schema'] as Node;
    for (const [name, example] of Object.entries((content['examples'] ?? {}) as Record<string, Node>)) {
      const entry: SpecExample = { value: example['value'] };
      const lines = example['x-crux-set-cookie'];
      if (Array.isArray(lines)) {
        entry.setCookie = lines as string[];
      }
      response.examples.set(name, entry);
    }
  }
  for (const [name, header] of Object.entries((node['headers'] ?? {}) as Record<string, Node>)) {
    const resolved = resolve(document, header);
    if (resolved['required'] === true) {
      response.requiredHeaders.push(name.toLowerCase());
    }
    if (name === 'Retry-After' && typeof resolved['example'] === 'number') {
      response.retryAfterExample = resolved['example'];
    }
  }
  return response;
}

export function loadSpec(path?: string): Spec {
  const file = path ?? join(dirname(fileURLToPath(import.meta.url)), '..', 'api', 'openapi.yaml');
  const document = parse(readFileSync(file, 'utf8')) as Node;
  const operations = new Map<string, SpecOperation>();
  for (const [routePath, item] of Object.entries(document['paths'] as Record<string, Node>)) {
    for (const method of METHODS) {
      const operation = item[method] as Node | undefined;
      if (!operation) {
        continue;
      }
      const id = operation['operationId'] as string;
      const requestExamples = new Map<string, unknown>();
      const requestBody = operation['requestBody'] as Node | undefined;
      const requestContent = (requestBody?.['content'] as Record<string, Node> | undefined)?.['application/json'];
      for (const [name, example] of Object.entries((requestContent?.['examples'] ?? {}) as Record<string, Node>)) {
        requestExamples.set(name, example['value']);
      }
      const responses = new Map<number, SpecResponse>();
      for (const [status, raw] of Object.entries(operation['responses'] as Record<string, Node>)) {
        responses.set(Number(status), readResponse(document, Number(status), raw));
      }
      const source = routePath.replace(/\{[^}]+\}/g, '[^/]+');
      operations.set(id, {
        id,
        method: method.toUpperCase(),
        path: routePath,
        pattern: new RegExp(`^${source}$`),
        access: operation['x-crux-access'] as string,
        requestExamples,
        responses,
      });
    }
  }
  return { operations, components: document['components'] as Record<string, unknown> };
}
