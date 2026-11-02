#!/usr/bin/env node
// Contract test for the browser UI that the firmware serves (include/web_ui.h).
//
// Runs the *real* HTML and script from that header against a minimal DOM shim,
// then:
//   1. checks that the page renders the configuration it fetches,
//   2. checks the document it submits has exactly the 13 members, with the
//      types, that src/main.cpp's decode() requires,
//   3. optionally writes that submitted body to disk so the C++ harness can
//      POST the browser's own bytes into the firmware.
//
// The point is the seam: the UI and the firmware are two independent
// encoders/decoders of the same JSON document, and nothing else in the test
// suite would notice if they drifted apart.
//
// Usage: node ui_contract_test.mjs [--root DIR] [--emit FILE] [--config FILE]
//
// Only Node built-ins are used; there are no dependencies to install.

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------
const args = process.argv.slice(2);
let root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
let emitPath = null;
let configPath = null;
for (let i = 0; i < args.length; ++i) {
  if (args[i] === '--root') root = path.resolve(args[++i]);
  else if (args[i] === '--emit') emitPath = path.resolve(args[++i]);
  else if (args[i] === '--config') configPath = path.resolve(args[++i]);
  else {
    console.error(`unknown argument: ${args[i]}`);
    process.exit(64);
  }
}

// ---------------------------------------------------------------------------
// Extract the real page and script from the firmware header
// ---------------------------------------------------------------------------
const headerPath = path.join(root, 'include', 'web_ui.h');
if (!fs.existsSync(headerPath)) {
  console.log(`SKIP  ui contract  (${headerPath} not found)`);
  process.exit(0);
}
const header = fs.readFileSync(headerPath, 'utf8');
const rawStart = header.indexOf('R"HTML(');
const rawEnd = header.lastIndexOf(')HTML"');
if (rawStart < 0 || rawEnd < 0) {
  console.error('FAIL  web_ui.h does not contain an R"HTML( ... )HTML" literal');
  process.exit(1);
}
const html = header.slice(rawStart + 'R"HTML('.length, rawEnd);
const scriptMatch = html.match(/<script>([\s\S]*?)<\/script>/);
if (!scriptMatch) {
  console.error('FAIL  web_ui.h has no <script> block');
  process.exit(1);
}
const script = scriptMatch[1];

// ---------------------------------------------------------------------------
// Minimal DOM
// ---------------------------------------------------------------------------
let failures = 0;
let checks = 0;
const check = (ok, what) => {
  ++checks;
  if (!ok) {
    ++failures;
    console.log(`  FAIL  ${what}`);
  }
};

class Element {
  constructor(tag, attrs = {}) {
    this.tagName = tag;
    this.attrs = attrs;
    this.children = [];
    this.type = attrs.type || '';
    this.name = attrs.name || '';
    this.value = attrs.value || '';
    this.checked = Object.prototype.hasOwnProperty.call(attrs, 'checked');
    this.textContent = '';
    this.innerHTML = '';
  }
  addEventListener(event, fn) { this[event] = fn; }
  append(child) {
    this.children.push(child);
  }
  get descendants() {
    return this.children.flatMap((child) => (child.tagName === 'input' ? [child] : child.descendants));
  }
}

const form = new Element('form');
const status = new Element('p');
const colors = new Element('div');
const message = new Element('p');
const save = new Element('button');
const reload = new Element('button');

