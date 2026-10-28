const canvas = document.getElementById('canvas');
const player = document.getElementById('player');
const startup = document.getElementById('startup');
const status = document.getElementById('status');
const startButton = document.getElementById('start');
const loadProgress = document.getElementById('load-progress');
const saveError = document.getElementById('save-error');
const retrySave = document.getElementById('retry-save');
const pointerMotion = new DrakanPointerMotion((x, y) => window.Module?._drakan_web_mouse_move?.(x, y));
window.drakanLog = '';
function message(text) {
  window.drakanLog = (window.drakanLog + text + '\n').slice(-30000);
  console.log(text);
}
let audioContext;
let audioParameters;
let audioStarting;
let sessionStarted = false;
let runtimeReady = false;
let initialized = false;
let failureReported = false;
let sessionEnded = false;
let capturePending = false;
const gameActive = () => runtimeReady && !sessionEnded && !failureReported;
function resizeGameSurface() {
  if (!gameActive()) return;
  const bounds = player.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.min(8192, Math.round(bounds.width * ratio)));
  const height = Math.max(1, Math.min(8192, Math.round(bounds.height * ratio)));
  window.Module?._drakan_web_resize?.(width, height);
}
new ResizeObserver(resizeGameSurface).observe(player);
window.addEventListener('resize', resizeGameSurface);
let pixelRatioQuery;
function watchPixelRatio() {
  pixelRatioQuery?.removeEventListener('change', pixelRatioChanged);
  pixelRatioQuery = matchMedia(`(resolution: ${window.devicePixelRatio || 1}dppx)`);
  pixelRatioQuery.addEventListener('change', pixelRatioChanged, {once:true});
}
function pixelRatioChanged() {
  watchPixelRatio();
  resizeGameSurface();
}
watchPixelRatio();
const inMenu = () => window.Module?._drakan_web_in_menu?.() !== 0;
function releaseInput() {
  pointerMotion.reset();
  capturePending = false;
  if (gameActive()) window.Module?._drakan_web_release_input?.();
}
function releaseMouse() { if (document.pointerLockElement === canvas) document.exitPointerLock(); }
function closeGpuWorker() {
  globalThis.drakanRasterWorker?.terminate();
  globalThis.drakanRasterWorker = null;
  globalThis.drakanGpuPending?.clear();
  globalThis.drakanGpuInputs = null;
  globalThis.drakanGpuResult = null;
}

function reloadGame() { location.reload(); }

// Browser input reaches the same guest pad state as the native player. There
// is no second set of HTML game menus or controls.
const keyCodes = {
  Space:32, Quote:39, Comma:44, Minus:45, Period:46, Slash:47, Semicolon:59,
  Equal:61, BracketLeft:91, Backslash:92, BracketRight:93, Backquote:96,
  Escape:256, Enter:257, Tab:258, Backspace:259, Insert:260, Delete:261,
  ArrowRight:262, ArrowLeft:263, ArrowDown:264, ArrowUp:265, PageUp:266,
  PageDown:267, Home:268, End:269, CapsLock:280, ScrollLock:281, NumLock:282,
  PrintScreen:283, NumpadDecimal:330, NumpadDivide:331, NumpadMultiply:332,
  NumpadSubtract:333, NumpadAdd:334, NumpadEnter:335,
  ShiftLeft:340, ControlLeft:341, AltLeft:342, MetaLeft:343,
  ShiftRight:344, ControlRight:345, AltRight:346, MetaRight:347, ContextMenu:348
};
for (let i = 0; i < 26; ++i) keyCodes['Key' + String.fromCharCode(65 + i)] = 65 + i;
for (let i = 0; i < 10; ++i) { keyCodes['Digit' + i] = 48 + i; keyCodes['Numpad' + i] = 320 + i; }
for (let i = 0; i < 12; ++i) keyCodes['F' + (i + 1)] = 290 + i;

