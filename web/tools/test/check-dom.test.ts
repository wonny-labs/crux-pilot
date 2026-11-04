import assert from 'node:assert/strict';
import { test } from 'node:test';
import { checkSource } from '../check-dom.ts';

function problems(name: string, text: string): string[] {
  return checkSource(name, text);
}

const banned: [string, string][] = [
  ['innerHTML assignment', 'node.innerHTML = value;'],
  ['outerHTML assignment', 'node.outerHTML = value;'],
  ['insertAdjacentHTML', 'node.insertAdjacentHTML("beforeend", value);'],
  ['dangerouslySetInnerHTML', '<div dangerouslySetInnerHTML={{ __html: value }} />'],
  ['document.write', 'document.write(value);'],
  ['eval', 'eval(value);'],
  ['new Function', 'new Function(value);'],
  ['srcdoc', '<iframe srcdoc={value} />'],
  ['a style attribute, which the strict policy blocks', '<div style="color: red" />'],
  ['a style binding, which the strict policy blocks', '<div style={{ color: "red" }} />'],
];

for (const [name, code] of banned) {
  test(`a tsx file with ${name} is refused`, () => {
    assert.notDeepEqual(problems('a.tsx', code), []);
  });
}

const external: [string, string, string][] = [
  ['an external script', 'a.html', '<script src="https://cdn.example/x.js"></script>'],
  ['an external style', 'a.html', '<link rel="stylesheet" href="https://cdn.example/x.css">'],
  ['a protocol-relative asset', 'a.html', '<script src="//cdn.example/x.js"></script>'],
  ['a CSS import', 'a.css', '@import url("https://fonts.example/x.css");'],
  ['a CSS font', 'a.css', '@font-face { src: url(https://fonts.example/x.woff2); }'],
  ['an import from the network', 'a.ts', 'import x from "https://esm.example/x.js";'],
  ['a fetch to another host', 'a.ts', 'fetch("https://elsewhere.example/api");'],
];

for (const [name, file, code] of external) {
  test(`${name} is refused`, () => {
    assert.notDeepEqual(problems(file, code), []);
  });
}

test('the SVG namespace is not an external asset', () => {
  assert.deepEqual(problems('a.svg', '<svg xmlns="http://www.w3.org/2000/svg"></svg>'), []);
});

test('an inline script or style in HTML is refused, because the policy would block it', () => {
  assert.notDeepEqual(problems('a.html', '<script>alert(1)</script>'), []);
  assert.notDeepEqual(problems('a.html', '<style>p { color: red }</style>'), []);
  assert.notDeepEqual(problems('a.html', '<button onclick="x()">x</button>'), []);
  assert.notDeepEqual(problems('a.html', '<p style="color: red">x</p>'), []);
});

test('a module script from the same origin is fine in HTML', () => {
  assert.deepEqual(problems('a.html', '<script type="module" src="./assets/app.js"></script>'), []);
});

test('user text in JSX is refused, because it belongs in the text table', () => {
  assert.notDeepEqual(problems('a.tsx', '<p>Sign in</p>'), []);
  assert.notDeepEqual(problems('a.tsx', '<button aria-label="Close">{x}</button>'), []);
  assert.notDeepEqual(problems('a.tsx', '<input placeholder="Name" />'), []);
});

test('expressions, arrows and comparisons in JSX are fine', () => {
  assert.deepEqual(problems('a.tsx', '<p>{text.title}</p>'), []);
  assert.deepEqual(problems('a.tsx', 'const f = (x: number) => x;'), []);
  assert.deepEqual(problems('a.tsx', 'if (seconds > 0) {\n  run();\n}'), []);
  assert.deepEqual(problems('a.tsx', 'const [a, b] = useState<string>(value);'), []);
  assert.deepEqual(problems('a.tsx', '<input aria-label={text.name} />'), []);
});

test('a banned name in a comment does not count', () => {
  assert.deepEqual(problems('a.ts', '// Never use innerHTML here.\nconst x = 1;'), []);
  assert.deepEqual(problems('a.ts', '/* eval(x) */\nconst x = 1;'), []);
});

test('each problem names the file, the line and the rule', () => {
  const [problem] = problems('src/a.tsx', 'const x = 1;\nnode.innerHTML = y;');
  assert.match(problem ?? '', /^src\/a\.tsx:2: /);
  assert.match(problem ?? '', /innerHTML/);
});

for (const [name, code] of Object.entries({
  setHTMLUnsafe: 'node.setHTMLUnsafe(value);',
  createContextualFragment: 'document.createRange().createContextualFragment(value);',
  DOMParser: 'new DOMParser().parseFromString(value, "text/html");',
})) {
  test(`a tsx file with ${name} is refused`, () => {
    assert.notDeepEqual(problems('a.tsx', code), []);
  });
}

test('a style element in JSX is refused, because the strict policy blocks it', () => {
  assert.notDeepEqual(problems('a.tsx', '<style>{css}</style>'), []);
});

for (const [name, code] of Object.entries({
  localStorage: 'localStorage.setItem("user", value);',
  sessionStorage: 'window.sessionStorage.setItem("token", value);',
  indexedDB: 'indexedDB.open("store");',
  'a cookie written by script': 'document.cookie = "a=b";',
})) {
  test(`${name} is refused, so that no secret can reach browser storage`, () => {
    assert.notDeepEqual(problems('a.ts', code), []);
  });
}

test('reading the cookie is still allowed, because the CSRF token is read that way', () => {
  assert.deepEqual(problems('a.ts', 'return document.cookie;'), []);
  assert.deepEqual(problems('a.ts', 'if (document.cookie === value) {'), []);
});

for (const [name, code] of Object.entries({
  'a URL with an IP address': 'fetch("http://192.0.2.10/api");',
  'a protocol-relative IP address': '<img src="//192.0.2.10/x.png">',
  'a localhost URL': 'fetch("http://localhost:8080/api");',
  'a loopback URL': 'fetch("https://127.0.0.1/api");',
  'an IPv6 URL': 'fetch("http://[::1]:8080/api");',
  'a WebSocket address': 'new WebSocket("wss://device.example/x");',
  'a bare address in a string': 'const device = "192.0.2.10";',
  'a bare address with a path': 'fetch("192.0.2.10/api");',
  'a bare localhost in a string': 'fetch("localhost:8080/api");',
})) {
  test(`${name} is refused`, () => {
    assert.notDeepEqual(problems('a.ts', code), []);
  });
}

test('an address inside a sentence of the text table is fine', () => {
  assert.deepEqual(problems('a.ts', "not_ipv4: 'Enter an address, such as 192.0.2.20.',"), []);
});