// Inputs that exist in the served markup (parsed, not hard-coded).
for (const match of html.matchAll(/<input\s+([^>]*?)\/?>/g)) {
  const attrs = Object.fromEntries([...match[1].matchAll(/([\w-]+)="([^"]*)"/g)].map((m) => [m[1], m[2]]));
  const input = new Element('input', attrs);
  if (input.type === 'color' && !input.value) input.value = '#000000';
  form.append(input);
}
form.append(colors);
const allInputs = () => form.descendants;

form.elements = new Proxy(
  {},
  { get: (_, name) => allInputs().find((input) => input.name === name) },
);
form.querySelectorAll = (selector) => (selector === 'input' ? allInputs() : []);

const document = {
  querySelector: (selector) => (selector === 'form' ? form : selector === '#status' ? status : selector === '#colors' ? colors : selector === '#message' ? message : selector === '#save' ? save : selector === '#reload' ? reload : null),
  createElement: (tag) => new Element(tag),
};

// ---------------------------------------------------------------------------
// Network stub: the payloads the firmware really serves
// ---------------------------------------------------------------------------
const serverConfig = configPath
  ? JSON.parse(fs.readFileSync(configPath, 'utf8'))
  : {
      count: 60, offset: 0, clockwise: true, idle: false, period: 4.5, low: 10, high: 50,
      timezone: 'UTC0', am: [255, 100, 20], pm: [180, 40, 255], minute: [20, 150, 255],
      second: [255, 30, 40], background: [255, 255, 255],
    };

let submitted = null;
let submittedType = null;
let submittedRevision = null;
let revision = '"test-0"';
const headers = {get: (name) => name.toLowerCase() === 'etag' ? revision : null};
let statusResponse = { connected: false, synchronized: true };
let saveResponse = { ok: true, code: 200, text: '{"saved":true}' };

const fetchStub = async (url, options = {}) => {
  const method = options.method || 'GET';
  if (url === '/api/config' && method === 'GET') {
    return { ok: true, status: 200, headers, json: async () => serverConfig, text: async () => JSON.stringify(serverConfig) };
  }
  if (url === '/api/status') {
    return { ok: true, status: 200, json: async () => statusResponse, text: async () => JSON.stringify(statusResponse) };
  }
  if (url === '/api/config' && method === 'POST') {
    submitted = options.body;
    submittedType = (options.headers || {})['Content-Type'];
    submittedRevision = (options.headers || {})['If-Match'];
    return { ok: saveResponse.ok, status: saveResponse.code, headers, json: async () => ({}), text: async () => saveResponse.text };
  }
  throw new Error(`unexpected request: ${method} ${url}`);
};

// ---------------------------------------------------------------------------
// Run the page script
// ---------------------------------------------------------------------------
const run = new Function('document', 'fetch', 'setInterval', `${script}\n;return {form, status, message};`);
const page = run(document, fetchStub, () => 0);
await new Promise((resolve) => setTimeout(resolve, 25)); // let refresh() settle

console.log('ui_contract: browser payload vs firmware contract');

// 1. The page shows what the firmware reports.
const num = (name) => Number(form.elements[name].value);
check(num('count') === serverConfig.count, `count field shows ${serverConfig.count}`);
check(num('period') === serverConfig.period, `period field shows ${serverConfig.period}`);
check(num('low') === serverConfig.low, 'low brightness field');
check(num('high') === serverConfig.high, 'high brightness field');
check(form.elements.clockwise.checked === serverConfig.clockwise, 'clockwise checkbox');
check(form.elements.idle.checked === serverConfig.idle, 'idle checkbox');
check(form.elements.timezone.value === serverConfig.timezone, 'timezone field');
const hex = (rgb) => `#${rgb.map((n) => n.toString(16).padStart(2, '0')).join('')}`;
for (const key of ['am', 'pm', 'minute', 'second', 'background'])
  check(form.elements[key] && form.elements[key].value === hex(serverConfig[key]), `${key} colour field shows #rrggbb`);
check(/Offline|synchronized|Waiting/i.test(page.status.textContent), 'status line reflects /api/status');

// 2. Editing and submitting produces the document the firmware accepts.
form.elements.count.value = '90';
form.elements.offset.value = '3';
form.elements.period.value = '3.5';
form.elements.low.value = '5';
form.elements.high.value = '60';
form.elements.idle.checked = true;
form.elements.second.value = '#00ff00';
await page.form.onsubmit({ preventDefault() {} });

check(submitted !== null, 'submit issues a POST to /api/config');
check(submittedType === 'application/json', 'submit uses Content-Type: application/json');
check(submittedRevision === revision, 'submit uses current If-Match revision');
if (submitted) {
  let body = null;
  try {
    body = JSON.parse(submitted);
  } catch {
    check(false, 'submitted body is valid JSON');
  }
  if (body) {
    const expectedKeys = ['count', 'offset', 'clockwise', 'timezone', 'period', 'low', 'high', 'idle',
      'am', 'pm', 'minute', 'second', 'background'];
    check(Object.keys(body).length === expectedKeys.length, `submits exactly ${expectedKeys.length} members (decode() rejects anything else)`);
    for (const key of expectedKeys) check(key in body, `submits "${key}"`);
    check(body.count === 90 && Number.isInteger(body.count), 'count is sent as an integer');
    check(body.offset === 3 && Number.isInteger(body.offset), 'offset is sent as an integer');
    check(Math.abs(body.period - 3.5) < 1e-9, 'period is sent as a number');
    check(body.low === 5 && body.high === 60, 'brightness levels are sent as numbers');
    check(body.clockwise === true && body.idle === true, 'booleans stay booleans');
    check(typeof body.timezone === 'string', 'timezone is sent as a string');
    for (const key of ['am', 'pm', 'minute', 'second', 'background']) {
      check(Array.isArray(body[key]) && body[key].length === 3 && body[key].every((n) => Number.isInteger(n) && n >= 0 && n <= 255),
        `${key} is [r,g,b] with 8-bit channels`);
    }
    check(JSON.stringify(body.second) === '[0,255,0]', 'an edited colour reaches the payload');
    if (emitPath) {
      fs.writeFileSync(emitPath, submitted);
      console.log(`  wrote submitted body to ${emitPath}`);
    }
  }
}

// 3. A rejected save is surfaced to the user, without injecting markup.
saveResponse = { ok: false, code: 400, text: '<img src=x onerror=alert(1)>Invalid settings' };
await page.form.onsubmit({ preventDefault() {} });
check(page.message.textContent.includes('Invalid settings'), 'rejection text is shown to the user');
check(!page.message.innerHTML, 'rejection text is not written as HTML');

// 4. The page must not build markup from server or user data.
check(!/innerHTML\s*=/.test(script), 'UI does not assign innerHTML');
check(!/document\.write/.test(script), 'UI does not use document.write');

console.log(`${checks} checks, ${failures} failures`);
process.exit(failures ? 1 : 0);
