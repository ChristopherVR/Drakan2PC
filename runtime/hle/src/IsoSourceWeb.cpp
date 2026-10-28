// Runs in the application's pthreads, never on the browser UI thread. Only the
// user-selected ISO exposed by the loopback server is accessible through this source.
#include "hle/IsoFs.h"
#include <emscripten.h>
#include <cstdlib>

// clang-format off
EM_JS(int, drakanReadDiscRange, (const char* url, double offset, void* output, unsigned size), {
    if (!ENVIRONMENT_IS_PTHREAD)
        throw new Error('Disc I/O must run in a game worker');
    const blockSize = 1024 * 1024;
    const cache = globalThis.drakanDiscCache || (globalThis.drakanDiscCache = new Map());
    let copied = 0;
    while (copied < size) {
        const position = offset + copied;
        const base = Math.floor(position / blockSize) * blockSize;
        let bytes = cache.get(base);
        if (!bytes) {
            // A reset of a reused loopback connection must not throw through the
            // guest worker. Retry transient transport failures, bounded to three.
            for (let attempt = 0; attempt < 3 && !bytes; ++attempt) {
                try {
                    const request = new XMLHttpRequest();
                    request.open('GET', UTF8ToString(url), false);
                    request.responseType = 'arraybuffer';
                    request.setRequestHeader('Range', 'bytes=' + base + '-' + (base + blockSize - 1));
                    request.send();
                    if (request.status !== 206 || !request.response)
                        return 0;
                    bytes = new Uint8Array(request.response);
                } catch (error) {
                    if (attempt === 2) {
                        console.error('Cannot read the local ISO range at ' + base + ': ' + error);
                        return 0;
                    }
                    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 20 * (attempt + 1));
                }
            }
            cache.set(base, bytes);
            if (cache.size > 16)
                cache.delete(cache.keys().next().value);
        }
        const within = position - base;
        const count = Math.min(size - copied, bytes.length - within);
        if (count <= 0)
            return 0;
        HEAPU8.set(bytes.subarray(within, within + count), output + copied);
        copied += count;
    }
    return 1;
});

// clang-format on

namespace hle {
namespace {
class WebIsoSource final : public IsoSource {
    std::string url_;

public:
    explicit WebIsoSource(std::string url) : url_(std::move(url)) {}
    bool readAt(u64 offset, void* destination, size_t size) const override {
        return offset < (1ull << 53) && size <= 0x7fffffffu &&
               drakanReadDiscRange(url_.c_str(), static_cast<double>(offset), destination, static_cast<unsigned>(size));
    }
};
} // namespace
std::shared_ptr<const IsoSource> openIsoSource(const std::string& path, std::string* error) {
    if (path != "/disc.iso") {
        if (error)
            *error = "Browser disc path must be /disc.iso";
        return {};
    }
    const char* url = std::getenv("DRAKAN_ISO_URL");
    if (!url || !url[0]) {
        if (error)
            *error = "The browser launcher did not set the disc URL";
        return {};
    }
    return std::make_shared<WebIsoSource>(url);
}
} // namespace hle
