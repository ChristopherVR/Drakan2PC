// DLSS Super Resolution through the NVIDIA NGX SDK (github.com/NVIDIA/DLSS, v310.x), Direct3D 11. Windows / NVIDIA
// only. Optional module: built only when CMake option GSR_DLSS is ON and the SDK folder exists; the core renderer sees
// it only through gsr::Upscaler (temporal.h). See docs/reference/dlss.md.
#include "temporal.h"
#include "runtime_flags.h"

#include <dxgi.h>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_d3d.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace gsr {
namespace {
// Development project identifier. Register/confirm the project and distribution requirements with NVIDIA before
// shipping.
constexpr const char* kProjectId = "d1c2f0a3-6b7e-4d58-9a0e-3f5b2c8e7a41";

ID3D11Device* g_ngxDevice = nullptr;
bool g_ngxInitFailed = false;
std::wstring g_dllDir;
std::vector<const wchar_t*> g_pathList;

void ngxLog(const char* msg, NVSDK_NGX_Logging_Level lvl, NVSDK_NGX_Feature) {
    if (lvl == NVSDK_NGX_LOGGING_LEVEL_OFF)
        return;
    if (runtimeFlags().dlssLog)
        std::fprintf(stderr, "[ngx] %s", msg);
}

std::wstring exeDir() {
    wchar_t buf[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    std::wstring p = buf;
    const size_t sl = p.find_last_of(L"\\/");
    return sl == std::wstring::npos ? L"." : p.substr(0, sl);
}

bool fileExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// folder that contains nvngx_dlss.dll: DRAKAN_DLSS_DIR, the executable's folder, <exe>/dlss
std::wstring findDllDir() {
    std::vector<std::wstring> c;
    if (const char* e = cstr(runtimeFlags().dlssDir)) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, e, -1, nullptr, 0);
        std::wstring w(n > 0 ? n : 0, L'\0');
        if (n > 1) {
            MultiByteToWideChar(CP_UTF8, 0, e, -1, &w[0], n);
            w.resize(n - 1);
        }
        c.push_back(w);
    }
    c.push_back(exeDir());
    c.push_back(exeDir() + L"\\dlss");
    for (const std::wstring& d : c)
        if (!d.empty() && fileExists(d + L"\\nvngx_dlss.dll"))
            return d;
    return L"";
}

bool isNvidia(ID3D11Device* dev, std::string& name) {
    IDXGIDevice* dxgi = nullptr;
    if (!dev)
        return false;
    if (FAILED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi)))
        return false;
    IDXGIAdapter* ad = nullptr;
    bool nv = false;
    if (SUCCEEDED(dxgi->GetAdapter(&ad))) {
        DXGI_ADAPTER_DESC d{};
        ad->GetDesc(&d);
        nv = d.VendorId == 0x10DE;
        char nm[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, nm, sizeof(nm), nullptr, nullptr);
        name = nm;
        ad->Release();
    }
    dxgi->Release();
    return nv;
}

bool ngxEnsureInit(ID3D11Device* dev, std::string& status) {
    if (g_ngxDevice == dev)
        return true;
    if (g_ngxInitFailed) {
        status = "NGX init failed earlier";
        return false;
    }
    if (g_ngxDevice) {
        status = "DLSS SR already in use by another D3D11 device";
        return false;
    }
    g_dllDir = findDllDir();
    g_pathList.clear();
    if (!g_dllDir.empty())
        g_pathList.push_back(g_dllDir.c_str());
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = g_pathList.empty() ? nullptr : g_pathList.data();
    info.PathListInfo.Length = (unsigned int)g_pathList.size();
    info.LoggingInfo.LoggingCallback = ngxLog;
    info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
    info.LoggingInfo.DisableOtherLoggingSinks = true;
    // data path for NGX logs / caches
    std::wstring data = exeDir() + L"\\ngx_data";
    CreateDirectoryW(data.c_str(), nullptr);
    NVSDK_NGX_Result r =
        NVSDK_NGX_D3D11_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", data.c_str(), dev, &info);
    if (NVSDK_NGX_FAILED(r)) {
        char b[96];
        std::snprintf(b, sizeof b, "NVSDK_NGX_D3D11_Init failed (0x%08x)", (unsigned)r);
        status = b;
        g_ngxInitFailed = true;
        return false;
    }
    g_ngxDevice = dev;
    return true;
}

