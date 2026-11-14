// What counts as a C build needing Node.js: finding it, running it, or naming the web tools.
const USES: RegExp[] = [
  /\bfind_program\s*\([^)]*\b(node|nodejs|npm|npx|yarn|pnpm)\b/i,
  /\bfind_package\s*\(\s*(node|nodejs)\b/i,
  /\b(node|nodejs|npm)_executable\b/i,
  /\b(npm|npx|nodejs|pnpm|yarn)\b/i,
  /\bcommand\s+(\S*\/)?node\b/i,
  /(^|[\s"'])(\S*\/)?node\s+\S+\.(m?js|cjs|ts)\b/m,
  // The frontend directory of the repository. Not the firmware component of that name: a path segment
  // named exactly `components` before `web/` exempts it, because `components/web/tools` holds the route
  // generator. `mycomponents/` and `.components/` do not, and neither does `components/web/../../web/tools`,
  // whose second `web/tools` has `../` before it. A scan cannot tell `src/components/` from
  // `${DIR}/components/`, so a segment of that name is taken as the firmware tree.
  // Of `web/api`, the specification file is the one thing a C build may name: it is plain YAML that
  // firmware code and tests read. Its lint tools, which need Node.js, and any other file there still
  // count, and so does a path that goes on from the file name, as in `openapi.yaml/../lint`. A Node.js
  // tool run on the file by its bare path is a known gap: the npm and npx rules catch `npx`, no more.
  /(?<!(?:^|[^\w.-])components\/)\bweb\/(client|mock|tools|e2e|package)\b/,
  /(?<!(?:^|[^\w.-])components\/)\bweb\/api\b(?!\/openapi\.yaml(?![\w/-]|\.\w))/,
  /\bsamples\/web\b/,
];

/** The text of each use found, empty when the file has none. */
export function nodeUseIn(text: string): string[] {
  return USES.flatMap((use) => {
    const match = use.exec(text);
    return match ? [match[0].trim().replace(/\s+/g, ' ')] : [];
  });
}
