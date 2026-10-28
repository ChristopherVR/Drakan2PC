// Built when the DLSS module is off (GSR_DLSS=OFF, or no SDK found): the temporal pipeline then never has an upscaler.
// See docs/reference/dlss.md.
#include "temporal.h"

namespace gsr {
bool dlssAvailable(ID3D11Device*, std::string& status) {
    status = "DLSS support not compiled in (GSR_DLSS=OFF or SDK missing)";
    return false;
}
std::unique_ptr<Upscaler> createDlssUpscaler(ID3D11Device*, ID3D11DeviceContext*, const Upscaler::Desc&,
                                             std::string& status) {
    status = "DLSS support not compiled in";
    return nullptr;
}
void dlssShutdown(ID3D11Device*) {}
} // namespace gsr
