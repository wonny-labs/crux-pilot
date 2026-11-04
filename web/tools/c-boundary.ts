// What counts as a C build needing Node.js: finding it, running it, or naming the web tools.
const USES: RegExp[] = [
  /\bfind_program\s*\([^)]*\b(node|nodejs|npm|npx|yarn|pnpm)\b/i,
  /\bfind_package\s*\(\s*(node|nodejs)\b/i,
  /\b(node|nodejs|npm)_executable\b/i,
  /\b(npm|npx|nodejs|pnpm|yarn)\b/i,
  /\bcommand\s+(\S*\/)?node\b/i,
  /(^|[\s"'])(\S*\/)?node\s+\S+\.(m?js|cjs|ts)\b/m,
  /\bweb\/(client|reference|mock|tools|api|e2e|package)\b/,
  /\bsamples\/web\b/,
];

/** The text of each use found, empty when the file has none. */
export function nodeUseIn(text: string): string[] {
  return USES.flatMap((use) => {
    const match = use.exec(text);
    return match ? [match[0].trim().replace(/\s+/g, ' ')] : [];
  });
}
