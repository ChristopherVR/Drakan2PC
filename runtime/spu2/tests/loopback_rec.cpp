// loopback_rec: records what the default Windows render device plays (WASAPI loopback) into a 16-bit WAV and prints
// per-second RMS/peak. Used to verify that the game's audio really reaches the speakers.
//   loopback_rec.exe <seconds> <out.wav>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    const double secs = argc > 1 ? atof(argv[1]) : 10.0;
    const char* path = argc > 2 ? argv[2] : "loopback.wav";
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* ac = nullptr;
    IAudioCaptureClient* cc = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                (void**)&en)))
        return 1;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
        return 2;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac)))
        return 3;
    WAVEFORMATEX* wf = nullptr;
    ac->GetMixFormat(&wf);
    HRESULT hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, wf, nullptr);
    if (FAILED(hr)) {
        printf("init failed %08lx\n", (unsigned long)hr);
        return 4;
    }
    ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cc);
    const int ch = wf->nChannels;
    const unsigned rate = wf->nSamplesPerSec;
    const bool isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                         (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->wBitsPerSample == 32);
    printf("loopback: %u Hz, %d ch, %u bit, float=%d\n", rate, ch, wf->wBitsPerSample, (int)isFloat);
    std::vector<int16_t> pcm;
    ac->Start();
    const DWORD t0 = GetTickCount();
    double sumSq = 0, pk = 0;
    size_t n = 0;
    DWORD lastReport = t0;
    double allSq = 0;
    size_t allN = 0;
    while ((GetTickCount() - t0) < secs * 1000.0) {
        Sleep(10);
        UINT32 got = 0;
        while (SUCCEEDED(cc->GetNextPacketSize(&got)) && got) {
            BYTE* d;
            UINT32 frames;
            DWORD flags;
            if (FAILED(cc->GetBuffer(&d, &frames, &flags, nullptr, nullptr)))
                break;
            for (UINT32 i = 0; i < frames; ++i) {
                float l = 0, r = 0;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && isFloat) {
                    const float* f = (const float*)d + (size_t)i * ch;
                    l = f[0];
                    r = ch > 1 ? f[1] : f[0];
                }
                pcm.push_back((int16_t)std::fmax(-32768.0, std::fmin(32767.0, l * 32768.0)));
                pcm.push_back((int16_t)std::fmax(-32768.0, std::fmin(32767.0, r * 32768.0)));
                sumSq += (double)l * l + (double)r * r;
                n += 2;
                allSq += (double)l * l + (double)r * r;
                allN += 2;
                pk = std::fmax(pk, std::fmax(std::fabs(l), std::fabs(r)));
            }
            cc->ReleaseBuffer(frames);
        }
        if (GetTickCount() - lastReport >= 1000) {
            printf("t=%2lu s  rms=%.4f  peak=%.4f\n", (unsigned long)((GetTickCount() - t0) / 1000),
                   n ? std::sqrt(sumSq / n) : 0.0, pk);
            fflush(stdout);
            sumSq = 0;
            pk = 0;
            n = 0;
            lastReport = GetTickCount();
        }
    }
    ac->Stop();
    printf("TOTAL rms=%.4f over %.1f s\n", allN ? std::sqrt(allSq / allN) : 0.0, (double)allN / 2 / rate);
    FILE* f = fopen(path, "wb");
    if (f) {
        uint32_t dataBytes = (uint32_t)(pcm.size() * 2), riff = 36 + dataBytes, fmtLen = 16, srate = rate,
                 brate = rate * 4;
        uint16_t tag = 1, chn = 2, ba = 4, bits = 16;
        fwrite("RIFF", 1, 4, f);
        fwrite(&riff, 4, 1, f);
        fwrite("WAVEfmt ", 1, 8, f);
        fwrite(&fmtLen, 4, 1, f);
        fwrite(&tag, 2, 1, f);
        fwrite(&chn, 2, 1, f);
        fwrite(&srate, 4, 1, f);
        fwrite(&brate, 4, 1, f);
        fwrite(&ba, 2, 1, f);
        fwrite(&bits, 2, 1, f);
        fwrite("data", 1, 4, f);
        fwrite(&dataBytes, 4, 1, f);
        fwrite(pcm.data(), 2, pcm.size(), f);
        fclose(f);
    }
    return 0;
}
