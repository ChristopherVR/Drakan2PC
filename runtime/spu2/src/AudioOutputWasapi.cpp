#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "spu2/AudioOutput.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace spu2 {

namespace {

template <class T>
void safeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// PKEY_Device_FriendlyName {a45c254e-df1c-4efd-8020-67d146a850e0}, 14 (spelled out to avoid linking propsys/uuid
// property tables)
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

std::string utf8(const wchar_t* w) {
    if (!w)
        return {};
    char buf[512];
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof buf, nullptr, nullptr);
    return n > 0 ? std::string(buf) : std::string();
}

std::string lowerStr(std::string s) {
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string friendlyName(IMMDevice* dev) {
    std::string out;
    IPropertyStore* ps = nullptr;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps)) && ps) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR)
            out = utf8(v.pwszVal);
        PropVariantClear(&v);
        ps->Release();
    }
    return out;
}

std::string deviceId(IMMDevice* dev) {
    LPWSTR id = nullptr;
    std::string out;
    if (dev && SUCCEEDED(dev->GetId(&id)) && id) {
        out = utf8(id);
        CoTaskMemFree(id);
    }
    return out;
}

// Picks the endpoint: the Windows default render device, or the first active one whose friendly name contains `want`.
IMMDevice* pickDevice(IMMDeviceEnumerator* en, const std::string& want) {
    IMMDevice* dev = nullptr;
    const std::string w = lowerStr(want);
    if (!w.empty() && w != "default") {
        IMMDeviceCollection* col = nullptr;
        if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
            UINT n = 0;
            col->GetCount(&n);
            for (UINT i = 0; i < n && !dev; ++i) { // exact endpoint id first
                IMMDevice* d = nullptr;
                if (SUCCEEDED(col->Item(i, &d)) && d) {
                    if (lowerStr(deviceId(d)) == w)
                        dev = d;
                    else
                        d->Release();
                }
            }
            for (UINT i = 0; i < n && !dev; ++i) {
                IMMDevice* d = nullptr;
                if (SUCCEEDED(col->Item(i, &d)) && d) {
                    if (lowerStr(friendlyName(d)).find(w) != std::string::npos)
                        dev = d;
                    else
                        d->Release();
                }
            }
            col->Release();
        }
        if (dev)
            return dev;
        std::fprintf(stderr, "[audio] device '%s' not found, using the default device\n", want.c_str());
    }
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
        dev = nullptr;
    return dev;
}

struct Stream {
    IMMDevice* dev = nullptr;
    IAudioClient* ac = nullptr;
    IAudioRenderClient* rc = nullptr;
    HANDLE ev = nullptr;
    UINT32 bufFrames = 0;
    // device mix format (only used when our own 48 kHz float format was refused)
    bool nativeFormat = false;
    unsigned devRate = 48000, devChannels = 2;
    bool devFloat = true;
    int devBits = 32;
    std::string name;
    std::string id;
    void close() {
        if (ac)
            ac->Stop();
        safeRelease(rc);
        safeRelease(ac);
        safeRelease(dev);
        if (ev) {
            CloseHandle(ev);
            ev = nullptr;
        }
    }
};

