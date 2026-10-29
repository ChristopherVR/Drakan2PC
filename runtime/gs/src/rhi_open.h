// Device creation paths shared by rhi_webgpu.cpp (the device) and rhi_proxy.cpp (the threaded proxy).
#pragma once
#include "rhi.h"

#include <functional>
#include <memory>
#include <string>

namespace gsr::rhi {

using OpenCallback = std::function<void(std::unique_ptr<Device> device, const std::string& error)>;
// The device itself, on the calling thread. Natively `done` runs before this returns; in the browser it runs from this
// thread's event loop once the adapter and device requests resolve.
void openDirectAsync(const OpenDesc& desc, OpenCallback done);
// A proxy whose device lives on a dedicated GPU thread (rhi_proxy.cpp). Blocks until that device exists.
std::unique_ptr<Device> openProxy(const OpenDesc& desc, std::string* error);

} // namespace gsr::rhi
