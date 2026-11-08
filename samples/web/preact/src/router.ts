// A small hash router. The server sees only the root path and the assets, so a reload needs no fallback.
import { useEffect, useState } from 'preact/hooks';

function currentPath(): string {
  const hash = window.location.hash.slice(1);
  return hash === '' ? '/' : hash;
}

export function useHashPath(): string {
  const [path, setPath] = useState(currentPath);
  useEffect(() => {
    const update = (): void => setPath(currentPath());
    window.addEventListener('hashchange', update);
    return () => window.removeEventListener('hashchange', update);
  }, []);
  return path;
}

export function hrefFor(path: string): string {
  return `#${path}`;
}