document.addEventListener('keydown', event => {
  if (!gameActive() || !saveError.hidden || (document.activeElement !== canvas && document.pointerLockElement !== canvas)) return;
  if (event.code === 'Tab' && event.shiftKey) return;
  if (event.code === 'Enter' && event.altKey) {
    event.preventDefault();
    if (!event.repeat) toggleFullscreen();
    return;
  }
  if (event.code === 'F10') {
    event.preventDefault();
    if (!event.repeat && !inMenu()) {
      if (document.pointerLockElement === canvas) releaseMouse();
      else captureMouse();
    }
    return;
  }
  const key = keyCodes[event.code];
  if (key !== undefined) {
    event.preventDefault();
    if (inMenu() && ['KeyF', 'Enter', 'NumpadEnter', 'Escape'].includes(event.code)) capturePending = true;
    window.Module?._drakan_web_key_event?.(key, 1);
  }
});
document.addEventListener('keyup', event => {
  const key = keyCodes[event.code];
  if (gameActive() && key !== undefined) window.Module?._drakan_web_key_event?.(key, 0);
});
const mouseButton = button => button === 0 ? 0 : button === 2 ? 1 : button === 1 ? 2 : button === 3 || button === 4 ? button : -1;
canvas.addEventListener('mousedown', event => {
  canvas.focus();
  if (!gameActive()) return;
  if (!inMenu() && document.pointerLockElement !== canvas) {
    event.preventDefault();
    captureMouse();
    return;
  }
  window.Module?._drakan_web_mouse_button?.(mouseButton(event.button), 1);
});
document.addEventListener('mouseup', event => {
  if (gameActive()) window.Module?._drakan_web_mouse_button?.(mouseButton(event.button), 0);
});
canvas.addEventListener('mousemove', event => {
  if (gameActive() && document.pointerLockElement === canvas)
    pointerMotion.move(event.movementX, event.movementY);
});
document.addEventListener('pointerlockchange', () => {
  pointerMotion.reset();
  if (gameActive()) window.Module?._drakan_web_pointer_lock?.(document.pointerLockElement === canvas ? 1 : 0);
});
canvas.addEventListener('focus', () => window.Module?._drakan_web_focus?.(1));
canvas.addEventListener('blur', () => { window.Module?._drakan_web_focus?.(0); releaseInput(); releaseMouse(); });
window.addEventListener('blur', () => { window.Module?._drakan_web_focus?.(0); releaseInput(); releaseMouse(); });
window.addEventListener('focus', () => window.Module?._drakan_web_focus?.(document.hasFocus() && document.activeElement === canvas ? 1 : 0));
document.addEventListener('visibilitychange', () => { if (document.hidden) releaseInput(); });
canvas.addEventListener('contextmenu', event => event.preventDefault());

