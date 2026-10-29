'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const repoRoot = path.resolve(__dirname, '..');
const htmlPath = path.join(repoRoot, 'web', 'index.html');
const htmlBytes = fs.readFileSync(htmlPath);
const html = htmlBytes.toString('utf8');
assert.notEqual(htmlBytes.subarray(0, 3).toString('hex'), 'efbbbf', 'HTML must not have a UTF-8 BOM');
assert.match(html, /\r\n/, 'HTML should use CRLF line endings');
assert.doesNotMatch(html.replace(/\r\n/g, ''), /\n/, 'HTML must not contain bare LF line endings');
assert.doesNotMatch(html, /[â€™â€“â€¦Â]/, 'HTML contains common UTF-8 mojibake');
assert.match(html, /<meta charset="utf-8">/i);
const securitySection = html.match(/<section><h2>Security and bonds<\/h2>([\s\S]*?)<\/section>/);
assert.ok(securitySection, 'Expected a Security and bonds section');
assert.match(securitySection[1], /<h3>Pairing<\/h3>[\s\S]*?<div id="pairing"/);
assert.doesNotMatch(html, /<section><h2>Pairing<\/h2>/, 'Pairing should be grouped with security and bonds');
assert.match(html, /<div id="extended"[^>]*>[\s\S]*?<\/div>/, 'Advertising instance controls should be in an uncollapsed subsection');
assert.match(html, /id="extendedHelp"/);
assert.match(html, /id="extendedControls"[^>]*\shidden(?:\s|>)/);
assert.doesNotMatch(html, /<details id="extended"/, 'Advertising instance controls should not be hidden in a disclosure');

class FakeNode {
  constructor(document, tagName = 'div', nodeType = 1) {
    this.document = document;
    this.tagName = tagName.toUpperCase();
    this.nodeType = nodeType;
    this.dataset = {};
    this.attributes = {};
    this.children = [];
    this.parentNode = null;
    this.type = '';
    this.value = '';
    this.checked = false;
    this.disabled = false;
    this.hidden = false;
    this.open = false;
    this.className = '';
    this.classList = {
      toggle: (name, force) => {
        const names = new Set(this.className.split(/\s+/).filter(Boolean));
        const enabled = force === undefined ? !names.has(name) : !!force;
        if (enabled) names.add(name); else names.delete(name);
        this.className = Array.from(names).join(' ');
        return enabled;
      }
    };
    this._id = '';
    this._textContent = '';
    this.modalOpenCount = 0;
  }

  set id(value) {
    this._id = value;
    if (value) this.document.nodesById.set(value, this);
  }

  get id() {
    return this._id;
  }

  get childNodes() {
    return this.children;
  }

  get textContent() {
    return this._textContent + this.children.map(child => child.textContent).join('');
  }

  set textContent(value) {
    this._textContent = String(value);
    for (const child of this.children) child.parentNode = null;
    this.children = [];
  }

  append(...nodes) {
    for (const node of nodes) {
      const child = typeof node === 'string' ? this.document.createTextNode(node) : node;
      child.parentNode = this;
      this.children.push(child);
    }
  }

  replaceChildren(...nodes) {
    for (const child of this.children) child.parentNode = null;
    this.children = [];
    this._textContent = '';
    this.append(...nodes);
  }

  remove() {
    if (!this.parentNode) return;
    this.parentNode.children = this.parentNode.children.filter(child => child !== this);
    this.parentNode = null;
  }

  setAttribute(name, value) {
    this.attributes[name] = String(value);
  }

  removeAttribute(name) {
    delete this.attributes[name];
  }

  checkValidity() {
    return true;
  }

  focus() {
    this.document.activeElement = this;
  }

  showModal() {
    this.open = true;
    this.modalOpenCount += 1;
  }

  close() {
    this.open = false;
  }

  querySelector(selector) {
    const stack = [...this.children];
    while (stack.length) {
      const node = stack.shift();
      if (selector.startsWith('[data-')) {
        const key = selector.slice(6, -1).replace(/-([a-z])/g, (_, c) => c.toUpperCase());
        if (Object.prototype.hasOwnProperty.call(node.dataset, key)) return node;
      } else if (node.tagName === selector.toUpperCase()) return node;
      stack.unshift(...node.children);
    }
    return null;
  }
}

class FakeDocument {
  constructor(source) {
    this.nodesById = new Map();
    this.nodes = new Set();
    this.activeElement = null;
    const idPattern = /<([a-z][a-z0-9-]*)\b([^>]*\bid="([^"]+)"[^>]*)>/gi;
    for (const match of source.matchAll(idPattern)) {
      const node = this.createElement(match[1]);
      node.id = match[3];
      const type = match[2].match(/\btype="([^"]+)"/i);
      if (type) node.type = type[1];
    }
  }

  createElement(tagName) {
    const node = new FakeNode(this, tagName);
    this.nodes.add(node);
    return node;
  }

  createTextNode(text) {
    const node = new FakeNode(this, '#text', 3);
    node.textContent = text;
    this.nodes.add(node);
    return node;
  }

  getElementById(id) {
    return this.nodesById.get(id) || null;
  }

  querySelectorAll(selector) {
    if (selector !== '[data-worker-action]') return [];
    return Array.from(this.nodes).filter(node => Object.prototype.hasOwnProperty.call(node.dataset, 'workerAction'));
  }
}

