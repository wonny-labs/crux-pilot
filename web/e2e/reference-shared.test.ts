import { join } from 'node:path';
import { webRoot } from './harness.ts';
import { sharedCases } from './shared-cases.ts';

sharedCases({ name: 'reference', dist: join(webRoot, 'reference', 'dist'), stylePolicy: 'strict' });