function showFailure(error) {
  if (failureReported) return;
  releaseInput();
  releaseMouse();
  failureReported = true;
  runtimeReady = false;
  message(String(error));
  startup.hidden = false;
  loadProgress.hidden = true;
  status.textContent = 'The game could not continue. Reload to try again.';
  startButton.disabled = false;
  startButton.textContent = 'Reload';
  startButton.onclick = reloadGame;
  globalThis.drakanAudioNode?.disconnect();
  audioContext?.suspend().catch(() => {});
  try { window.Module?._drakan_web_stop?.(); } catch (_) {}
}
window.addEventListener('error', event => { if (sessionStarted) showFailure(event.error || event.message); });
window.addEventListener('unhandledrejection', event => { if (sessionStarted) showFailure(event.reason); });
async function ensureAudio() {
  if (globalThis.drakanAudioNode) { await audioContext.resume(); return; }
  if (!audioParameters) return;
  if (audioStarting) return audioStarting;
  audioStarting = (async () => {
    try {
      await audioContext.audioWorklet.addModule('drakan-audio-worklet.js');
      const [memory, control, samples, frames] = audioParameters;
      const node = new AudioWorkletNode(audioContext, 'drakan-audio', {numberOfInputs:0, numberOfOutputs:1, outputChannelCount:[2], processorOptions:{memory,control,samples,frames}});
      globalThis.drakanAudioNode = node;
      node.connect(audioContext.destination);
      await audioContext.resume();
    } catch (error) { message('Audio could not start: ' + error); }
    finally { audioStarting = undefined; }
  })();
  return audioStarting;
}
globalThis.drakanStartAudio = function(...parameters) { audioParameters = parameters; return ensureAudio(); };
async function captureMouse() {
  if (!gameActive() || inMenu() || !saveError.hidden) return;
  canvas.focus();
  await ensureAudio();
  try {
    try { await canvas.requestPointerLock({unadjustedMovement:true}); }
    catch (error) {
      if (error.name !== 'NotSupportedError') throw error;
      await canvas.requestPointerLock();
    }
  }
  catch (error) { message('Mouse capture: ' + error); }
}
async function toggleFullscreen() {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await player.requestFullscreen();
  } catch (error) { message('Fullscreen: ' + error); }
}
function followGameMenus() {
  if (!gameActive()) return;
  const menu = inMenu();
  if (menu && document.pointerLockElement === canvas) releaseMouse();
  if (!menu && capturePending) {
    capturePending = false;
    if (navigator.userActivation?.isActive) captureMouse();
  }
  requestAnimationFrame(followGameMenus);
}
function showSaveError(error) {
  message('Save storage: ' + error);
  releaseInput();
  releaseMouse();
  const shouldPause = gameActive() && !inMenu();
  saveError.hidden = false;
  retrySave.disabled = false;
  retrySave.focus();
  if (shouldPause) {
    Module._drakan_web_key_event(257, 1);
    Module._drakan_web_key_event(257, 0);
  }
}
retrySave.onclick = async () => {
  retrySave.disabled = true;
  try {
    await Module.drakanSync();
    saveError.hidden = true;
    if (gameActive()) canvas.focus();
  } catch (error) { message('Save storage: ' + error); }
  finally { retrySave.disabled = false; }
};
startButton.onclick = async function () {
  if (!globalThis.WebAssembly?.Tag || !globalThis.AudioWorkletNode) {
    status.textContent = 'A current desktop browser is required.';
    return;
  }
  if (!globalThis.crossOriginIsolated || !globalThis.SharedArrayBuffer) {
    status.textContent = 'Open the game with its Play-Browser launcher or the browser player page.';
    return;
  }
  this.disabled = true;
  this.textContent = 'Loading…';
  sessionStarted = true;
  audioContext = new AudioContext({sampleRate:48000});
  await audioContext.resume();
  window.Module = {
    canvas,
    print:message,
    printErr:message,
    setStatus:text => {
      if (!text || runtimeReady) return;
      const progress = text.match(/\((\d+)\/(\d+)\)/);
      if (progress && Number(progress[2]) > 0) {
        loadProgress.hidden = false;
        loadProgress.value = Number(progress[1]) / Number(progress[2]);
      }
    },
    onAbort:showFailure,
    onSaveError:showSaveError,
    onExit:code => {
      if (sessionEnded) return;
      closeGpuWorker();
      sessionEnded = true;
      runtimeReady = false;
      releaseMouse();
      if (code) showFailure('The game exited with status ' + code);
      const store = initialized && Module.drakanSync ? Module.drakanSync() : Promise.resolve();
      store.then(() => {
        if (Module.drakanRestartRequested) { location.reload(); return; }
        if (!failureReported) {
          startup.hidden = false;
          status.textContent = 'Game closed.';
          startButton.textContent = 'Return';
          startButton.disabled = false;
          startButton.onclick = reloadGame;
        }
      }).catch(showSaveError);
      if (audioContext.state !== 'closed') audioContext.close().catch(() => {});
    },
    onRuntimeInitialized:() => {
      initialized = true;
      runtimeReady = true;
      startup.hidden = true;
      loadProgress.hidden = true;
      status.textContent = '';
      canvas.focus();
      resizeGameSurface();
      requestAnimationFrame(followGameMenus);
    }
  };
  const script = document.createElement('script');
  script.src = 'drakan.js';
  script.onerror = () => showFailure('The game files could not be loaded.');
  document.body.append(script);
};
