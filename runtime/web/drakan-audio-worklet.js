class DrakanAudio extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const p = options.processorOptions;
    this.control = new Int32Array(p.memory, p.control, 4);
    this.samples = new Float32Array(p.memory, p.samples, p.frames * 2);
    this.frames = p.frames;
    this.phase = 0;
    Atomics.store(this.control, 2, 1);
  }
  process(inputs, outputs) {
    const output = outputs[0];
    let read = Atomics.load(this.control, 0) >>> 0;
    const write = Atomics.load(this.control, 1) >>> 0;
    const ratio = 48000 / sampleRate;
    for (let i = 0; i < output[0].length; ++i) {
      const available = (write - read) >>> 0;
      if (available < 2) { output[0][i] = output[1][i] = 0; continue; }
      const a = read % this.frames, b = (read + 1) % this.frames;
      for (let c = 0; c < 2; ++c) {
        output[c][i] = this.samples[a * 2 + c] * (1 - this.phase) + this.samples[b * 2 + c] * this.phase;
      }
      this.phase += ratio;
      const consumed = Math.min(Math.floor(this.phase), available - 1);
      read = (read + consumed) >>> 0;
      this.phase -= consumed;
    }
    Atomics.store(this.control, 0, read);
    return true;
  }
}
registerProcessor('drakan-audio', DrakanAudio);
