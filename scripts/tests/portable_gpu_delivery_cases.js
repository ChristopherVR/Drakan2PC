"use strict";
// Exercise the shipped worker's ownership handshake without a GPU or game data.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../../runtime/web/portable_gpu_worker.js"), "utf8");

function harness({failure = false} = {}) {
    let release, submitted;
    const gate = new Promise(resolve => { release = resolve; });
    const queued = new Promise(resolve => { submitted = resolve; });
    const expected = new Uint32Array([0x12345678, 0xabcdef01, 0x10203040, 0x50607080]);
    const device = {
        lost: new Promise(() => {}),
        pushErrorScope() {},
        async popErrorScope() { return null; },
        createShaderModule() { return {async getCompilationInfo() { return {messages: []}; }}; },
        async createComputePipelineAsync() { return {getBindGroupLayout() { return {}; }}; },
        createBuffer() {
            return {
                destroy() {}, unmap() {},
                async mapAsync() { submitted(); await gate; if (failure) throw Error("Simulated device failure"); },
                getMappedRange() { return expected.buffer; }
            };
        },
        createBindGroup() { return {}; },
        createCommandEncoder() {
            return {
                beginComputePass() { return {setPipeline() {}, setBindGroup() {}, dispatchWorkgroups() {}, end() {}}; },
                copyBufferToBuffer() {}, finish() { return {}; }
            };
        },
        queue: {writeBuffer() {}, submit() {}}
    };
    const messages = [];
    const context = vm.createContext({
        onmessage: null, postMessage(message) { messages.push(message); },
        console: {info() {}, error() {}},
        navigator: {gpu: {async requestAdapter() { return {async requestDevice() { return device; }}; }}},
        GPUBufferUsage: {STORAGE: 1, COPY_DST: 2, COPY_SRC: 4, MAP_READ: 8}, GPUMapMode: {READ: 1},
        Atomics, SharedArrayBuffer, ArrayBuffer, Uint8Array, Uint32Array, Int32Array, Promise, Error, Math
    });
    vm.runInContext(source, context);
    return {context, queued, release, expected, messages};
}
async function delivery({direct = true, cancel = false, failure = false, claimed = false} = {}) {
    const {context, queued, release, expected, messages} = harness({failure});
    const heap = new SharedArrayBuffer(512), bytes = new Uint8Array(heap);
    bytes.fill(0xa5);
    const completion = new Int32Array(heap, 64, 1);
    completion[0] = 0;
    const result = new SharedArrayBuffer(direct ? 4 : 20), token = new Int32Array(result, 0, 1);
    const job = {id: 7, shader: "procedural", inputs: [32, 4, 4, 16].map(size => new ArrayBuffer(size)),
                 result, width: 1, height: 2};
    if (direct) Object.assign(job, {heap, output: 128, completion: 64});
    const running = context.onmessage({data: job});
    await queued;
    if (cancel) {
        assert.equal(Atomics.compareExchange(token, 0, 0, -2), 0);
        // The original caller has returned and its addresses have been reused.
        bytes.fill(0x5a);
    } else if (claimed) token[0] = 2;
    const before = bytes.slice();
    release();
    await running;
    assert.equal(messages.length, 1);
    assert.equal(messages[0].id, 7);
    assert.equal(messages[0].inputs, job.inputs);
    if (cancel || claimed) {
        assert.deepEqual(bytes, before, "late completion must not touch reused Wasm addresses");
        assert.equal(token[0], cancel ? -2 : 2);
    } else if (direct) {
        assert.equal(completion[0], failure ? -1 : 1);
        assert.equal(token[0], failure ? -1 : 1);
        assert.deepEqual(bytes.subarray(128, 144), failure ? before.subarray(128, 144) : new Uint8Array(expected.buffer));
        assert.deepEqual(bytes.subarray(0, 64), before.subarray(0, 64));
        assert.deepEqual(bytes.subarray(68, 128), before.subarray(68, 128));
        assert.deepEqual(bytes.subarray(144), before.subarray(144));
    } else {
        assert.equal(token[0], failure ? -1 : 1);
        if (!failure) assert.deepEqual(new Uint32Array(result, 4), expected);
        assert.deepEqual(bytes, before);
    }
}
async function until(predicate) {
    const deadline = Date.now() + 2000;
    while (!predicate()) {
        if (Date.now() > deadline) throw Error("Queue fixture timed out");
        await new Promise(setImmediate);
    }
}
async function queueDelivery({cancel = false, failure = false, beforeRead = false} = {}) {
    const {context, queued, release, expected, messages} = harness({failure});
    const memory = new WebAssembly.Memory({initial: 1, maximum: 2, shared: true});
    const control = new Int32Array(memory.buffer, 64, 15);
    const running = context.onmessage({data: {memory, control: 64, shader: "procedural"}});
    await until(() => control[1] !== 0);
    assert.equal(control[1], 1);
    // The control view predates growth; all job allocations exceed its old size.
    memory.grow(1);
    const bytes = new Uint8Array(memory.buffer);
    const output = bytes.subarray(70512, 70528);
    output.fill(0xa5);
    control.set([70000, 32, 70032, 4, 70036, 4, 70512, 16, 70040, 0, 1, 2, 0], 2);
    Atomics.store(control, 0, 1);
    Atomics.notify(control, 0);
    if (beforeRead) {
        assert.equal(Atomics.compareExchange(control, 0, 1, 4), 1);
        Atomics.notify(control, 0);
    } else {
        await queued;
        assert.equal(control[0], 3);
        if (cancel) {
            assert.equal(Atomics.compareExchange(control, 0, 3, 4), 3);
            // Simulate freeing and reusing the input/output allocations.
            bytes.fill(0x5a, 70000, 70040);
            output.fill(0x5a);
        }
        release();
    }
    if (!cancel && !beforeRead) await until(() => control[0] === (failure ? -1 : 6));
    else await running;
    assert.deepEqual(output, cancel ? new Uint8Array(16).fill(0x5a) :
        failure || beforeRead ? new Uint8Array(16).fill(0xa5) : new Uint8Array(expected.buffer));
    assert.equal(messages.length, 0, "shared jobs must not need a UI message callback");
    Atomics.store(control, 0, 4); Atomics.notify(control, 0);
    await running;
}
function workerErrorOwnership() {
    const bridge = fs.readFileSync(path.join(__dirname, "../../runtime/gs/src/portable_gpu_web.cpp"), "utf8");
    const match = bridge.match(/const fail=\(\)=>\{[\s\S]*?\n                    \};\n                    fail\(\);/);
    assert.ok(match, "Production worker-error ownership handler must be exercised");
    for (const initial of [0, 1, 2, 3, 4, 5, 6]) {
        const control = new Int32Array(new SharedArrayBuffer(60));
        control[0] = initial;
        const timers = [];
        let terminated = false;
        vm.runInNewContext(match[0], {control, Atomics,
            setTimeout(callback) { timers.push(callback); },
            worker: {terminate() { assert.equal(control[0], -1); terminated = true; }}
        });
        if (initial === 2 || initial === 5) {
            assert.equal(terminated, false, "A live copy retains caller allocations");
            assert.equal(control[0], initial);
            assert.equal(timers.length, 1);
            control[0] = 6;
            timers.shift()();
        }
        assert.equal(terminated, true);
        assert.equal(control[0], -1);
    }
}
(async () => {
    for (const options of [{}, {failure: true}, {cancel: true}, {cancel: true, failure: true},
                           {claimed: true}, {direct: false}, {direct: false, failure: true}]) await delivery(options);
    for (const options of [{}, {failure: true}, {cancel: true}, {cancel: true, failure: true},
                           {beforeRead: true}]) await queueDelivery(options);
    workerErrorOwnership();
    console.log("Shared GPU queue, memory growth, cancellation, direct delivery and legacy readback passed");
})().catch(error => { console.error(error); process.exitCode = 1; });
