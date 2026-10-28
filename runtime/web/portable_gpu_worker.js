"use strict";
// Own the asynchronous WebGPU device separately from blocking Wasm pthreads.
// Jobs own their input copies and a private completion token. A direct Wasm
// result claims that token before touching output memory; cancellation claims
// it instead before the caller releases its addresses.
let device, pipeline;
let buffers = [], capacities = [], staging, stagingCapacity = 0;
let unavailable;
async function initialize(shader) {
    if (unavailable) throw unavailable;
    if (device) return;
    try {
        if (!navigator.gpu) throw new Error("WebGPU is unavailable");
        const adapter = await navigator.gpu.requestAdapter({powerPreference: "high-performance"});
        if (!adapter) throw new Error("No WebGPU adapter");
        device = await adapter.requestDevice();
        device.lost.then(info => { unavailable = new Error(`WebGPU device lost: ${info.message}`); });
        const module = device.createShaderModule({code: shader, label: "Drakan ordered GS tiles"});
        const info = await module.getCompilationInfo();
        const errors = info.messages.filter(message => message.type === "error");
        if (errors.length) throw new Error(errors.map(e => `${e.lineNum}:${e.linePos} ${e.message}`).join("\n"));
        pipeline = await device.createComputePipelineAsync({layout: "auto", compute: {module, entryPoint: "main"}});
        console.info("[portable-gpu]", adapter.info?.description || adapter.info?.device || "WebGPU adapter");
    } catch (error) { unavailable = error; throw error; }
}
async function rasterJob(job) {
    const status = new Int32Array(job.result, job.resultOffset || 0, 1);
    const pending = job.queued ? 3 : 0, copying = job.queued ? 5 : 2, complete = job.queued ? 6 : 1;
    try {
        await initialize(job.shader);
        if (unavailable) throw unavailable;
        device.pushErrorScope("validation");
        const inputs = job.inputs;
        const sizes = job.sizes || inputs.map(input => input.byteLength);
        for (let i = 0; i < 4; i++) {
            const size = Math.max(4, i === 2 && job.textureWords ? job.textureWords * 4 : sizes[i]);
            if (!buffers[i] || capacities[i] < size) {
                buffers[i]?.destroy(); capacities[i] = Math.ceil(size / 256) * 256;
                buffers[i] = device.createBuffer({size: capacities[i], usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | (i === 3 ? GPUBufferUsage.COPY_SRC : 0)});
            }
            if (i === 2 && job.textureWords) {
                const writes = new Uint32Array(inputs[4], 0, sizes[4] / 4);
                if (writes.length % 2) throw new Error("Invalid texture write ranges");
                let source = 0;
                for (let w = 0; w < writes.length; w += 2) {
                    const offset = writes[w] * 4, bytes = writes[w + 1] * 4;
                    if (offset + bytes > size || source + bytes > sizes[2]) throw new Error("Invalid texture write extent");
                    if (bytes) device.queue.writeBuffer(buffers[2], offset, inputs[2], source, bytes);
                    source += bytes;
                }
                if (source !== sizes[2]) throw new Error("Incomplete texture writes");
            } else if (sizes[i]) device.queue.writeBuffer(buffers[i], 0, inputs[i], 0, sizes[i]);
        }
        const bytes = sizes[3];
        if (!staging || stagingCapacity < bytes) {
            staging?.destroy(); stagingCapacity = Math.ceil(bytes / 256) * 256;
            staging = device.createBuffer({size: stagingCapacity, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST});
        }
        const bind = device.createBindGroup({layout: pipeline.getBindGroupLayout(0), entries: [0,1,2,3].map(binding => ({binding, resource: {buffer: buffers[binding], size: Math.max(4, binding === 2 && job.textureWords ? job.textureWords * 4 : sizes[binding])}}))});
        const encoder = device.createCommandEncoder(); const pass = encoder.beginComputePass();
        pass.setPipeline(pipeline); pass.setBindGroup(0, bind); pass.dispatchWorkgroups(Math.ceil(job.width / 8), Math.ceil(job.height / 8)); pass.end();
        encoder.copyBufferToBuffer(buffers[3], 0, staging, 0, bytes); device.queue.submit([encoder.finish()]);
        // Both promises concern the submitted job. Waiting for validation before
        // requesting the map added a second GPU-process round trip per dispatch.
        const [validation] = await Promise.all([device.popErrorScope(), staging.mapAsync(GPUMapMode.READ, 0, bytes)]);
        if (validation) { staging.unmap(); throw new Error(validation.message); }
        const mapped = new Uint8Array(staging.getMappedRange(0, bytes));
        if (job.heap) {
            // Prepare views before claiming. No await or fallible allocation may
            // separate the claim from copying and waking the waiting caller.
            const output = new Uint8Array(job.heap, job.output, bytes);
            const completion = job.queued ? null : new Int32Array(job.heap, job.completion, 1);
            if (Atomics.compareExchange(status, 0, pending, copying) === pending) {
                output.set(mapped);
                if (completion) {
                    Atomics.store(completion, 0, 1);
                    Atomics.notify(completion, 0);
                }
                Atomics.store(status, 0, complete);
            }
        } else {
            new Uint8Array(job.result, 4).set(mapped);
            Atomics.store(status, 0, 1);
        }
        staging.unmap();
    } catch (error) {
        console.error("[portable-gpu]", error);
        if (job.heap) {
            const completion = job.queued ? null : new Int32Array(job.heap, job.completion, 1);
            if (Atomics.compareExchange(status, 0, pending, copying) === pending) {
                if (completion) {
                    Atomics.store(completion, 0, -1);
                    Atomics.notify(completion, 0);
                }
                Atomics.store(status, 0, -1);
            }
        } else Atomics.store(status, 0, -1);
    } finally { Atomics.notify(status, 0); if (job.id !== undefined) postMessage({id: job.id, inputs: job.inputs}, job.inputs); }
}

