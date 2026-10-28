// File System Access handles chosen by the player, kept in IndexedDB so the service worker can read the same files.
// Shared by loader.js (page) and sw.js (service worker).
const drakanHandles = (() => {
  const DB = 'drakan-pages';
  const STORE = 'handles';
  function open() {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open(DB, 1);
      request.onupgradeneeded = () => request.result.createObjectStore(STORE);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
  }
  async function run(mode, action) {
    const db = await open();
    try {
      return await new Promise((resolve, reject) => {
        const request = action(db.transaction(STORE, mode).objectStore(STORE));
        request.onsuccess = () => resolve(request.result);
        request.onerror = () => reject(request.error);
      });
    } finally {
      db.close();
    }
  }
  return {
    get: key => run('readonly', store => store.get(key)),
    set: (key, value) => run('readwrite', store => store.put(value, key)),
    remove: key => run('readwrite', store => store.delete(key)),
  };
})();
