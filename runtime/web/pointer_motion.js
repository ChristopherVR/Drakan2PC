// Preserve sub-count movement until the native input bridge can consume it.
// Capture/focus changes discard old motion; no sensitivity or acceleration here.
class DrakanPointerMotion {
  constructor(send) { this.send = send; this.reset(); }
  reset() { this.x = this.y = 0; }
  move(x, y) {
    if (!Number.isFinite(x) || !Number.isFinite(y)) return;
    this.x += x; this.y += y;
    const dx = Math.trunc(this.x), dy = Math.trunc(this.y);
    this.x -= dx; this.y -= dy;
    if (dx || dy) this.send(dx, dy);
  }
}
if (typeof module !== 'undefined') module.exports = DrakanPointerMotion;
