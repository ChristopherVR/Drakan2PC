#include "hle/IsoFs.h"
#include <emscripten.h>
#include <cassert>
#include <cstdlib>
#include <thread>
#include <array>
#include <cstdio>

// Exercise the production EM_JS source in a real pthread, including a transport
// exception, cache reuse, a permanent failure and rejected full-file responses.
int main() {
    setenv("DRAKAN_ISO_URL", "http://127.0.0.1/disc.iso", 1);
    std::thread worker([] {
        // clang-format off
        EM_ASM({
            globalThis.testCalls = 0;
            globalThis.testMode = 0;
            globalThis.XMLHttpRequest = class {
                open() {}
                setRequestHeader() {}
                send() {
                    ++globalThis.testCalls;
                    if (globalThis.testMode === 1 || globalThis.testCalls === 1)
                        throw new Error('synthetic transport reset');
                    this.status = globalThis.testMode === 2 ? 200 : 206;
                    this.response = new Uint8Array(1024 * 1024).fill(37).buffer;
                }
            };
        });
        // clang-format on
        auto source = hle::openIsoSource("/disc.iso", nullptr);
        assert(source);
        std::array<unsigned char, 100> data{};
        assert(source->readAt(10, data.data(), data.size()));
        for (auto byte : data)
            assert(byte == 37);
        assert(EM_ASM_INT({ return globalThis.testCalls; }) == 2);
        assert(source->readAt(20, data.data(), data.size()));
        assert(EM_ASM_INT({ return globalThis.testCalls; }) == 2);
        EM_ASM({ globalThis.testMode = 1; });
        assert(!source->readAt(1024 * 1024, data.data(), data.size()));
        assert(EM_ASM_INT({ return globalThis.testCalls; }) == 5);
        EM_ASM({ globalThis.testMode = 2; });
        assert(!source->readAt(1024 * 1024, data.data(), data.size()));
        assert(EM_ASM_INT({ return globalThis.testCalls; }) == 6);
    });
    worker.join();
    std::puts("Browser ISO transport retry, cache and failure handling passed");
}
