const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const pages = fs.readFileSync(path.join(__dirname, '..', 'include', 'pages.h'), 'utf8');

function createPage(name, fetch) {
  const html = pages.match(new RegExp(`const char ${name}\\[\\] PROGMEM = R"HTML\\(([\\s\\S]*?)\\)HTML";`))[1];
  const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const elements = new Map();
  for (const match of html.matchAll(/id="([^"]+)"/g)) {
    elements.set(match[1], {
      value: '', textContent: '', disabled: false, hidden: false, children: [],
      appendChild(child) { this.children.push(child); },
      replaceChildren() { this.children = []; },
    });
  }
  const intervals = [];
  const context = {
    document: {
      getElementById: id => elements.get(id),
      createElement: () => ({ value: '', textContent: '', disabled: false }),
    },
    fetch,
    URLSearchParams,
    confirm: () => true,
    setTimeout: callback => queueMicrotask(callback),
    setInterval: callback => intervals.push(callback),
  };
  vm.runInNewContext(script, context);
  return { elements, intervals };
}

function response(data, ok = true) {
  return { ok, json: async () => data };
}

async function settle() {
  await new Promise(resolve => setImmediate(resolve));
}

test('setup scans, handles connection failure, and hands off the home IP', async () => {
  const requests = [];
  let status = { ready: false, connecting: false, message: 'Choose a network.' };
  const fetch = async (url, options = {}) => {
    requests.push({ url, options });
    if (url === '/api/scan' && options.method === 'POST') return response({});
    if (url === '/api/scan') return response({ scanning: false, networks: [
      { ssid: '<img src=x>', signal: -50, security: 'WPA2', supported: true },
      { ssid: 'Enterprise', signal: -70, security: 'unsupported security', supported: false },
    ] });
    if (url === '/api/status') return response(status);
    if (url === '/api/connect' || url === '/api/finish') return response({});
    throw new Error('Unexpected request: ' + url);
  };
  const { elements, intervals } = createPage('SETUP_PAGE', fetch);
  await settle();
  const network = elements.get('network');
  assert.equal(network.children[0].value, '<img src=x>');
  assert.equal(network.children[0].textContent, '<img src=x> (-50 dBm, WPA2)');
  assert.equal(network.children[1].disabled, true);

  network.value = network.children[0].value;
  elements.get('password').value = 'test-password';
  await elements.get('form').onsubmit({ preventDefault() {} });
  const connect = requests.find(request => request.url === '/api/connect');
  assert.equal(connect.options.body.get('ssid'), '<img src=x>');
  assert.equal(connect.options.body.get('password'), 'test-password');
  assert.equal(elements.get('password').value, '');

  status = { ready: false, connecting: false, message: 'Could not connect within 20 seconds.' };
  await intervals[0]();
  assert.equal(elements.get('message').textContent, status.message);
  assert.equal(elements.get('connect').disabled, false);

  status = { ready: true, connecting: false, message: 'Connected.', ip: '192.168.1.42', mdns: false };
  await intervals[0]();
  assert.equal(elements.get('handoff').hidden, false);
  assert.equal(elements.get('ip').href, 'http://192.168.1.42/');
  assert.match(elements.get('mdns').textContent, /unavailable/);
  await elements.get('finish').onclick();
  assert.equal(requests.at(-1).url, '/api/finish');
  assert.equal(elements.get('finish').disabled, false);
});

test('moisture page displays readings and stops polling after reset', async () => {
  const requests = [];
  const fetch = async (url, options = {}) => {
    requests.push({ url, options });
    if (url === '/api/moisture') return response({ raw: 2250, percent: 50 });
    if (url === '/api/reset') return response({ ok: true });
    throw new Error('Unexpected request: ' + url);
  };
  const { elements, intervals } = createPage('MOISTURE_PAGE', fetch);
  await settle();
  assert.equal(elements.get('moisture').textContent, '50%');
  assert.equal(elements.get('raw').textContent, 'Raw ADC reading: 2250');
  await elements.get('reset').onclick();
  assert.match(elements.get('message').textContent, /192\.168\.4\.1/);
  assert.equal(elements.get('reset').disabled, true);
  const count = requests.length;
  await intervals[0]();
  assert.equal(requests.length, count);
});

test('setup keeps a connection error visible during status polling', async () => {
  const fetch = async (url, options = {}) => {
    if (url === '/api/scan' && options.method === 'POST') return response({});
    if (url === '/api/scan') return response({ scanning: false, networks: [] });
    if (url === '/api/status') return response({ ready: false, connecting: false, message: 'Choose a network.' });
    if (url === '/api/connect') return response({ error: 'Invalid Wi-Fi password.' }, false);
    throw new Error('Unexpected request: ' + url);
  };
  const { elements, intervals } = createPage('SETUP_PAGE', fetch);
  await settle();
  await elements.get('form').onsubmit({ preventDefault() {} });
  await intervals[0]();
  assert.equal(elements.get('message').textContent, 'Invalid Wi-Fi password.');
  assert.equal(elements.get('connect').disabled, false);
});
