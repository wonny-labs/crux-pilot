// Static rules for the frontend sources: no raw HTML paths into the DOM, nothing the content policy would
// block, no external asset, and user text only in the text table. The content policy and the browser tests
// back these up at run time. Usage: node tools/check-dom.ts [folder...]
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

interface Rule {
  pattern: RegExp;
  message: string;
  files: RegExp;
}

const CODE = /\.(ts|tsx)$/;
const TSX = /\.tsx$/;
const HTML = /\.html$/;
const ANY = /\.(ts|tsx|html|css|svg)$/;

const RULES: Rule[] = [
  { pattern: /\binnerHTML\b/, message: 'innerHTML is not allowed', files: CODE },
  { pattern: /\bouterHTML\b/, message: 'outerHTML is not allowed', files: CODE },
  { pattern: /\binsertAdjacentHTML\b/, message: 'insertAdjacentHTML is not allowed', files: CODE },
  { pattern: /\bdangerouslySetInnerHTML\b/, message: 'dangerouslySetInnerHTML is not allowed', files: CODE },
  { pattern: /\bdocument\.write(ln)?\b/, message: 'document.write is not allowed', files: CODE },
  { pattern: /\beval\s*\(/, message: 'eval is not allowed', files: CODE },
  { pattern: /\bnew\s+Function\b/, message: 'new Function is not allowed', files: CODE },
  { pattern: /\bsrcdoc\b/, message: 'srcdoc is not allowed', files: CODE },
  { pattern: /\bsetHTMLUnsafe\b/, message: 'setHTMLUnsafe is not allowed', files: CODE },
  { pattern: /\bcreateContextualFragment\b/, message: 'createContextualFragment is not allowed', files: CODE },
  { pattern: /\bDOMParser\b/, message: 'DOMParser is not allowed', files: CODE },
  { pattern: /\b(localStorage|sessionStorage|indexedDB)\b/, message: 'browser storage is not allowed, so that it can hold no secret', files: CODE },
  { pattern: /\bdocument\.cookie\s*=(?!=)/, message: 'a script must not write a cookie', files: CODE },
  { pattern: /\bstyle\s*=/, message: 'a style attribute is blocked by the strict policy', files: TSX },
  { pattern: /<style\b/, message: 'a style element is blocked by the strict policy', files: TSX },
  { pattern: /<script(?![^>]*\bsrc=)[^>]*>/i, message: 'an inline script is blocked by the policy', files: HTML },
  { pattern: /<style\b/i, message: 'an inline style is blocked by the strict policy', files: HTML },
  { pattern: /\son[a-z]+\s*=/i, message: 'an inline event handler is blocked by the policy', files: HTML },
  { pattern: /\sstyle\s*=/i, message: 'a style attribute is blocked by the strict policy', files: HTML },
  {
    pattern: /(?:[a-z][a-z0-9+.-]*:)?\/\/(?!www\.w3\.org\/2000\/svg)[a-z0-9.-]+\.[a-z]{2,}/i,
    message: 'an external address is not allowed',
    files: ANY,
  },
  {
    pattern: /(?:[a-z][a-z0-9+.-]*:)?\/\/(?:localhost\b|\d{1,3}(?:\.\d{1,3}){3}\b|\[[0-9a-f:]+\])/i,
    message: 'a fixed device or loopback address is not allowed',
    files: ANY,
  },
  {
    pattern: /['"`](?:localhost|\d{1,3}(?:\.\d{1,3}){3})(?::\d+)?(?:\/[^'"`]*)?['"`]/i,
    message: 'a fixed device or loopback address is not allowed',
    files: ANY,
  },
  {
    pattern: /(?<![=\-(,?:&|!<]\s*)(?<!=)>\s*[A-Za-z][A-Za-z ,.'-]*(?:<|$)/,
    message: 'user text belongs in the text table',
    files: TSX,
  },
  {
    pattern: /\b(aria-label|placeholder|title|alt)\s*=\s*["'][^"']+["']/,
    message: 'user text belongs in the text table',
    files: TSX,
  },
];

function stripComments(text: string): string {
  // Lines stay in place, so that a reported line number is right. Only whole-line comments and block
  // comments are removed, so an address inside a string is still seen.
  return text
    .replace(/\/\*[\s\S]*?\*\//g, (block) => block.replace(/[^\n]/g, ' '))
    .replace(/^\s*\/\/.*$/gm, '');
}

export function checkSource(name: string, text: string): string[] {
  const problems: string[] = [];
  const lines = stripComments(text).split('\n');
  for (const rule of RULES) {
    if (!rule.files.test(name)) {
      continue;
    }
    lines.forEach((line, index) => {
      if (rule.pattern.test(line)) {
        problems.push(`${name}:${index + 1}: ${rule.message}`);
      }
    });
  }
  return problems;
}

function listFiles(folder: string): string[] {
  const files: string[] = [];
  for (const entry of readdirSync(folder)) {
    const path = join(folder, entry);
    if (entry === 'node_modules' || entry === 'dist') {
      continue;
    }
    if (statSync(path).isDirectory()) {
      files.push(...listFiles(path));
    } else if (ANY.test(entry)) {
      files.push(path);
    }
  }
  return files;
}

function main(argv: string[]): number {
  const web = join(dirname(fileURLToPath(import.meta.url)), '..');
  const folders = argv.length > 0 ? argv : [join(web, '..', 'samples', 'web', 'preact'), join(web, 'client', 'src')];
  const problems: string[] = [];
  let count = 0;
  for (const folder of folders) {
    for (const file of listFiles(folder)) {
      count += 1;
      problems.push(...checkSource(relative(web, file), readFileSync(file, 'utf8')));
    }
  }
  for (const problem of problems) {
    process.stderr.write(`check-dom: ${problem}\n`);
  }
  if (problems.length === 0) {
    process.stdout.write(`check-dom: ${count} files clean\n`);
  }
  return problems.length === 0 ? 0 : 1;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  process.exitCode = main(process.argv.slice(2));
}