async function main() {
  const scriptMatch = html.match(/<script>\s*([\s\S]*?)<\/script>/i);
  assert.ok(scriptMatch, 'Expected one inline application script');
  const document = new FakeDocument(html);
  const context = {
    document,
    Node: { TEXT_NODE: 3 },
    fetch: () => new Promise(() => {}),
    setTimeout: () => 0,
    confirm: () => true,
    console,
    Map,
    Set,
    Array,
    Number,
    String,
    Boolean,
    Object,
    JSON,
    Math,
    Error,
    Promise
  };
  context.window = context;
  vm.createContext(context);
  vm.runInContext(scriptMatch[1], context, { filename: 'web/index.html' });

  const status = overrides => ({
    device: {
      name: 'WROVER', address: 'AA:BB:CC:DD:EE:FF', firmwareRevision: 42,
      firmwareSource: '0123456789abcdef'.repeat(4), firmwareEnvironment: 'wrover'
    },
    network: { ip: '192.0.2.4', hostname: 'ble-unit.local' },
    preferredMtu: 247,
    advertising: [{
      instanceId: 0,
      name: 'BLE Peripheral',
      active: true,
      mode: 0,
      discoverability: 2,
      intervalMs: 100,
      restartAfterConnection: false,
      directedTarget: { address: '11:22:33:44:55:66', addressType: 0 }
    }],
    security: { capabilities: 3, bond: true, mitm: false, secureConnections: true, ltk: true, irk: true, csrk: false },
    bonds: [{ address: '11:22:33:44:55:66', addressType: 0 }],
    connections: [{
      connectionId: 'connection-1', handle: 1, address: '11:22:33:44:55:66', intervalMs: 30,
      timeoutMs: 4000, latency: 0, mtu: 247, bonded: true, encrypted: true, authenticated: true, keySize: 16
    }],
    capabilities: { extendedAdvertising: false, maxAdvertisingInstances: 1, phyUpdate: false, supportedModes: [{ value: 0, label: 'Connectable' }] },
    csrfToken: 'token-1',
    lastOperation: null,
    resources: {},
    ...overrides
  });

  context.testStatus = status();
  vm.runInContext('renderStatus(testStatus)', context);
  const get = id => document.getElementById(id);
  assert.equal(get('mode').value, '0');
  assert.equal(get('interval').value, '100');
  assert.equal(get('restartAction').attributes['aria-pressed'], 'false');
  assert.equal(get('restartAction').textContent, 'Restart after connection: Disabled');
  assert.match(get('restartAction').className, /inactive-state/, 'The button should style the confirmed disabled state');
  assert.equal(get('restartAction').disabled, false);
  assert.equal(get('io').value, '3');
  assert.equal(get('mtu').value, '247');
  assert.equal(get('firmwareIdentity').textContent, 'Firmware r42 · wrover · source 0123456789ab');
  context.testStatus = status({ device: { name: 'Old server', address: 'AA:BB:CC:DD:EE:FF' } });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('firmwareIdentity').textContent, 'Firmware version unavailable', 'Older servers without source identity use the fallback label');
  context.testStatus = status();
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('flag-bond').checked, true);
  assert.equal(get('start').textContent, 'Started', 'Selected active advertising should be indicated on the start button');
  assert.equal(get('start').disabled, true);
  assert.equal(get('stop').textContent, 'Stop selected');
  assert.equal(get('stop').disabled, false);
  assert.equal(get('requestPhy').disabled, true, 'WROVER capability should disable PHY update');
  assert.match(get('phyHelp').textContent, /not supported/i);
  assert.equal(get('extended').hidden, false, 'Unsupported-device guidance should remain visible');
  assert.equal(get('extendedControls').hidden, true, 'WROVER should hide unsupported advertising controls');
  assert.match(get('extendedHelp').textContent, /cannot be added/i, 'WROVER should explain that additional instances are unavailable');
  assert.equal(get('add').disabled, true, 'Unsupported devices must not add advertising instances');
  assert.equal(get('remove').disabled, true, 'Unsupported devices must not remove advertising instances');

  document.activeElement = get('restartAction');
  context.testStatus = status({ advertising: [{ ...context.testStatus.advertising[0], restartAfterConnection: true }] });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('restartAction').attributes['aria-pressed'], 'true', 'A focused restart action must follow an external reported change');
  assert.equal(get('restartAction').textContent, 'Restart after connection: Enabled');
  assert.match(get('restartAction').className, /current-state/, 'The button should style the confirmed enabled state');
  context.testStatus = status({ advertising: [{ ...context.testStatus.advertising[0], restartAfterConnection: undefined }] });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('restartAction').textContent, 'Restart after connection: Unknown');
  assert.equal(get('restartAction').disabled, true, 'An unknown restart policy must not offer a guessed action');
  context.testStatus = status({ advertising: [] });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('restartAction').textContent, 'Restart after connection: No instance selected');
  assert.equal(get('restartAction').disabled, true, 'A restart policy cannot be changed without a selected instance');
  document.activeElement = null;

  const c3Advertising = [
    { instanceId: 0, name: 'BLE Peripheral', active: false, mode: 0, discoverability: 1, intervalMs: 100, restartAfterConnection: false, directedTarget: null },
    { instanceId: 1, name: 'Second', active: false, mode: 0, discoverability: 1, intervalMs: 100, restartAfterConnection: false, directedTarget: null }
  ];
  context.testStatus = status({
    device: { name: 'ESP32-C3', address: 'AA:BB:CC:DD:EE:FF' },
    advertising: c3Advertising,
    capabilities: { extendedAdvertising: true, maxAdvertisingInstances: 4, phyUpdate: false, supportedModes: [{ value: 0, label: 'Connectable' }] }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('extended').hidden, false, 'C3 instance controls should remain visible');
  assert.equal(get('extendedControls').hidden, false, 'C3 should show supported advertising controls');
  assert.equal(get('start').textContent, 'Start selected');
  assert.equal(get('start').disabled, false);
  assert.equal(get('stop').textContent, 'Stopped', 'Selected inactive advertising should be indicated on the stop button');
  assert.equal(get('stop').disabled, true);
  context.testStatus.advertising = c3Advertising.map(item => ({ ...item, active: item.instanceId === 1, restartAfterConnection: item.instanceId === 1 }));
  vm.runInContext('renderStatus(testStatus)', context);
  get('adv').value = '1';
  get('adv').onchange();
  assert.equal(get('start').textContent, 'Started', 'Button state should follow a newly selected active instance');
  assert.equal(get('restartAction').attributes['aria-pressed'], 'true', 'Restart policy should follow the selected instance');
  assert.equal(get('restartAction').textContent, 'Restart after connection: Enabled');
  get('adv').value = '0';
  get('adv').onchange();
  assert.equal(get('stop').textContent, 'Stopped', 'Button state should follow a newly selected inactive instance');
  assert.equal(get('restartAction').attributes['aria-pressed'], 'false', 'Switching instances must show that instance reported policy');
  assert.equal(get('restartAction').textContent, 'Restart after connection: Disabled');
  context.fetch = async (_url, options) => {
    context.restartRequest = JSON.parse(options.body);
    return { ok: false, status: 409, json: async () => ({ result: 'Busy', detail: 'Another BLE operation is still active.' }) };
  };
  await assert.rejects(vm.runInContext('toggleRestartPolicy()', context), /Another BLE operation is still active/);
  assert.equal(context.restartRequest.instanceId, 0, 'The restart action must capture the selected advertising instance');
  assert.equal(context.restartRequest.enabled, true, 'The restart action must request the opposite reported state');
  assert.equal(get('restartAction').attributes['aria-pressed'], 'false', 'A rejected request must retain the device-reported state');
  assert.equal(get('restartAction').attributes['aria-busy'], 'false', 'A rejected request must clear the Applying indicator');
  assert.equal(get('errorDialog').open, true, 'A rejected restart request should use the existing error feedback');
  get('errorDialog').close();

  let resolveRestartResponse;
  context.fetch = (_url, options) => {
    context.restartRequest = JSON.parse(options.body);
    return new Promise(resolve => { resolveRestartResponse = () => resolve({ ok: true, status: 202, json: async () => ({ result: 'Queued', operationId: 'op-restart-1' }) }); });
  };
  const pendingRestartToggle = vm.runInContext('toggleRestartPolicy()', context);
  assert.equal(get('restartAction').attributes['aria-pressed'], 'false', 'A pending request must not optimistically change confirmed state');
  assert.equal(get('restartAction').attributes['aria-busy'], 'true', 'A submitted restart request should show Applying while awaiting the device');
  assert.match(get('restartAction').textContent, /Disabled \(applying\.\.\.\)/, 'Applying feedback belongs inside the same button without claiming the desired state');
  assert.equal(get('restartAction').disabled, true, 'The restart action should be disabled while the worker is busy');
  get('adv').value = '1';
  get('adv').onchange();
  assert.equal(context.restartRequest.instanceId, 0, 'Changing selection while the request is pending must not change its captured instance');
  assert.equal(context.restartRequest.enabled, true, 'Changing selection while the request is pending must not change its captured desired state');
  assert.equal(get('restartAction').attributes['aria-pressed'], 'true', 'A selection change should show the newly selected instance readback');
  assert.equal(get('restartAction').attributes['aria-busy'], 'false', 'Applying should remain scoped to the instance whose operation is pending');
  resolveRestartResponse();
  await pendingRestartToggle;
  get('adv').value = '0';
  get('adv').onchange();
  assert.equal(get('restartAction').attributes['aria-pressed'], 'false', 'Queued work must leave confirmed state unchanged until status reports it');
  assert.equal(get('restartAction').attributes['aria-busy'], 'true', 'Queued work should remain visibly Applying');
  assert.equal(get('restartAction').disabled, true, 'The action remains disabled until queued work completes');
  context.testStatus = status({
    advertising: context.testStatus.advertising,
    capabilities: context.testStatus.capabilities,
    lastOperation: { operationId: 'op-restart-1', operation: 'setRestartAfterConnection', target: 'instance:0', state: 'Running', result: null, detail: 'Running.' }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('restartAction').attributes['aria-pressed'], 'false', 'Running work must not replace the confirmed readback');
  assert.equal(get('restartAction').attributes['aria-busy'], 'true', 'Running restart work should stay visibly Applying');
  context.testStatus = status({
    advertising: context.testStatus.advertising.map(item => item.instanceId === 0 ? { ...item, restartAfterConnection: true } : item),
    capabilities: context.testStatus.capabilities,
    lastOperation: { operationId: 'op-restart-1', operation: 'setRestartAfterConnection', target: 'instance:0', state: 'Done', result: 'Applied', detail: 'Restart policy updated.' }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('restartAction').attributes['aria-pressed'], 'true', 'The confirmed state should change only after status reports it');
  assert.equal(get('restartAction').attributes['aria-busy'], 'false', 'The Applying indicator should clear when status reports completion');
  assert.equal(get('restartAction').textContent, 'Restart after connection: Enabled');
  assert.equal(get('restartAction').disabled, false);
  context.fetch = async (_url, options) => {
    context.restartRequest = JSON.parse(options.body);
    return { ok: false, status: 409, json: async () => ({ result: 'Busy', detail: 'Another BLE operation is still active.' }) };
  };
  await assert.rejects(vm.runInContext('toggleRestartPolicy()', context), /Another BLE operation is still active/);
  assert.equal(context.restartRequest.enabled, false, 'An enabled readback must request Disable');
  assert.equal(get('restartAction').attributes['aria-pressed'], 'true', 'A rejected disable must preserve the enabled readback');
  assert.equal(get('restartAction').attributes['aria-busy'], 'false');
  get('errorDialog').close();
  get('newId').value = '2';
  get('newId').oninput();
  assert.equal(get('add').disabled, false, 'C3 should allow a free ID while capacity remains');
  get('newId').value = '1';
  get('newId').oninput();
  assert.equal(get('add').disabled, true, 'An existing advertising instance ID cannot be added again');
  get('newId').value = '4';
  get('newId').oninput();
  assert.equal(get('add').disabled, true, 'New instance IDs must be below the reported capacity');
  get('adv').value = '1';
  get('adv').onchange();
  assert.equal(get('remove').disabled, false, 'A nonzero selected instance can be removed when another remains');
  get('adv').value = '0';
  get('adv').onchange();
  assert.equal(get('remove').disabled, true, 'The backend-protected instance 0 cannot be removed');
  context.testStatus = status({
    advertising: c3Advertising,
    capabilities: { extendedAdvertising: true, maxAdvertisingInstances: 2, phyUpdate: false, supportedModes: [{ value: 0, label: 'Connectable' }] }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  get('newId').value = '2';
  get('newId').oninput();
  assert.equal(get('add').disabled, true, 'Adding must be disabled when the device capacity is full');
  context.testStatus = status({
    advertising: [c3Advertising[0]],
    capabilities: { extendedAdvertising: true, maxAdvertisingInstances: 1, phyUpdate: false, supportedModes: [{ value: 0, label: 'Connectable' }] }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('extendedControls').hidden, true, 'Capacity one has no additional-instance controls');
  assert.match(get('extendedHelp').textContent, /Configured capacity allows no additional advertising instances/);

  const originalModeOption = get('mode').childNodes[0];
  context.testStatus = status({
    advertising: [{
      instanceId: 0, name: 'BLE Peripheral', active: false, mode: 0, discoverability: 1,
      intervalMs: 250, restartAfterConnection: true, directedTarget: null
    }],
    security: { capabilities: 4, bond: false, mitm: true, secureConnections: true, ltk: true, irk: false, csrk: true },
    preferredMtu: 517
  });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('mode').childNodes[0], originalModeOption, 'Stable options should not be recreated on each poll');
  assert.equal(get('discoverability').value, '1', 'Discoverability should follow external changes');
  assert.equal(get('interval').value, '250', 'Interval should follow external changes');
  assert.equal(get('restartAction').attributes['aria-pressed'], 'true', 'Restart policy should follow external changes');
  assert.equal(get('target').value, '', 'Directed target should follow external changes');
  assert.equal(get('io').value, '4', 'I/O capability should follow external changes');
  assert.equal(get('mtu').value, '517', 'Preferred MTU should follow external changes');
  assert.equal(get('flag-bond').checked, false, 'Security flags should follow external changes');
  assert.match(get('peerInfo').textContent, /"intervalMs": 30/, 'Peer details should retain actual negotiated values');

  get('minInterval').value = '17';
  get('maxInterval').value = '42';
  get('latency').value = '2';
  get('timeout').value = '6000';
  document.activeElement = get('interval');
  get('interval').value = '333';
  context.testStatus = status({ advertising: [{ ...context.testStatus.advertising[0], intervalMs: 500 }] });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('interval').value, '333', 'A focused field should keep its local edit');
  document.activeElement = null;
  get('interval').oninput();
  context.testStatus = status({ advertising: [{ ...context.testStatus.advertising[0], intervalMs: 600 }] });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('interval').value, '333', 'A dirty field should remain stable after focus moves');
  for (const id of ['minInterval', 'maxInterval', 'latency', 'timeout']) assert.ok(get(id).value, id + ' request input should not be reset by status polling');
  get('interval').dataset.dirty = 'false';
  context.testStatus = status({ advertising: [{ ...context.testStatus.advertising[0], intervalMs: 700 }] });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('interval').value, '700', 'A clean field should resume device-state synchronization');

  context.testStatus = status({
    security: { capabilities: 3, bond: true, mitm: false, secureConnections: true, ltk: true, irk: true, csrk: false }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  vm.runInContext('renderPairing({ requests: [] })', context);
  assert.match(get('pairingCapability').textContent, /No input or output/);
  assert.match(get('pairingCapability').textContent, /Pairing may fail/);
  context.testPairing = { requests: [{ requestId: 'pair-1', type: 'PasskeyEntry', connectionId: 'connection-1', expiresInMs: 24000 }] };
  vm.runInContext('renderPairing(testPairing)', context);
  assert.ok(get('pairing').querySelector('form'), 'An active passkey request keeps its response controls if I/O mode changes');
  assert.match(get('pairing').textContent, /This active request needs a passkey/);
  vm.runInContext('renderPairing({ requests: [] })', context);

  context.testStatus = status({
    connections: [],
    capabilities: { extendedAdvertising: false, maxAdvertisingInstances: 1, phyUpdate: true, supportedModes: [{ value: 0, label: 'Connectable' }] }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('requestPhy').disabled, true, 'PHY update must remain disabled without a selected peer');
  assert.match(get('phyHelp').textContent, /Select a connected peer/);

  const failedOperation = {
    operationId: 'op-failed-1', operation: 'setDirectedTarget', target: 'instance:2', state: 'Done', result: 'Failed',
    detail: 'Stop advertising on this instance before changing its directed target.',
    error: { code: 'advertising_active', instanceId: 2 }
  };
  context.testOperation = failedOperation;
  vm.runInContext('observeWorker(testOperation)', context);
  assert.equal(get('errorDialog').open, true, 'A terminal worker failure should open the error dialog');
  assert.equal(get('stopAdvertisingForError').hidden, false, 'Directed target active failure should offer explicit stop');
  assert.equal(get('errorDialog').dataset.stopInstanceId, '2', 'Recovery must use the reported instance ID');
  assert.match(get('errorContext').textContent, /assign directed target/i);
  const failureDialogCount = get('errorDialog').modalOpenCount;
  get('errorDialog').close();
  vm.runInContext('observeWorker(testOperation)', context);
  assert.equal(get('errorDialog').open, false, 'The same terminal failure must not reopen after dismissal');
  assert.equal(get('errorDialog').modalOpenCount, failureDialogCount, 'A worker failure is surfaced once per operation ID');

  context.testOperation = {
    operationId: 'op-applied-1', operation: 'startAll', target: 'all', state: 'Done', result: 'Applied', detail: 'Started 1 advertising instance.'
  };
  vm.runInContext('observeWorker(testOperation)', context);
  assert.match(get('feedback').textContent, /Start all advertising .*: Applied/);

  context.testStatus = status({
    lastOperation: { operationId: 'op-previous', operation: 'setInterval', target: 'instance:0', state: 'Done', result: 'Applied', detail: 'Previous interval update.' }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  get('interval').dataset.dirty = 'true';
  get('interval').dataset.editVersion = '1';
  get('interval').value = '720';
  context.fetch = async () => ({
    ok: true,
    status: 202,
    json: async () => ({ result: 'Queued', operationId: 'op-queued-1' })
  });
  const queuedPost = vm.runInContext("post('advertising','setInterval',{instanceId:0,intervalMs:720},['interval'])", context);
  assert.equal(get('start').disabled, true, 'Worker controls should disable as soon as a POST starts');
  await queuedPost;
  assert.equal(get('start').disabled, true, 'Worker controls should remain disabled after HTTP 202');
  context.testStatus = status({
    lastOperation: { operationId: 'op-previous', operation: 'setInterval', target: 'instance:0', state: 'Done', result: 'Applied', detail: 'Previous interval update.' }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.match(get('lastOperation').textContent, /Waiting for operation op-queued-1/);
  assert.equal(get('start').disabled, true, 'An older in-flight status snapshot must not release local worker busy state');
  context.testStatus = status({
    lastOperation: { operationId: 'op-queued-1', operation: 'setInterval', target: 'instance:0', state: 'Running', result: null, detail: 'Running.' }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  get('interval').value = '730';
  get('interval').oninput();
  context.testStatus = status({
    advertising: [{ ...context.testStatus.advertising[0], intervalMs: 700 }],
    lastOperation: { operationId: 'op-queued-1', operation: 'setInterval', target: 'instance:0', state: 'Done', result: 'Applied', detail: 'Interval updated.' }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('interval').value, '730', 'Completing an older operation must preserve a newer edit to the same field');
  assert.equal(get('interval').dataset.dirty, 'true');

  context.fetch = async () => ({
    ok: false,
    status: 409,
    json: async () => ({ result: 'Busy', detail: 'Another BLE operation is still active.' })
  });
  document.activeElement = null;
  get('interval').value = '888';
  get('interval').oninput();
  await assert.rejects(
    vm.runInContext("post('advertising','setInterval',{instanceId:0,intervalMs:888},['interval'])", context),
    /Another BLE operation is still active/
  );
  assert.equal(get('interval').value, '700', 'A rejected edit should return to the latest authoritative value');
  assert.equal(get('interval').dataset.dirty, 'false', 'A rejected edit should release its dirty marker');
  assert.equal(get('errorDialog').open, true, 'Structured HTTP errors should open the accessible error dialog');
  assert.match(get('errorContext').textContent, /Busy/);
  assert.match(get('errorDetail').textContent, /Another BLE operation is still active/);

  get('errorDialog').close();
  get('interval').value = '777';
  get('interval').dataset.dirty = 'true';
  get('interval').dataset.editVersion = '91';
  get('flag-mitm').checked = true;
  vm.runInContext("markDirty(['flag-mitm'])", context);
  document.activeElement = get('flag-mitm');
  await assert.rejects(
    vm.runInContext("post('security','setAuthentication',{flag:'mitm',enabled:true},['flag-mitm'])", context),
    /Another BLE operation is still active/
  );
  assert.equal(get('flag-mitm').checked, false, 'A focused rejected security checkbox should immediately return to the authoritative value');
  assert.equal(get('flag-mitm').dataset.dirty, 'false', 'A focused rejected checkbox should release its dirty marker');
  assert.equal(get('interval').value, '777', 'Releasing one submitted field must preserve a separate dirty field');
  assert.equal(get('interval').dataset.dirty, 'true');
  assert.equal(get('errorDialog').open, true);
  get('errorDialog').close();
  document.activeElement = null;
  get('interval').dataset.dirty = 'false';
  vm.runInContext('renderStatus(testStatus)', context);

  context.fetch = async () => ({ ok: true, status: 202, json: async () => ({ result: 'Queued', operationId: 'op-adv0' }) });
  context.testStatus = status({
    advertising: [
      { instanceId: 0, name: 'BLE Peripheral', active: false, mode: 0, discoverability: 1, intervalMs: 700, restartAfterConnection: true, directedTarget: null },
      { instanceId: 1, name: 'Second', active: false, mode: 0, discoverability: 2, intervalMs: 222, restartAfterConnection: false, directedTarget: null }
    ],
    lastOperation: { operationId: 'op-queued-1', operation: 'setInterval', target: 'instance:0', state: 'Done', result: 'Applied', detail: 'Interval updated.' }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  get('interval').value = '999';
  get('interval').oninput();
  await vm.runInContext("post('advertising','setInterval',{instanceId:0,intervalMs:999},['interval'])", context);
  get('adv').value = '1';
  get('adv').onchange();
  assert.equal(get('interval').value, '222', 'Switching advertising instances should discard the previous instance draft');
  context.testStatus = status({
    advertising: context.testStatus.advertising,
    lastOperation: { operationId: 'op-adv0', operation: 'setInterval', target: 'instance:0', state: 'Done', result: 'Applied', detail: 'Interval updated.' }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('interval').value, '222', 'An older instance completion must not overwrite the new instance value');

  get('errorDialog').close();
  vm.runInContext("showPollError('Status',apiError('busy',409,{result:'Busy',detail:'Snapshot busy'}))", context);
  assert.equal(get('errorDialog').open, false, 'Expected transient status Busy responses should not open an error modal');
  context.statusPollError = vm.runInContext("apiError('status allocation failed',500,{result:'Failed',detail:'Status unavailable'})", context);
  context.pairPollError = vm.runInContext("apiError('pairing allocation failed',500,{result:'Failed',detail:'Pairing unavailable'})", context);
  vm.runInContext("showPollError('Status',statusPollError)", context);
  assert.equal(get('errorDialog').open, true);
  get('errorDialog').close();
  vm.runInContext("showPollError('Pairing status',pairPollError)", context);
  assert.equal(get('errorDialog').open, true, 'Pairing errors should have a separate deduplication key');
  get('errorDialog').close();
  vm.runInContext("showPollError('Status',statusPollError)", context);
  assert.equal(get('errorDialog').open, false, 'A pairing refresh must not clear a persistent status error');

  context.fetch = async () => ({ ok: true, status: 202, json: async () => ({ result: 'Queued', operationId: 'op-skipped-terminal' }) });
  get('interval').value = '333';
  get('interval').oninput();
  await vm.runInContext("post('advertising','setInterval',{instanceId:1,intervalMs:333},['interval'])", context);
  assert.equal(get('start').disabled, true, 'A queued operation remains locally busy until observed or superseded');
  context.testStatus = status({
    advertising: [
      { instanceId: 0, name: 'BLE Peripheral', active: false, mode: 0, discoverability: 1, intervalMs: 700, restartAfterConnection: true, directedTarget: null },
      { instanceId: 1, name: 'Second', active: false, mode: 0, discoverability: 2, intervalMs: 444, restartAfterConnection: false, directedTarget: null }
    ],
    lastOperation: { operationId: 'op-external', operation: 'setInterval', target: 'instance:0', state: 'Done', result: 'Applied', detail: 'Another browser completed an operation.' }
  });
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('start').disabled, false, 'A different operation record should release the stale local worker lock');
  assert.equal(get('interval').value, '444', 'A superseded operation without an observed terminal should release its submitted edit to authoritative state');
  assert.equal(get('interval').dataset.dirty, 'false', 'The skipped terminal must clear the matching submitted dirty marker');
  assert.equal(vm.runInContext("submittedFields.has('op-skipped-terminal')", context), false, 'The skipped operation snapshot should be discarded');

  // GATT value publication, peer limits, source identity, and polling behavior.
  assert.doesNotMatch(html, /id="gattRestore"|gattRestore|gattBaseline|id="gattState"|restoreValue|baselineHex/,
    'Restore and baseline-only UI/API concepts should be removed');
  assert.doesNotMatch(html, /Use a shorter payload|request a larger ATT MTU|minimum required MTU/,
    'Publication-limit UI should report measurements without suggesting remedies');
  assert.match(html, /publicationDispatched flag mean NimBLE accepted or queued the request/);
  assert.match(html, /do not prove peer dispatch or client-application receipt/);
  const peer = (connectionId, address, handle, mtu) => ({
    connectionId, address, handle, mtu, intervalMs: 30, timeoutMs: 4000, latency: 0,
    bonded: false, encrypted: false, authenticated: false, keySize: 0
  });
  const peer23a = peer('peer-23a', '11:22:33:44:55:66', 1, 23);
  const peer23b = peer('peer-23b', '22:33:44:55:66:77', 2, 23);
  const peer517 = peer('peer-517', '33:44:55:66:77:88', 3, 517);
  const indicationBaseline = Buffer.from('Indicate Characteristic', 'ascii').toString('hex').toUpperCase();
  const indicationSpaced = indicationBaseline.match(/../g).join(' ');
  const gattSubscriptionDiagnostics = {
    currentConnectionCount: 3,
    maximumEntries: 12,
    subscriptionSemantics: 'Each value is the latest observed onSubscribe callback for a current connection. Absence means no event was observed; it does not mean Off. Disconnect clears observations.',
    publicationStatusSemantics: 'Asynchronous connection-scoped NimBLE status only: code 0 is notification TX status, BLE_HS_EDONE is indication acknowledgement, and other nonzero codes are errors. None proves client-application receipt.',
    lastObservedSubscriptions: [
      {connectionId: 'peer-23a', peerAddress: peer23a.address, characteristicUuid: '12345678-1234-5678-1234-56789abcdef5', cccdValue: '0000', observedAtUptimeMs: 1001, connectionFreshness: 'current'},
      {connectionId: 'peer-23b', peerAddress: peer23b.address, characteristicUuid: '12345678-1234-5678-1234-56789abcdef5', cccdValue: '0003', observedAtUptimeMs: 1002, connectionFreshness: 'current'}
    ],
    lastPublicationStatuses: [
      {connectionId: 'peer-23a', peerAddress: peer23a.address, characteristicUuid: '12345678-1234-5678-1234-56789abcdef5', statusCode: 0, statusKind: 'NotificationTx', observedAtUptimeMs: 1101, connectionFreshness: 'current'},
      {connectionId: 'peer-23b', peerAddress: peer23b.address, characteristicUuid: '12345678-1234-5678-1234-56789abcdef5', statusCode: 123, statusKind: 'IndicationAcknowledged', observedAtUptimeMs: 1102, connectionFreshness: 'current'},
      {connectionId: 'peer-517', peerAddress: peer517.address, characteristicUuid: '12345678-1234-5678-1234-56789abcdef5', statusCode: -99, statusKind: 'Error', observedAtUptimeMs: 1103, connectionFreshness: 'current'}
    ]
  };
  assert.equal(indicationBaseline.length / 2, 23);
  context.testStatus = status({connections: [peer23a, peer23b, peer517]});
  vm.runInContext('renderStatus(testStatus)', context);
  const temporaryServiceId = '0123456789abcdef-00000011';
  const dynamicServices = [
    {serviceId: '0000000000000000-00000000', serviceUuid: '12345678-1234-5678-1234-56789abcdef0', characteristicUuid: '12345678-1234-5678-1234-56789abcdef2', protectedService: true},
    {serviceId: temporaryServiceId, serviceUuid: '12345678-1234-5678-1234-56789abce100', characteristicUuid: '12345678-1234-5678-1234-56789abce101', protectedService: false}
  ];
  context.testGatt = {state: 'Ready', valuesAvailable: true, servicesAvailable: true, serviceLimit: 4, services: dynamicServices,
    subscriptionDiagnostics: gattSubscriptionDiagnostics, values: [
    { targetId: '0123456789abcdef-00000001', name: 'Original notification', serviceUuid: 'original', characteristicUuid: '12345678-1234-5678-1234-56789abcdef2', notify: true, indicate: false, currentHex: '0102', maxLength: 512, publicationLimit: 20, minimumPeerMtu: 23, connectedPeerCount: 3 },
    { targetId: '0123456789abcdef-00000002', name: 'Original indication', serviceUuid: 'original', characteristicUuid: '12345678-1234-5678-1234-56789abcdef3', notify: false, indicate: true, currentHex: indicationBaseline, maxLength: 512, publicationLimit: 20, minimumPeerMtu: 23, connectedPeerCount: 3 },
    { targetId: '0123456789abcdef-00000003', name: 'Temperature Scalar', serviceUuid: 'fixture', characteristicUuid: '12345678-1234-5678-1234-56789abcdf05', notify: true, indicate: false, currentHex: 'EB00', maxLength: 512, publicationLimit: 20, minimumPeerMtu: 23, connectedPeerCount: 3 },
    { targetId: '0123456789abcdef-00000004', name: 'Dual notification and indication', serviceUuid: 'original', characteristicUuid: '12345678-1234-5678-1234-56789abcdef5', notify: true, indicate: true, currentHex: '010203', maxLength: 512, publicationLimit: 20, minimumPeerMtu: 23, connectedPeerCount: 3 }
  ] };
  vm.runInContext('renderGatt(testGatt)', context);
  assert.equal(vm.runInContext('isSubscriptionDiagnostics(testGatt.subscriptionDiagnostics)', context), true,
    'A complete current diagnostics object is required and accepted');
  assert.equal(vm.runInContext('isGattSnapshotEnvelope(testGatt)', context), true,
    'The GATT response envelope requires subscription diagnostics');
  assert.match(get('gattSubscriptionDiagnostics').textContent, /Connection freshness: 3 current peer/);
  assert.match(get('gattSubscriptionDiagnostics').textContent, /CCCD 0000/,
    'An observed explicit Off callback remains distinguishable from no event');
  assert.match(get('gattSubscriptionDiagnostics').textContent, /CCCD 0003/);
  assert.match(get('gattSubscriptionDiagnostics').textContent, /Notification TX status/);
  assert.match(get('gattSubscriptionDiagnostics').textContent, /Indication acknowledgement/);
  assert.match(get('gattSubscriptionDiagnostics').textContent, /NimBLE publication error/);
  assert.match(get('gattSubscriptionDiagnostics').textContent, /does not mean Off/);
  assert.match(get('gattSubscriptionDiagnostics').textContent, /does not prove client-application receipt/);
  const missingDiagnosticsEnvelope = {...context.testGatt};
  delete missingDiagnosticsEnvelope.subscriptionDiagnostics;
  context.missingDiagnosticsEnvelope = missingDiagnosticsEnvelope;
  assert.equal(vm.runInContext('isGattSnapshotEnvelope(missingDiagnosticsEnvelope)', context), false,
    'A GATT response without subscriptionDiagnostics is rejected');
  context.unknownSubscriptionDiagnostics = {...gattSubscriptionDiagnostics,
    lastObservedSubscriptions: [], lastPublicationStatuses: []};
  assert.equal(vm.runInContext('isSubscriptionDiagnostics(unknownSubscriptionDiagnostics)', context), true);
  vm.runInContext('renderGattSubscriptionDiagnostics(unknownSubscriptionDiagnostics)', context);
  assert.match(get('gattSubscriptionDiagnostics').textContent, /no event was observed/);
  assert.match(get('gattSubscriptionDiagnostics').textContent, /unknown; it does not mean Off/);
  assert.doesNotMatch(get('gattSubscriptionDiagnostics').textContent, /CCCD 0000/,
    'No callback remains unknown and is not rendered as Off');
  vm.runInContext('renderGattSubscriptionDiagnostics(testGatt.subscriptionDiagnostics)', context);
  context.invalidSubscriptionDiagnostics = {...gattSubscriptionDiagnostics,
    lastObservedSubscriptions: [{...gattSubscriptionDiagnostics.lastObservedSubscriptions[0], connectionFreshness: 'stale'}]};
  assert.equal(vm.runInContext('isSubscriptionDiagnostics(invalidSubscriptionDiagnostics)', context), false,
    'Disconnected observations cannot be presented as current');
  context.invalidSubscriptionDiagnostics = {...gattSubscriptionDiagnostics,
    lastObservedSubscriptions: [{...gattSubscriptionDiagnostics.lastObservedSubscriptions[0], cccdValue: '0004'}]};
  assert.equal(vm.runInContext('isSubscriptionDiagnostics(invalidSubscriptionDiagnostics)', context), false,
    'Only observed CCCD values 0000 through 0003 are accepted');
  context.invalidSubscriptionDiagnostics = {...gattSubscriptionDiagnostics,
    lastPublicationStatuses: [{...gattSubscriptionDiagnostics.lastPublicationStatuses[0], statusKind: 'Delivered'}]};
  assert.equal(vm.runInContext('isSubscriptionDiagnostics(invalidSubscriptionDiagnostics)', context), false,
    'Publication status cannot claim client delivery');
  assert.equal(get('gattHex').value, '01 02');
  assert.equal(get('gattCurrent').textContent, '01 02');
  assert.equal(get('gattRestore'), null);
  assert.equal(get('gattBaseline'), null);
  assert.equal(get('gattState'), null);
  get('gattHex').value = 'AA BB'; get('gattHex').oninput();
  context.testGatt.values[0].currentHex = '0908';
  vm.runInContext('renderGatt(testGatt)', context);
  assert.equal(get('gattHex').value, 'AA BB', 'Polling must preserve an in-progress GATT draft');
  assert.equal(get('gattCurrent').textContent, '09 08', 'Observed bytes update independently from the draft');
  vm.runInContext('renderGatt(null)', context);
  assert.equal(get('gattPublish').disabled, true);
  assert.equal(get('gattIdentity').textContent.includes('0123456789abcdef-00000001'), true, 'A transient failure preserves the last target identity');
  assert.equal(get('gattCurrent').textContent, '09 08', 'A transient failure preserves the last value and marks it stale in the snapshot banner');
  assert.match(get('gattSnapshotStatus').textContent, /Values: stale or unavailable/);
  assert.equal(get('gattHex').value, 'AA BB', 'A transient snapshot failure must preserve edits');
  vm.runInContext("renderGatt({state:'Ready',valuesAvailable:false,values:null,servicesAvailable:false,services:null,serviceLimit:4,reason:'GATT host work is busy'})", context);
  assert.equal(vm.runInContext('gattValues.length', context), 4, 'Null values preserve all four prior publication targets in memory');
  assert.match(get('gattInventoryStatus').textContent, /Stale service inventory/);
  assert.equal(get('gattServiceTable').children.length, 1, 'Null inventory keeps the prior rows visibly stale');
  assert.equal(get('gattServiceUuid').value, '', 'Snapshot polling does not overwrite unfinished service input');
  vm.runInContext('renderGatt(testGatt)', context);
  assert.equal(get('gattHex').value, 'AA BB', 'Recovery must also preserve the draft');

  const gattPosts = [];
  let gattOperationSequence = 0;
  context.fetch = async (url, request) => {
    gattPosts.push({url, body: JSON.parse(request.body)});
    return { ok: true, status: 202, json: async () => ({ result: 'Queued', operationId: 'gatt-op-' + (++gattOperationSequence) }) };
  };
  get('gattScenario').value = '0'; get('gattScenario').onchange();
  assert.equal(get('gattPrefill').disabled, true);
  assert.match(get('gattScenarioInfo').textContent, /Temperature Update Trigger/);
  get('gattScenario').value = '5'; get('gattScenario').onchange(); get('gattPrefill').onclick();
  assert.equal(get('gattTarget').value, '0123456789abcdef-00000003');
  assert.equal(get('gattHex').value, 'F5 00');
  assert.equal(gattPosts.length, 0, 'Scenario prefill must never send or mutate firmware');
  assert.equal(vm.runInContext("parseGattHex(' 00 eb\\tF5 00 ')", context), '00EBF500');
  assert.equal(vm.runInContext("parseGattHex('')", context), '');
  for (const invalid of ['0', '0 1', '0x01', '00,FF', '0G', 'AA'.repeat(257)]) {
    context.invalidGattHex = invalid;
    assert.throws(() => vm.runInContext('parseGattHex(invalidGattHex)', context));
  }
  assert.equal(vm.runInContext("parseGattHex('AA'.repeat(256)).length", context), 512);
  get('gattTarget').value = '0123456789abcdef-00000002'; get('gattTarget').onchange();
  assert.equal(get('gattPublish').textContent, 'Send indication');
  get('gattHex').value = indicationSpaced; get('gattHex').oninput();
  assert.equal(get('gattPublish').disabled, true, 'A 23-byte payload is disabled at the 20-byte peer limit');
  assert.match(get('gattDraftStatus').textContent, /Payload: 23 bytes exceeds the current publication limit/);
  assert.match(get('gattDraftStatus').textContent, /Maximum single update: 20 bytes \(3 too many\)/);
  assert.match(get('gattDraftStatus').textContent, /Limiting peer\(s\): 11:22:33:44:55:66 \(handle 1, ATT MTU 23\); 22:33:44:55:66:77 \(handle 2, ATT MTU 23\)/);
  assert.match(get('gattDraftStatus').textContent, /ATT MTU - 3 gives 20 bytes/);
  assert.match(get('gattDraftStatus').textContent, /including unsubscribed peers/);
  get('gattPublish').onclick();
  assert.equal(get('errorDialog').open, true, 'A defensive oversized click displays an explanation');
  assert.match(get('errorDetail').textContent, /Cannot send 23 bytes: current publication limit is 20 bytes; the maximum single update is 20 bytes/);
  assert.match(get('errorDetail').textContent, /ATT MTU 23/);
  assert.match(get('errorDetail').textContent, /ATT MTU - 3 gives 20 bytes/);
  assert.doesNotMatch(get('errorDetail').textContent, /Use a shorter payload|request a larger ATT MTU|at least 26/);
  assert.equal(gattPosts.length, 0, 'Oversized payloads fail before HTTP');
  get('errorDialog').close();

  // An MTU-517 peer leaves the Web cap (256), not the transport, as the limiter.
  context.testStatus = status({connections: [peer517]});
  vm.runInContext('renderStatus(testStatus)', context);
  context.testGatt.values[1].publicationLimit = 256;
  context.testGatt.values[1].minimumPeerMtu = 517;
  context.testGatt.values[1].connectedPeerCount = 1;
  vm.runInContext('renderGatt(testGatt)', context);
  assert.equal(get('gattHex').value, indicationSpaced, 'A polled limit increase preserves the edited payload');
  assert.equal(get('gattPublish').disabled, false, 'The same 23-byte value fits a 256-byte publication limit');
  assert.match(get('gattLimit').textContent, /Maximum single update: 256 bytes \(stored maximum 512 bytes; Web cap 256 bytes\)/);
  assert.match(get('gattLimit').textContent, /Web payload cap is the binding limit/);
  assert.doesNotMatch(get('gattLimit').textContent, /Limiting peer/);
  await vm.runInContext('publishGatt()', context);
  assert.deepEqual(gattPosts[0], {
    url: '/api/gatt', body: {op: 'publishValue', targetId: '0123456789abcdef-00000002', hex: indicationBaseline}
  });
  assert.equal(get('gattPublish').disabled, true, 'A queued publish uses the existing worker lock');
  assert.match(get('feedback').textContent, /Queued:.*operation gatt-op-1/,
    'The HTTP response reports worker queueing before any operation completion');
  assert.doesNotMatch(get('feedback').textContent, /Dispatched|delivered|receipt/i,
    'A queued response does not claim publication or client receipt');

  context.testStatus = status({connections: [peer517], lastOperation: {
    operationId: 'gatt-op-1', operation: 'publishValue', target: 'gatt', state: 'Done', result: 'Dispatched',
    detail: 'Value stored; NimBLE accepted/queued the characteristic update request. Peer dispatch and client-application receipt are not proven.',
    data: {storageVerified: true, publicationAttempted: true, publicationDispatched: true}
  }});
  vm.runInContext('renderStatus(testStatus)', context);
  assert.match(get('lastOperation').textContent, /NimBLE accepted\/queued the characteristic update request/);
  assert.match(get('lastOperation').textContent, /Peer dispatch and client-application receipt are not proven/);
  context.testGatt.values[1].currentHex = indicationBaseline;
  vm.runInContext('renderGatt(testGatt)', context);
  assert.equal(get('gattHex').value, indicationSpaced);

  // Each local cap is explained separately from the negotiated peer limit.
  context.testGatt.values[1].maxLength = 512;
  context.testGatt.values[1].publicationLimit = 256;
  context.testGatt.values[1].minimumPeerMtu = 517;
  context.testGatt.values[1].connectedPeerCount = 1;
  vm.runInContext('renderGatt(testGatt)', context);
  get('gattHex').value = 'AA'.repeat(257); get('gattHex').oninput();
  assert.equal(get('gattPublish').disabled, true);
  assert.match(get('gattDraftStatus').textContent, /257 bytes exceeds the current publication limit/);
  assert.match(get('gattDraftStatus').textContent, /Maximum single update: 256 bytes \(1 too many\)/);
  assert.match(get('gattDraftStatus').textContent, /Web payload cap is the binding limit/);
  get('gattPublish').onclick();
  assert.equal(get('errorDialog').open, true, 'A defensive click shows the binding cap');
  assert.match(get('errorDetail').textContent, /Cannot send 257 bytes: current publication limit is 256 bytes; the maximum single update is 256 bytes/);
  assert.match(get('errorDetail').textContent, /Web payload cap is 256 bytes/);
  get('errorDialog').close();
  get('gattHex').value = 'AA'.repeat(513); get('gattHex').oninput();
  assert.match(get('gattDraftStatus').textContent, /Payload: 513 bytes exceeds/);
  assert.match(get('gattDraftStatus').textContent, /Maximum single update: 256 bytes \(257 too many\)/);
  assert.match(get('gattDraftStatus').textContent, /Web cap/);
  context.testGatt.values[1].maxLength = 16;
  context.testGatt.values[1].publicationLimit = 16;
  context.testGatt.values[1].minimumPeerMtu = 517;
  context.testGatt.values[1].connectedPeerCount = 1;
  vm.runInContext('renderGatt(testGatt)', context);
  get('gattHex').value = 'AA'.repeat(17); get('gattHex').oninput();
  assert.match(get('gattDraftStatus').textContent, /Maximum single update: 16 bytes \(1 too many\)/);
  assert.match(get('gattDraftStatus').textContent, /stored maximum is the binding limit/);

  // Connection and GATT snapshots can race; avoid attributing the limit to a peer.
  context.testStatus = status({connections: [peer23a]});
  vm.runInContext('renderStatus(testStatus)', context);
  context.testGatt.values[1].maxLength = 512;
  context.testGatt.values[1].publicationLimit = 256;
  context.testGatt.values[1].minimumPeerMtu = 517;
  context.testGatt.values[1].connectedPeerCount = 1;
  vm.runInContext('renderGatt(testGatt)', context);
  assert.match(get('gattLimit').textContent, /Connection details are refreshing/);
  assert.doesNotMatch(get('gattLimit').textContent, /Limiting peer/);
  get('gattHex').value = 'AA'.repeat(257); get('gattHex').oninput();
  assert.match(get('gattDraftStatus').textContent, /Payload: 257 bytes exceeds/);
  assert.match(get('gattDraftStatus').textContent, /publication limit: 256 bytes/);
  assert.match(get('gattDraftStatus').textContent, /Connection details are refreshing/);

  // A GATT snapshot can name the limiting MTU while the status peer list is stale.
  context.testStatus = status({connections: [peer517]});
  vm.runInContext('renderStatus(testStatus)', context);
  context.testGatt.values[1].publicationLimit = 20;
  context.testGatt.values[1].minimumPeerMtu = 23;
  context.testGatt.values[1].connectedPeerCount = 1;
  vm.runInContext('renderGatt(testGatt)', context);
  assert.match(get('gattLimit').textContent, /GATT snapshot minimum connected-peer ATT MTU is 23/);
  assert.match(get('gattLimit').textContent, /Connection details are refreshing/);
  assert.doesNotMatch(get('gattLimit').textContent, /Limiting peer\(s\):/);
  assert.doesNotMatch(get('gattLimit').textContent, /33:44:55:66:77:88/);
  get('gattHex').value = indicationSpaced; get('gattHex').oninput();
  get('gattPublish').onclick();
  assert.equal(get('errorDialog').open, true);
  assert.match(get('errorDetail').textContent, /GATT snapshot minimum connected-peer ATT MTU is 23/);
  assert.doesNotMatch(get('errorDetail').textContent, /33:44:55:66:77:88/);
  assert.equal(gattPosts.length, 1, 'A locally oversized payload with inconsistent peers makes no HTTP request');
  get('errorDialog').close();

  // With no peers, publication changes storage and nobody receives an update.
  context.testStatus = status({connections: []});
  vm.runInContext('renderStatus(testStatus)', context);
  context.testGatt.values[1].publicationLimit = 256;
  context.testGatt.values[1].minimumPeerMtu = 0;
  context.testGatt.values[1].connectedPeerCount = 0;
  vm.runInContext('renderGatt(testGatt)', context);
  assert.match(get('gattLimit').textContent, /No connected peers are reported; nobody can receive an update now/);
  assert.match(get('gattLimit').textContent, /Send only changes the stored value/);
  get('gattHex').value = '01'; get('gattHex').oninput();
  assert.equal(get('gattPublish').disabled, false, 'A valid payload may still be stored without peers');
  assert.match(get('gattLimit').textContent, /nobody can receive/);

  // Invalid, empty, one-byte, and worker-busy behavior.
  context.testStatus = status({connections: [peer517]});
  vm.runInContext('renderStatus(testStatus)', context);
  context.testGatt.values[1].maxLength = 512;
  context.testGatt.values[1].publicationLimit = 256;
  context.testGatt.values[1].minimumPeerMtu = 517;
  context.testGatt.values[1].connectedPeerCount = 1;
  vm.runInContext('renderGatt(testGatt)', context);
  get('gattHex').value = '0'; get('gattHex').oninput();
  assert.equal(get('gattPublish').disabled, true, 'Invalid hexadecimal disables Send');
  assert.match(get('gattDraftStatus').textContent, /invalid hexadecimal/);
  get('gattHex').value = '01'; get('gattHex').oninput();
  assert.equal(get('gattPublish').disabled, false, 'A one-byte update is allowed');
  await vm.runInContext('publishGatt()', context);
  assert.deepEqual(gattPosts[1].body, {op: 'publishValue', targetId: '0123456789abcdef-00000002', hex: '01'});
  assert.equal(get('gattPublish').disabled, true, 'A queued operation disables Send');
  context.testStatus = status({connections: [peer517], lastOperation: {
    operationId: 'gatt-op-2', operation: 'publishValue', target: 'gatt', state: 'Done', result: 'Dispatched',
    detail: 'Value stored; NimBLE accepted/queued the characteristic update request. Peer dispatch and client-application receipt are not proven.',
    data: {storageVerified: true, publicationAttempted: true, publicationDispatched: true}
  }});
  vm.runInContext('renderStatus(testStatus)', context);
  get('gattHex').value = ''; get('gattHex').oninput();
  assert.equal(get('gattDraftStatus').textContent, 'Payload: 0 bytes. Maximum single update: 256 bytes.');
  assert.equal(get('gattPublish').disabled, false, 'An empty publication remains valid');
  await vm.runInContext('publishGatt()', context);
  assert.deepEqual(gattPosts[2].body, {op: 'publishValue', targetId: '0123456789abcdef-00000002', hex: ''});
  context.testStatus = status({connections: [peer517], lastOperation: {
    operationId: 'gatt-op-3', operation: 'publishValue', target: 'gatt', state: 'Done', result: 'Dispatched',
    detail: 'Empty value stored; NimBLE accepted/queued the characteristic update request. Peer dispatch and client-application receipt are not proven.',
    data: {storageVerified: true, publicationAttempted: true, publicationDispatched: true}
  }});
  vm.runInContext('renderStatus(testStatus)', context);

  // A stale client snapshot must surface the server's numeric rejection detail.
  const serverDetail = 'Cannot publish 23 bytes: current publication limit is 20 bytes; maximum single update is 20 bytes (stored maximum 512 bytes; Web cap 256 bytes). Connected peers: 2. Smallest negotiated ATT MTU: 23; ATT MTU - 3 gives 20 value bytes. Every connected peer, including unsubscribed peers, must fit.';
  context.fetch = async (_url, request) => {
    gattPosts.push({url: '/api/gatt', body: JSON.parse(request.body)});
    return {ok: false, status: 400, json: async () => ({result: 'InvalidArgument', detail: serverDetail})};
  };
  get('gattHex').value = indicationSpaced; get('gattHex').oninput();
  context.testGatt.values[1].publicationLimit = 256;
  vm.runInContext('renderGatt(testGatt)', context);
  await assert.rejects(vm.runInContext('publishGatt()', context), /Cannot publish 23 bytes: current publication limit is 20 bytes/);
  assert.equal(get('errorDialog').open, true);
  assert.ok(get('errorDetail').textContent.startsWith(serverDetail), 'A changed server limit is reported with its current numeric reason');
  assert.match(get('errorDetail').textContent, /Smallest negotiated ATT MTU: 23; ATT MTU - 3 gives 20 value bytes/);
  assert.match(get('errorDetail').textContent, /Every connected peer, including unsubscribed peers/);
  assert.match(get('errorDetail').textContent, /Connection details are refreshing; server-reported peer data is authoritative and page peer addresses are not attributed/);
  assert.doesNotMatch(get('errorDetail').textContent, /33:44:55:66:77:88/);
  assert.equal(gattPosts.length, 4, 'Only valid payloads and the stale-snapshot check reach HTTP');
  // Dynamic GATT controls preserve the existing worker/API flow and readiness boundaries.
  const dynamicSnapshot = overrides => ({
    ...context.testGatt,
    state: 'Ready',
    valuesAvailable: true,
    servicesAvailable: true,
    services: dynamicServices,
    serviceLimit: 4,
    ...overrides
  });
  const inactiveAdvertising = {...status().advertising[0], active: false, mode: 0};
  context.testStatus = status({connections: [], advertising: [inactiveAdvertising]});
  vm.runInContext('renderStatus(testStatus)', context);
  context.testGatt = dynamicSnapshot({});
  vm.runInContext('renderGatt(testGatt)', context);
  const retainedGattIdentity = get('gattIdentity').textContent;
  const retainedGattCurrent = get('gattCurrent').textContent;
  const failedValueSnapshot = {...context.testGatt, valuesAvailable: false, values: null,
    servicesAvailable: true, services: dynamicServices, reason: 'Value serializer failed; inventory remains current.'};
  context.fetch = async url => {
    if(url === '/api/status') return {ok: true, status: 200, json: async () => context.testStatus};
    if(url === '/api/pairing') return {ok: true, status: 200, json: async () => ({requests: []})};
    assert.equal(url, '/api/gatt');
    return {ok: false, status: 500, json: async () => failedValueSnapshot};
  };
  vm.runInContext('polling=false', context);
  await vm.runInContext('poll()', context);
  assert.equal(get('gattIdentity').textContent, retainedGattIdentity, 'A valid HTTP 500 snapshot preserves the last value identity');
  assert.equal(get('gattCurrent').textContent, retainedGattCurrent, 'A valid HTTP 500 snapshot preserves the last current bytes as stale');
  assert.match(get('gattSnapshotStatus').textContent, /Values: stale or unavailable/);
  assert.match(get('gattInventoryStatus').textContent, /Current inventory/);
  assert.equal(get('gattServiceTable').children.length, 1, 'Available services remain in the current inventory table on a value snapshot failure');
  assert.ok(dynamicServices.every(service => get('gattServiceTable').textContent.includes(service.serviceId)), 'The current inventory rows remain visible');
  assert.equal(get('gattPublish').disabled, true, 'Failed values cannot be published');
  assert.equal(get('gattAddService').disabled, false, 'Independent current inventory still enables structural controls');
  assert.equal(get('errorDialog').open, true, 'The HTTP 500 is still reported while its valid body is rendered');
  assert.match(get('errorDetail').textContent, /HTTP 500/);
  get('errorDialog').close();
  vm.runInContext("shownPollErrors.delete('GATT status');renderGatt(testGatt)", context);
  assert.equal(get('gattAddService').disabled, false, 'Add is enabled with a current inventory and no peers or directed advertising');
  assert.equal(get('gattRemoveService').disabled, false, 'A current temporary service can be selected for removal');
  assert.equal(vm.runInContext('gattScenarios.length', context), 16, 'All existing scenarios remain, with four appended descriptor and subscription scenarios');
  get('gattScenario').value = '9'; get('gattScenario').onchange();
  assert.match(get('gattScenarioInfo').textContent, /click Add service manually/);
  const postsBeforePrefill = gattPosts.length;
  get('gattPrefill').onclick();
  assert.equal(get('gattServiceUuid').value, '12345678-1234-5678-1234-56789abce100');
  assert.equal(get('gattCharacteristicUuid').value, '12345678-1234-5678-1234-56789abce101');
  assert.equal(get('gattInitialValueHex').value, '01 02');
  assert.equal(gattPosts.length, postsBeforePrefill, 'Add scenario prefill never mutates firmware');
  context.fetch = async (url, request) => {
    gattPosts.push({url, body: JSON.parse(request.body)});
    return {ok: true, status: 202, json: async () => ({result: 'Queued', operationId: 'gatt-op-' + (++gattOperationSequence)})};
  };
  await vm.runInContext('addGattService()', context);
  assert.deepEqual(gattPosts.at(-1), {
    url: '/api/gatt',
    body: {
      op: 'addService',
      serviceUuid: '12345678-1234-5678-1234-56789abce100',
      characteristicUuid: '12345678-1234-5678-1234-56789abce101',
      valueHex: '0102'
    }
  }, 'Add uses the exact existing GATT route and valueHex request contract');
  vm.runInContext("localWorkerBusy=false;localPendingOperationId='';localPreviousOperationId='';", context);
  get('gattScenario').value = '10'; get('gattScenario').onchange();
  assert.match(get('gattScenarioInfo').textContent, /click Remove service manually/);
  get('gattPrefill').onclick();
  assert.equal(get('gattRemoveTarget').value, temporaryServiceId);
  assert.equal(gattPosts.length, postsBeforePrefill + 1, 'Remove scenario prefill only selects a service');
  await vm.runInContext('removeGattService()', context);
  assert.deepEqual(gattPosts.at(-1), {url: '/api/gatt', body: {op: 'removeService', serviceId: temporaryServiceId}});
  vm.runInContext("localWorkerBusy=false;localPendingOperationId='';localPreviousOperationId='';", context);

  assert.equal(vm.runInContext('gattScenarios.length', context), 16,
    'All existing scenarios remain, with four appended descriptor and subscription scenarios');
  const centralScenarios = [
    ['12', /sole 0x2900/],
    ['13', /unsigned uint8 value 10/],
    ['14', /type, units, and bounds as unresolved/]
  ];
  const beforeCentralPrefill = {
    target: get('gattTarget').value,
    value: get('gattHex').value,
    serviceUuid: get('gattServiceUuid').value,
    characteristicUuid: get('gattCharacteristicUuid').value,
    initialValue: get('gattInitialValueHex').value,
    removeTarget: get('gattRemoveTarget').value
  };
  const postsBeforeCentralPrefill = gattPosts.length;
  for (const [scenarioId, expectedText] of centralScenarios) {
    get('gattScenario').value = scenarioId;
    get('gattScenario').onchange();
    assert.match(get('gattScenarioInfo').textContent, expectedText);
    assert.equal(get('gattPrefill').disabled, true,
      'Central-only descriptor scenarios do not prefill peripheral publication controls');
    get('gattPrefill').onclick();
    assert.deepEqual({
      target: get('gattTarget').value,
      value: get('gattHex').value,
      serviceUuid: get('gattServiceUuid').value,
      characteristicUuid: get('gattCharacteristicUuid').value,
      initialValue: get('gattInitialValueHex').value,
      removeTarget: get('gattRemoveTarget').value
    }, beforeCentralPrefill, 'Selecting or prefilling a central-only scenario leaves every control unchanged');
    assert.equal(gattPosts.length, postsBeforeCentralPrefill,
      'Central-only scenario selection and prefill never call the device API');
  }
  get('gattScenario').value = '15';
  get('gattScenario').onchange();
  assert.match(get('gattScenarioInfo').textContent, /Use the central CCCD/);
  assert.equal(get('gattPrefill').disabled, false, 'The dual update is a publication scenario and can prefill its matching target');
  const postsBeforeDualPrefill = gattPosts.length;
  get('gattPrefill').onclick();
  assert.equal(get('gattTarget').value, '0123456789abcdef-00000004',
    'The appended dual Notify+Indicate fixture is the fourth target');
  assert.equal(get('gattHex').value, '01 02 03', 'Dual fixture prefill selects only its publication bytes');
  assert.equal(gattPosts.length, postsBeforeDualPrefill,
    'Selecting and prefilling the dual publication scenario does not write, publish, reset, or mutate a descriptor');

  vm.runInContext('gattReadyObserved=false', context);
  vm.runInContext("renderGatt({state:'Ready',valuesAvailable:false,values:null,servicesAvailable:false,services:null,serviceLimit:4,reason:'Fixture is not ready.'})", context);
  assert.equal(get('gattSendServiceChanged').disabled, true, 'Lifecycle Ready alone does not prove initial fixture readiness');
  assert.match(get('gattSnapshotStatus').textContent, /Fixture is not ready/);

  context.testStatus = status({connections: [peer517], advertising: [inactiveAdvertising]});
  vm.runInContext('renderStatus(testStatus)', context);
  context.testGatt = dynamicSnapshot({valuesAvailable:false,values:null,servicesAvailable:true,reason:'Value snapshot is busy.'});
  vm.runInContext('renderGatt(testGatt)', context);
  assert.equal(get('gattPublish').disabled, true, 'Value publication requires a current value snapshot');
  assert.equal(get('gattAddService').disabled, true, 'Structural changes are disabled while a peer is connected');
  assert.equal(get('gattRemoveService').disabled, true, 'Removal is disabled while a peer is connected');
  assert.equal(get('gattSendServiceChanged').disabled, false, 'Service Changed remains available while connected with observed GATT readiness');
  get('gattScenario').value = '8'; get('gattScenario').onchange();
  assert.match(get('gattScenarioInfo').textContent, /Keep a BLE central connected/);
  await vm.runInContext('sendGattServiceChanged()', context);
  assert.deepEqual(gattPosts.at(-1), {url: '/api/gatt', body: {op: 'sendServiceChanged'}});
  vm.runInContext("localWorkerBusy=false;localPendingOperationId='';localPreviousOperationId='';", context);

  context.testStatus = status({connections: [], advertising: [{...inactiveAdvertising, active: true, mode: 4}]});
  vm.runInContext('renderStatus(testStatus)', context);
  assert.equal(get('gattAddService').disabled, true, 'Add is disabled during active directed advertising');
  assert.equal(get('gattRemoveService').disabled, true, 'Remove is disabled during active directed advertising');

  const recoveryOperation = {
    operationId: 'recovery-op', operation: 'addService', target: 'services', state: 'Done', result: 'Failed',
    detail: 'Restart the device to recover the GATT database.', error: {code: 'gatt_recovery_required'}
  };
  context.testStatus = status({connections: [], advertising: [inactiveAdvertising], lastOperation: recoveryOperation});
  vm.runInContext('renderStatus(testStatus)', context);
  const unfinishedServiceUuid = get('gattServiceUuid').value;
  vm.runInContext("renderGatt({state:'RecoveryRequired',valuesAvailable:false,values:null,servicesAvailable:false,services:null,serviceLimit:4,reason:'Restart the device to recover the GATT database.'})", context);
  assert.match(get('gattSnapshotStatus').textContent, /Restart the device to recover the GATT database/);
  assert.match(get('gattInventoryStatus').textContent, /previous entries are hidden/);
  assert.equal(get('gattServiceTable').children.length, 0, 'Recovery never presents old inventory as current');
  assert.equal(get('gattAddService').disabled, true);
  assert.equal(get('gattRemoveService').disabled, true);
  assert.equal(get('gattSendServiceChanged').disabled, true);
  assert.equal(get('gattServiceUuid').value, unfinishedServiceUuid, 'Recovery preserves unfinished form input');

  const restoreWarningOperation = {
    operationId: 'restore-op', operation: 'addService', target: 'services', state: 'Done', result: 'Applied',
    detail: 'Advertising restoration was incomplete.', error: {code: 'advertising_restore_incomplete'}
  };
  context.testStatus = status({connections: [], advertising: [inactiveAdvertising], lastOperation: restoreWarningOperation});
  vm.runInContext('renderStatus(testStatus)', context);
  context.testGatt = dynamicSnapshot({});
  vm.runInContext('renderGatt(testGatt)', context);
  assert.match(get('gattSnapshotStatus').textContent, /Warning: advertising restoration was incomplete/);
  assert.match(get('gattInventoryStatus').textContent, /Current inventory/);
  console.log('web UI regression checks passed (including GATT controls and scenarios)');
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