// The queue has process lifetime. Callers retain their input/output allocations
// while state is Reading (2) or Writing (5), and may cancel Ready (1) or Async (3).
// No caller address is accessed after cancellation. The worker owns input copies
// before publishing Async, so device promises never retain caller input memory.
async function sharedQueue({memory, control: address, shader}) {
    const control = new Int32Array(memory.buffer, address, 15);
    let inputs = [];
    try {
        if (!Atomics.waitAsync) throw Error("Shared GPU queue requires Atomics.waitAsync");
        await initialize(shader);
        Atomics.store(control, 1, 1); Atomics.notify(control, 1);
        for (;;) {
            const state = Atomics.load(control, 0);
            if (state === 4 || state === -1) return;
            if (state !== 1) { await Atomics.waitAsync(control, 0, state).value; continue; }
            if (Atomics.compareExchange(control, 0, 1, 2) !== 1) continue;
            try {
                // Refresh after memory growth. The queue's original low-memory
                // control view remains valid while newer allocations may be larger.
                const heap = memory.buffer;
                const header = new Uint32Array(heap, address, 15);
                const sizes = [];
                for (let i = 0; i < 5; ++i) {
                    const pointer = header[2 + i * 2], bytes = header[3 + i * 2];
                    if (!inputs[i] || inputs[i].byteLength < bytes)
                        inputs[i] = new ArrayBuffer(Math.ceil(Math.max(4, bytes) / 256) * 256);
                    new Uint8Array(inputs[i], 0, bytes).set(new Uint8Array(heap, pointer, bytes));
                    sizes.push(bytes);
                }
                const job = {shader, inputs, sizes, result: control.buffer, resultOffset: address,
                    heap, output: header[8], width: header[12], height: header[13],
                    textureWords: header[14], queued: true};
                Atomics.store(control, 0, 3); Atomics.notify(control, 0);
                await rasterJob(job);
            } catch (error) {
                console.error("[portable-gpu]", error);
                Atomics.store(control, 0, -1); Atomics.notify(control, 0);
                return;
            }
        }
    } catch (error) {
        console.error("[portable-gpu]", error);
        Atomics.store(control, 1, -1); Atomics.notify(control, 1);
    }
}
onmessage = async ({data}) => {
    if (data.memory) await sharedQueue(data);
    else await rasterJob(data);
};
