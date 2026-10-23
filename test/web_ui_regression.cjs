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
    device: { name: 'WROVER', address: 'AA:BB:CC:DD:EE:FF' },
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

  console.log('web UI regression checks passed');
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