bool openStream(IMMDeviceEnumerator* en, const std::string& want, Stream& s, std::string& err) {
    auto fail = [&](const char* what, HRESULT hr) {
        char b[128];
        snprintf(b, sizeof b, "%s failed (hr=0x%08lx)", what, (unsigned long)hr);
        err = b;
        s.close();
        return false;
    };
    s.dev = pickDevice(en, want);
    if (!s.dev)
        return fail("no render endpoint", E_FAIL);
    s.name = friendlyName(s.dev);
    s.id = deviceId(s.dev);
    HRESULT hr = s.dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&s.ac);
    if (FAILED(hr))
        return fail("Activate", hr);
    s.ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    WAVEFORMATEX* mix = nullptr;
    if (SUCCEEDED(s.ac->GetMixFormat(&mix)) && mix) {
        s.devRate = mix->nSamplesPerSec;
        s.devChannels = mix->nChannels;
        s.devBits = mix->wBitsPerSample;
        s.devFloat = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                     (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->wBitsPerSample == 32);
    }
    // 1) our own format (48 kHz stereo float); the audio engine converts to whatever the endpoint really is
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wf.nChannels = 2;
    wf.nSamplesPerSec = 48000;
    wf.wBitsPerSample = 32;
    wf.nBlockAlign = 8;
    wf.nAvgBytesPerSec = 48000 * 8;
    wf.cbSize = 0;
    hr = s.ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                          AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                              AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                          300000 /*30 ms*/, 0, &wf, nullptr);
    if (FAILED(hr) && mix) {
        // 2) the endpoint's own mix format; we convert (linear resampling, stereo -> first two channels)
        s.ac->Release();
        s.ac = nullptr;
        if (FAILED(s.dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&s.ac))) {
            CoTaskMemFree(mix);
            return fail("Activate(2)", hr);
        }
        hr = s.ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 300000, 0, mix, nullptr);
        if (SUCCEEDED(hr) && (s.devFloat || s.devBits == 16))
            s.nativeFormat = true;
        else if (SUCCEEDED(hr))
            hr = E_FAIL;
    }
    if (mix)
        CoTaskMemFree(mix);
    if (FAILED(hr))
        return fail("IAudioClient::Initialize", hr);
    hr = s.ac->SetEventHandle(s.ev);
    if (FAILED(hr))
        return fail("SetEventHandle", hr);
    hr = s.ac->GetBufferSize(&s.bufFrames);
    if (FAILED(hr))
        return fail("GetBufferSize", hr);
    hr = s.ac->GetService(__uuidof(IAudioRenderClient), (void**)&s.rc);
    if (FAILED(hr))
        return fail("GetService", hr);
    BYTE* p = nullptr;
    if (SUCCEEDED(s.rc->GetBuffer(s.bufFrames, &p)))
        s.rc->ReleaseBuffer(s.bufFrames, AUDCLNT_BUFFERFLAGS_SILENT); // prime with silence
    hr = s.ac->Start();
    if (FAILED(hr))
        return fail("Start", hr);
    return true;
}

} // namespace

std::vector<OutputDevice> AudioOutput::enumerateDevices() {
    std::vector<OutputDevice> out;
    const HRESULT coh = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(coh) && coh != RPC_E_CHANGED_MODE)
        return out;
    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                   (void**)&en)) &&
        en) {
        std::string defId;
        IMMDevice* dd = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dd)) && dd) {
            defId = deviceId(dd);
            dd->Release();
        }
        IMMDeviceCollection* col = nullptr;
        if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
            UINT n = 0;
            col->GetCount(&n);
            for (UINT i = 0; i < n; ++i) {
                IMMDevice* d = nullptr;
                if (SUCCEEDED(col->Item(i, &d)) && d) {
                    OutputDevice od;
                    od.id = deviceId(d);
                    od.name = friendlyName(d);
                    od.isDefault = !defId.empty() && od.id == defId;
                    out.push_back(od);
                    d->Release();
                }
            }
            col->Release();
        }
        en->Release();
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const OutputDevice& a, const OutputDevice& b) { return a.isDefault && !b.isDefault; });
    if (SUCCEEDED(coh))
        CoUninitialize();
    return out;
}

