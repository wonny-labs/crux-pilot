import { join } from 'node:path';
import { webRoot } from './harness.ts';
import { sharedCases } from './shared-cases.ts';

sharedCases({ name: 'preact', dist: join(webRoot, '..', 'samples', 'web', 'preact', 'dist'), stylePolicy: 'strict' });
