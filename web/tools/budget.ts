// Measure a built folder and check it against a packaging budget. The server's default of three clients
// is why the file count matters as much as the compressed size. Compressed size is gzip at level 9.
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, relative, sep } from 'node:path';
import { gzipSync } from 'node:zlib';

export interface MeasuredFile {
  path: string;
  rawBytes: number;
  compressedBytes: number;
}

export interface Measure {
  files: MeasuredFile[];
  fileCount: number;
  rawBytes: number;
  compressedBytes: number;
}

export interface Budget {
  maxFiles: number;
  maxCompressedBytes: number;
}

function walk(folder: string): string[] {
  return readdirSync(folder)
    .sort()
    .flatMap((entry) => {
      const path = join(folder, entry);
      return statSync(path).isDirectory() ? walk(path) : [path];
    });
}

export function measureFolder(folder: string): Measure {
  const files = walk(folder).map((path): MeasuredFile => {
    const data = readFileSync(path);
    return {
      path: relative(folder, path).split(sep).join('/'),
      rawBytes: data.length,
      compressedBytes: gzipSync(data, { level: 9 }).length,
    };
  });
  if (files.length === 0) {
    throw new Error(`${folder} holds no files`);
  }
  return {
    files,
    fileCount: files.length,
    rawBytes: files.reduce((sum, file) => sum + file.rawBytes, 0),
    compressedBytes: files.reduce((sum, file) => sum + file.compressedBytes, 0),
  };
}

export function checkBudget(measure: Measure, budget: Budget): string[] {
  const problems: string[] = [];
  if (measure.fileCount > budget.maxFiles) {
    problems.push(`budget: ${measure.fileCount} files, over the limit of ${budget.maxFiles}`);
  }
  if (measure.compressedBytes > budget.maxCompressedBytes) {
    problems.push(`budget: ${measure.compressedBytes} compressed bytes, over the limit of ${budget.maxCompressedBytes}`);
  }
  return problems;
}