void AudioOutput::threadMain() {
    const HRESULT coh = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en);
    using Clock = std::chrono::steady_clock;
    std::vector<float> tmp(2 * 4800);
    std::vector<float> conv;
    auto nextTry = Clock::now();
    std::string lastErr;

    while (!stop_) {
        Stream s;
        bool opened = false;
        const unsigned gen = devGen_.load();
        std::string want;
        {
            std::lock_guard<std::mutex> g(devM_);
            want = device_;
        }
        if (opt_.useDevice && en && Clock::now() >= nextTry) {
            std::string e;
            opened = openStream(en, want, s, e);
            if (!opened) {
                err_ = e;
                if (e != lastErr)
                    std::fprintf(
                        stderr,
                        "[audio] cannot open the output device: %s (keeping the SPU2 running silently, retrying)\n",
                        e.c_str());
                lastErr = e;
                nextTry = Clock::now() + std::chrono::seconds(1);
            }
        }
        if (!opened) {
            // no device (or not wanted): drive the SPU2 on a wall clock, 5 ms chunks, and look for a device again later
            running_ = false;
            ready_ = true; // start() returns now; running_ tells the outcome
            auto next = Clock::now();
            while (!stop_ && !(opt_.useDevice && Clock::now() >= nextTry)) {
                if (devGen_.load() != gen) {
                    nextTry = Clock::now();
                    break;
                } // another device was requested: try it now
                renderBlock(tmp.data(), 240);
                next += std::chrono::milliseconds(5);
                std::this_thread::sleep_until(next);
                if (Clock::now() > next + std::chrono::milliseconds(100))
                    next = Clock::now(); // do not burst after a stall
            }
            continue;
        }

        rate_ = s.nativeFormat ? s.devRate : 48000u;
        {
            while (nameLock_.test_and_set(std::memory_order_acquire)) {
            }
            name_ = s.name;
            nameLock_.clear(std::memory_order_release);
        }
        {
            std::lock_guard<std::mutex> g(devM_);
            curId_ = s.id;
        }
        std::fprintf(stderr, "[audio] WASAPI shared: '%s' (%s), engine %s, volume %.0f%%\n", s.name.c_str(),
                     want == "default" ? "default device" : want.c_str(),
                     s.nativeFormat ? "native mix format (own resampler)" : "48 kHz float via AUTOCONVERTPCM",
                     gain_.load() * 100.0f);
        lastErr.clear();
        running_ = true;
        ready_ = true;
        err_.clear();
        ++reopens_;
        auto lastCheck = Clock::now();
        double srcPos = 0;
        std::vector<float> src48; // for the native-format fallback
        bool deviceLost = false;
        while (!stop_ && !deviceLost) {
            if (WaitForSingleObject(s.ev, 100) != WAIT_OBJECT_0) {
                // no buffer event: either a hiccup or a dead stream; check cheaply that the endpoint is still there
                UINT32 pad = 0;
                const HRESULT hp = s.ac->GetCurrentPadding(&pad);
                if (FAILED(hp)) {
                    deviceLost = true;
                    break;
                }
            }
            if (devGen_.load() != gen) {
                std::fprintf(stderr, "[audio] output device change requested, switching\n");
                nextTry = Clock::now();
                break;
            }
            if (want == "default" && Clock::now() - lastCheck > std::chrono::seconds(1)) {
                lastCheck = Clock::now();
                IMMDevice* d = nullptr;
                if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &d)) && d) {
                    const std::string id = deviceId(d);
                    d->Release();
                    if (!id.empty() && id != s.id) {
                        std::fprintf(stderr, "[audio] the default output device changed, switching\n");
                        deviceLost = true;
                        break;
                    }
                }
            }
            UINT32 pad = 0;
            if (FAILED(s.ac->GetCurrentPadding(&pad))) {
                deviceLost = true;
                break;
            }
            UINT32 n = s.bufFrames - pad;
            if (!n)
                continue;
            BYTE* p = nullptr;
            if (FAILED(s.rc->GetBuffer(n, &p))) {
                deviceLost = true;
                break;
            }
            if (!s.nativeFormat) {
                renderBlock((float*)p, n);
            } else {
                // render n * 48000/devRate frames of 48 kHz and resample linearly into the device format
                const double ratio = 48000.0 / (double)s.devRate;
                const size_t need = (size_t)std::ceil(n * ratio) + 2;
                src48.resize(need * 2);
                renderBlock(src48.data(), need); // gain + dump + levels once, before resampling
                for (UINT32 i = 0; i < n; ++i) {
                    const double pos = srcPos + i * ratio;
                    const size_t i0 = (size_t)pos;
                    const float fr = (float)(pos - (double)i0);
                    for (unsigned c = 0; c < s.devChannels; ++c) {
                        const unsigned sc = c < 2 ? c : 0;
                        float v =
                            c < 2 ? (src48[i0 * 2 + sc] * (1 - fr) + src48[std::min(i0 + 1, need - 1) * 2 + sc] * fr)
                                  : 0.0f;
                        if (s.devFloat)
                            ((float*)p)[(size_t)i * s.devChannels + c] = v;
                        else
                            ((int16_t*)p)[(size_t)i * s.devChannels + c] =
                                (int16_t)std::lrintf(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f);
                    }
                }
            }
            s.rc->ReleaseBuffer(n, 0);
        }
        running_ = false;
        s.close();
        {
            std::lock_guard<std::mutex> g(devM_);
            curId_.clear();
        }
        if (deviceLost && !stop_) {
            std::fprintf(stderr, "[audio] output device lost, reopening\n");
            nextTry = Clock::now() + std::chrono::milliseconds(300);
        }
    }
    running_ = false;
    safeRelease(en);
    if (SUCCEEDED(coh))
        CoUninitialize();
}

} // namespace spu2
