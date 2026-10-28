// Browser player loader for the static project site. The player picks their locally built DrakanPC-web folder and
// their ISO; sw.js then serves play/ from those files with the cross-origin isolation headers the game needs.
const ui = {
  build: document.getElementById('pick-build'),
  iso: document.getElementById('pick-iso'),
  textures: document.getElementById('pick-textures'),
  play: document.getElementById('play'),
  status: document.getElementById('status'),
  buildState: document.getElementById('build-state'),
  isoState: document.getElementById('iso-state'),
  texturesState: document.getElementById('textures-state'),
};
const chosen = {build: null, iso: null, textures: null};

function show(element, text, ok) {
  element.textContent = text;
  element.classList.toggle('ok', !!ok);
}
function refresh() {
  show(ui.buildState, chosen.build ? chosen.build.name : 'not chosen', !!chosen.build);
  show(ui.isoState, chosen.iso ? chosen.iso.name : 'not chosen', !!chosen.iso);
  show(ui.texturesState, chosen.textures ? chosen.textures.name : 'none', !!chosen.textures);
  ui.play.disabled = !(chosen.build && chosen.iso);
}

async function isBuildFolder(directory) {
  for (const name of ['index.html', 'drakan.js', 'drakan.wasm']) {
    try { await directory.getFileHandle(name); } catch (_) { return false; }
  }
  return true;
}

async function choose(key, pick, validate) {
  ui.status.textContent = '';
  let handle;
  try { handle = await pick(); } catch (error) { if (error.name !== 'AbortError') ui.status.textContent = String(error); return; }
  if (validate) {
    const problem = await validate(handle);
    if (problem) { ui.status.textContent = problem; return; }
  }
  chosen[key] = handle;
  await drakanHandles.set(key, handle);
  refresh();
}

ui.build.onclick = () => choose('build', () => showDirectoryPicker({id: 'drakan-build', mode: 'read'}),
  async handle => (await isBuildFolder(handle)) ? '' :
    'That folder is not a browser build: it needs index.html, drakan.js and drakan.wasm (dist\\DrakanPC-web).');
ui.iso.onclick = () => choose('iso', async () => (await showOpenFilePicker({
  id: 'drakan-iso',
  types: [{description: 'Disc image', accept: {'application/octet-stream': ['.iso']}}],
}))[0], async handle => (await handle.getFile()).size < 100 * 1024 * 1024 ?
  'That file is too small to be the game disc. Choose the full .iso image.' : '');
ui.textures.onclick = () => choose('textures', () => showDirectoryPicker({id: 'drakan-textures', mode: 'read'}));

async function permitted(handle) {
  if (!handle) return true;
  const options = {mode: 'read'};
  if (await handle.queryPermission(options) === 'granted') return true;
  return await handle.requestPermission(options) === 'granted';
}

ui.play.onclick = async () => {
  ui.play.disabled = true;
  ui.status.textContent = 'Starting…';
  try {
    for (const key of ['build', 'iso', 'textures']) {
      if (!(await permitted(chosen[key]))) throw new Error('Allow read access to the chosen files to play.');
    }
    const registration = await navigator.serviceWorker.register('sw.js', {scope: './'});
    await navigator.serviceWorker.ready;
    registration.active?.postMessage('refresh');
    location.href = 'play/';
  } catch (error) {
    ui.status.textContent = String(error.message || error);
    ui.play.disabled = false;
  }
};

(async () => {
  if (!globalThis.showDirectoryPicker || !globalThis.showOpenFilePicker || !navigator.serviceWorker) {
    for (const button of [ui.build, ui.iso, ui.textures]) button.disabled = true;
    ui.status.textContent = 'This browser cannot open local folders. Use a current desktop Chrome or Edge, or ' +
      'Play-Browser.cmd in the DrakanPC-web folder.';
    return;
  }
  for (const key of ['build', 'iso', 'textures']) {
    try { chosen[key] = (await drakanHandles.get(key)) || null; } catch (_) { chosen[key] = null; }
  }
  refresh();
})();
