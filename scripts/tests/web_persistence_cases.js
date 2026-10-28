// Exercise the shipped preRun hook without game data or a browser database.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.resolve(__dirname, '../../runtime/web/pre.js'), 'utf8');

function fixture() {
  const state = {calls: [], timers: [], errors: [], saveErrors: [], handlers: {}, active: 0, maximum: 0, foreign: 0};
  const context = {
    Module: {preRun: [], printErr: text => state.errors.push(text), onSaveError: e => state.saveErrors.push(e)},
    ENVIRONMENT_IS_PTHREAD: false,
    fetch: async () => ({ok:true, json:async () => ({files:[], metadata:{}})}),
    ENV: {}, URL, document: {baseURI: 'http://127.0.0.1:8715/'},
    window: {addEventListener: (name, callback) => { state.handlers[name] = callback; }},
    IDBFS: {queuePersist: () => { ++state.foreign; }},
    FS: {
      mkdir() {},
      mount(backend, options, root) {
        assert.equal(root, '/persist');
        assert.equal(options.autoPersist, true);
      },
      syncfs(populate, callback) {
        ++state.active;
        state.maximum = Math.max(state.maximum, state.active);
        state.calls.push({populate, finish(error) { --state.active; callback(error); }});
      }
    },
    addRunDependency() {}, removeRunDependency() {},
    abort(error) { throw error; },
    setTimeout(callback) { state.timers.push(callback); return state.timers.length; },
    setInterval(callback) { state.periodic = callback; }
  };
  vm.createContext(context);
  vm.runInContext(source, context);
  context.Module.preRun[0]();
  return {state, context};
}
async function settle() { for (let i = 0; i < 8; ++i) await Promise.resolve(); }

async function run() {
  const {state: s, context: c} = fixture();
  assert.equal(s.calls.length, 1);
  assert.equal(s.calls[0].populate, true);
  await assert.rejects(c.Module.drakanSync(), /not been restored/);
  c.IDBFS.queuePersist({mountpoint: '/persist'});
  s.periodic();
  s.handlers.pagehide();
  assert.equal(s.calls.length, 1, 'restoration must finish before any writes');
  assert.equal(s.timers.length, 0, 'restoring existing nodes must not schedule write-back');
  s.calls[0].finish(null);

  c.IDBFS.queuePersist({mountpoint: '/other'});
  assert.equal(s.foreign, 1);
  for (let i = 0; i < 4; ++i) c.IDBFS.queuePersist({mountpoint: '/persist'});
  assert.equal(s.timers.length, 1, 'file-close notifications must coalesce');
  s.timers.shift()();
  await settle();
  assert.equal(s.calls.length, 2);
  assert.equal(s.calls[1].populate, false);
  const manual = c.Module.drakanSync();
  s.periodic();
  c.IDBFS.queuePersist({mountpoint: '/persist'});
  s.timers.shift()();
  await settle();
  assert.equal(s.calls.length, 2, 'manual/automatic writes must wait for the active sync');

  s.calls[1].finish(new Error('simulated storage failure'));
  await settle();
  assert.equal(s.saveErrors.length, 1, 'a storage failure must reach the player');
  assert.equal(s.calls.length, 3, 'a failed flush must not poison the queue');
  s.calls[2].finish(null);
  await manual;
  await settle();
  assert.equal(s.calls.length, 4);
  s.calls[3].finish(null);
  await settle();
  assert.equal(s.maximum, 1, 'only one persistence operation may run at a time');
  assert.equal(s.active, 0);

  // A settings temp file can be renamed after IDBFS enumerates the local
  // tree but before it reads the file. Retry only that transient ENOENT,
  // retaining the serialization slot until a fresh snapshot completes.
  const raced = fixture();
  raced.state.calls[0].finish(null);
  const retrying = raced.context.Module.drakanSync();
  const queued = raced.context.Module.drakanSync();
  await settle();
  for (let attempt = 0; attempt < 3; ++attempt) {
    raced.state.calls[attempt + 1].finish({errno:44});
    await settle();
    assert.equal(raced.state.timers.length, 1);
    assert.equal(raced.state.calls.length, attempt + 2, 'queued sync must not overtake a retry');
    raced.state.timers.shift()();
    await settle();
  }
  raced.state.calls[4].finish(null);
  await retrying;
  await settle();
  raced.state.calls[5].finish(null);
  await queued;
  assert.equal(raced.state.maximum, 1);

  const exhausted = fixture();
  exhausted.state.calls[0].finish(null);
  const failure = assert.rejects(exhausted.context.Module.drakanSync(), e => e.errno === 44);
  await settle();
  for (let attempt = 0; attempt < 4; ++attempt) {
    exhausted.state.calls[attempt + 1].finish({errno:44});
    await settle();
    if (attempt < 3) { exhausted.state.timers.shift()(); await settle(); }
  }
  await failure;
  assert.equal(exhausted.state.timers.length, 0, 'persistent failure must not retry forever');

  const failed = fixture();
  assert.throws(() => failed.state.calls[0].finish(new Error('restore unavailable')), /restore unavailable/);
  await assert.rejects(failed.context.Module.drakanSync(), /not been restored/);
  failed.state.periodic();
  failed.state.handlers.pagehide();
  assert.equal(failed.state.calls.length, 1, 'failed restoration must never overwrite existing storage');
  console.log('File-close persistence, serialization, failure recovery and restore protection passed');
}
run().catch(error => { console.error(error); process.exitCode = 1; });
