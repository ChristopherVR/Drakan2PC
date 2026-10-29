Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  if (ENVIRONMENT_IS_PTHREAD) return;
  ENV['DRAKAN_ISO'] = '/disc.iso';
  ENV['DRAKAN_ISO_URL'] = new URL('disc.iso', document.baseURI).href;
  ENV['DRAKAN_ELF'] = '/SCES_500.06';
  ENV['DRAKAN_INI'] = '/persist/drakan.ini';
  ENV['DRAKAN_INPUT_INI'] = '/persist/drakan_input.ini';
  ENV['DRAKAN_MEMCARD_DIR'] = '/persist/mc0';
  // The shared WebGPU renderer: its GPU thread owns the canvas (an OffscreenCanvas), so there is no WebGL fallback.
  ENV['PS2X_GS'] = 'gpu';
  // Private renderer comparison, with no additional player UI.
  if (new URL(document.baseURI).searchParams.get('gpu') === '1') ENV['DRAKAN_GPU_RASTER'] = '1';
  const gpuScale = new URL(document.baseURI).searchParams.get('gpuScale');
  if (gpuScale === '3' || gpuScale === '5') ENV['DRAKAN_GPU_SCALE'] = gpuScale;
  ENV['DRAKAN_VU_BATCH'] = '0';
  ENV['DRAKAN_VU_ASYNC'] = '0';
  // Read-only performance collection for the development harness; no player UI.
  if (new URL(document.baseURI).searchParams.get('metrics') === '1') {
    ENV['DRAKAN_FRAME_METRICS'] = '1';
    ENV['DRAKAN_FRAME_AUDIT'] = '0';
  }

  // Index only: images stream on a texture worker when requested, without
  // preloading an entire HD pack into the browser heap or blocking a draw.
  addRunDependency('texture-pack-index');
  fetch(new URL('texture-pack.json', document.baseURI)).then(response => {
    if (!response.ok) throw new Error('Texture pack index: ' + response.status);
    return response.json();
  }).then(index => {
    FS.mkdir('/textures');
    for (const name of index.files) {
      if (!/^[0-9a-f]{16}\.(png|dds)$/i.test(name)) continue;
      FS.writeFile('/textures/' + name, new Uint8Array());
    }
    for (const name of ['alias.txt', 'exclude.txt'])
      if (typeof index.metadata[name] === 'string') FS.writeFile('/textures/' + name, index.metadata[name]);
    ENV['DRAKAN_TEXTURE_DIR'] = '/textures';
    ENV['DRAKAN_TEXTURE_URL'] = new URL('textures/', document.baseURI).href;
  }).catch(error => Module['printErr']('HD textures unavailable: ' + error))
    .finally(() => removeRunDependency('texture-pack-index'));

  // Narration subtitles (subtitles/<intro|outro>_<language>.srt beside the build, as on native targets). The game
  // reads them from the executable directory, which is the virtual filesystem's root. Missing files mean no subtitles.
  addRunDependency('narration-subtitles');
  FS.mkdir('/subtitles');
  Promise.all(['english', 'french', 'german'].flatMap(language => ['intro', 'outro'].map(async part => {
    const name = part + '_' + language + '.srt';
    try {
      const response = await fetch(new URL('subtitles/' + name, document.baseURI));
      if (response.ok) FS.writeFile('/subtitles/' + name, new Uint8Array(await response.arrayBuffer()));
    } catch (error) {
      // Optional asset; the runtime logs a missing track when the narration starts.
    }
  }))).finally(() => removeRunDependency('narration-subtitles'));

  // File-close persistence, periodic recovery and exit share one queue. Never
  // replace browser storage with an empty filesystem after a failed restore.
  let restored = false;
  let syncQueue = Promise.resolve();
  let pending = 0;
  let automatic;
  function reportSaveError(error) {
    Module['printErr']('Cannot persist browser saves: ' + error);
    Module['onSaveError']?.(error);
  }
  Module['drakanSync'] = function () {
    if (!restored) return Promise.reject(new Error('Saved games have not been restored'));
    ++pending;
    const task = syncQueue.then(async function () {
      for (let attempt = 0; ; ++attempt) {
        try {
          await new Promise(function (resolve, reject) {
            FS.syncfs(false, function (error) { if (error) reject(error); else resolve(); });
          });
          return;
        } catch (error) {
          // Settings replace their .tmp files atomically. IDBFS enumerates
          // before awaiting IndexedDB, so a rename in that gap can make its
          // later read return Emscripten ENOENT (44). Take a fresh snapshot;
          // quota, permission and database failures still reach the player.
          if (error?.errno !== 44 || attempt >= 3) throw error;
          await new Promise(resolve => setTimeout(resolve, 16));
        }
      }
    });
    syncQueue = task.then(function () { --pending; }, function () { --pending; });
    return task;
  };
  const originalQueuePersist = IDBFS.queuePersist;
  IDBFS.queuePersist = function (mount) {
    if (mount.mountpoint !== '/persist') return originalQueuePersist(mount);
    if (!restored || automatic !== undefined) return;
    automatic = setTimeout(function () {
      automatic = undefined;
      Module['drakanSync']().catch(reportSaveError);
    }, 0);
  };
  FS.mkdir('/persist');
  FS.mount(IDBFS, {autoPersist:true}, '/persist');
  addRunDependency('restore-saves');
  FS.syncfs(true, function (error) {
    if (error) { Module['printErr']('Cannot restore browser saves: ' + error); abort(error); }
    restored = true;
    removeRunDependency('restore-saves');
  });
  setInterval(function () {
    if (!restored || pending) return;
    Module['drakanSync']().catch(reportSaveError);
  }, 3000);
  window.addEventListener('pagehide', function () {
    if (restored) Module['drakanSync']().catch(function () {});
  });
});
