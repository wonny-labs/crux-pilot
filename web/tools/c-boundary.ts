// What counts as a C build needing Node.js: finding it, running it, or naming the web tools.
const USES: RegExp[] = [
  /\bfind_program\s*\([^)]*\b(node|nodejs|npm|npx|yarn|pnpm)\b/i,
  /\bfind_package\s*\(\s*(node|nodejs)\b/i,
  /\b(node|nodejs|npm)_executable\b/i,
  /\b(npm|npx|nodejs|pnpm|yarn)\b/i,
  /\bcommand\s+(\S*\/)?node\b/i,
  /(^|[\s"'])(\S*\/)?node\s+\S+\.(m?js|cjs|ts)\b/m,
  // The frontend directory of the repository. Not `components/web/`, the firmware component of that
  // name, whose own `components/web/tools` holds the route generator. Of `web/api`, the specification
  // file is the one thing a C build may name: it is plain YAML that firmware code and tests read, and
  // no Node.js runs on it. Its lint tools, which need Node.js, and any other file there still count.
  /(?<!components\/)\bweb\/(client|reference|mock|tools|e2e|package)\b/,
  /(?<!components\/)\bweb\/api\b(?!\/openapi\.yaml(?![\w-]|\.\w))/,
  /\bsamples\/web\b/,
];

/** The text of each use found, empty when the file has none. */
export function nodeUseIn(text: string): string[] {
  return USES.flatMap((use) => {
    const match = use.exec(text);
    return match ? [match[0].trim().replace(/\s+/g, ' ')] : [];
  });
}