class DlssUpscaler final : public Upscaler {
public:
    ~DlssUpscaler() override {
        if (m_handle)
            NVSDK_NGX_D3D11_ReleaseFeature(m_handle);
        if (m_params)
            NVSDK_NGX_D3D11_DestroyParameters(m_params);
    }
    bool init(ID3D11Device* dev, ID3D11DeviceContext* ctx, const Desc& d, std::string& status) {
        m_ctx = ctx;
        m_d = d;
        NVSDK_NGX_Parameter* caps = nullptr;
        if (NVSDK_NGX_FAILED(NVSDK_NGX_D3D11_GetCapabilityParameters(&caps)) || !caps) {
            status = "NGX capability query failed";
            return false;
        }
        int avail = 0;
        caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &avail);
        if (!avail) {
            int needs = 0;
            caps->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs);
            NVSDK_NGX_D3D11_DestroyParameters(caps);
            status = needs ? "DLSS needs a newer NVIDIA driver"
                           : "DLSS Super Resolution is not available (is nvngx_dlss.dll next to the exe?)";
            return false;
        }
        NVSDK_NGX_D3D11_DestroyParameters(caps);
        if (NVSDK_NGX_FAILED(NVSDK_NGX_D3D11_AllocateParameters(&m_params)) || !m_params) {
            status = "NGX AllocateParameters failed";
            return false;
        }
        NVSDK_NGX_PerfQuality_Value pq = NVSDK_NGX_PerfQuality_Value_MaxQuality;
        const char* modeName = "Quality";
        switch (d.mode) {
            case 1:
                pq = NVSDK_NGX_PerfQuality_Value_DLAA;
                modeName = "DLAA";
                break;
            case 3:
                pq = NVSDK_NGX_PerfQuality_Value_Balanced;
                modeName = "Balanced";
                break;
            case 4:
                pq = NVSDK_NGX_PerfQuality_Value_MaxPerf;
                modeName = "Performance";
                break;
            default:
                break;
        }
        if (d.preset) {
            // pin the render preset (J = 10, K = 11, L = 12, M = 13) for every mode slot so the choice does not depend
            // on how NGX maps our mode
            for (const char* key :
                 {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                  NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
                  NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                  NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
                  NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality})
                NVSDK_NGX_Parameter_SetUI(m_params, key, d.preset);
        }
        NVSDK_NGX_DLSS_Create_Params cp{};
        cp.Feature.InWidth = d.renderW;
        cp.Feature.InHeight = d.renderH;
        cp.Feature.InTargetWidth = d.outW;
        cp.Feature.InTargetHeight = d.outH;
        cp.Feature.InPerfQualityValue = pq;
        cp.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure |
                                  (d.depthInverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0);
        cp.InEnableOutputSubrects = false;
        NVSDK_NGX_Result r = NGX_D3D11_CREATE_DLSS_EXT(ctx, &m_handle, m_params, &cp);
        if (NVSDK_NGX_FAILED(r)) {
            char b[96];
            std::snprintf(b, sizeof b, "DLSS CreateFeature failed (0x%08x)", (unsigned)r);
            status = b;
            return false;
        }
        unsigned long long vram = 0;
        (void)vram;
        char b[192];
        std::snprintf(b, sizeof b, "DLSS SR %s %ux%u -> %ux%u, preset %s (NGX SDK 310.9.16)", modeName, d.renderW,
                      d.renderH, d.outW, d.outH,
                      d.preset == 0 ? "default"
                                    : (d.preset == 10   ? "J"
                                       : d.preset == 11 ? "K"
                                       : d.preset == 12 ? "L"
                                       : d.preset == 13 ? "M"
                                                        : "?"));
        status = b;
        m_name = b;
        return true;
    }

    bool evaluate(const Inputs& in) override {
        NVSDK_NGX_D3D11_DLSS_Eval_Params ep{};
        ep.Feature.pInColor = in.color;
        ep.Feature.pInOutput = in.out;
        ep.Feature.InSharpness = 0.0f;
        ep.pInDepth = in.depth;
        ep.pInMotionVectors = in.mv;
        ep.InJitterOffsetX = in.jitterX;
        ep.InJitterOffsetY = in.jitterY;
        ep.InRenderSubrectDimensions.Width = m_d.renderW;
        ep.InRenderSubrectDimensions.Height = m_d.renderH;
        ep.InReset = in.reset ? 1 : 0;
        ep.InMVScaleX = in.mvScaleX;
        ep.InMVScaleY = in.mvScaleY;
        ep.InPreExposure = 1.0f;
        ep.InExposureScale = 1.0f;
        const NVSDK_NGX_Result r = NGX_D3D11_EVALUATE_DLSS_EXT(m_ctx, m_handle, m_params, &ep);
        if (NVSDK_NGX_FAILED(r)) {
            static int warned = 0;
            if (warned++ < 5)
                std::fprintf(stderr, "[dlss] EvaluateFeature failed 0x%08x\n", (unsigned)r);
            return false;
        }
        return true;
    }
    std::string name() const override { return m_name; }

private:
    ID3D11DeviceContext* m_ctx = nullptr;
    NVSDK_NGX_Handle* m_handle = nullptr;
    NVSDK_NGX_Parameter* m_params = nullptr;
    Desc m_d;
    std::string m_name;
};
} // namespace

bool dlssAvailable(ID3D11Device* dev, std::string& status) {
    std::string adapter;
    if (!isNvidia(dev, adapter)) {
        status = "DLSS unavailable: not an NVIDIA GPU (" + adapter + ")";
        return false;
    }
    if (findDllDir().empty()) {
        status = "DLSS unavailable: nvngx_dlss.dll not found next to the exe (or in DRAKAN_DLSS_DIR)";
        return false;
    }
    status = "DLSS SR candidate on " + adapter + "; NGX feature support not yet confirmed";
    return true;
}

std::unique_ptr<Upscaler> createDlssUpscaler(ID3D11Device* dev, ID3D11DeviceContext* ctx, const Upscaler::Desc& d,
                                             std::string& status) {
    if (!dlssAvailable(dev, status))
        return nullptr;
    if (!ngxEnsureInit(dev, status))
        return nullptr;
    auto u = std::make_unique<DlssUpscaler>();
    if (!u->init(dev, ctx, d, status))
        return nullptr;
    static bool credited = false;
    if (!credited) {
        credited = true;
        std::fprintf(stderr, "[dlss] Uses NVIDIA DLSS. NVIDIA, RTX and DLSS are trademarks of NVIDIA Corporation.\n");
    }
    return u;
}

void dlssShutdown(ID3D11Device* dev) {
    g_ngxInitFailed = false;
    if (g_ngxDevice && g_ngxDevice == dev) {
        NVSDK_NGX_D3D11_Shutdown1(dev);
        g_ngxDevice = nullptr;
    }
}

} // namespace gsr
