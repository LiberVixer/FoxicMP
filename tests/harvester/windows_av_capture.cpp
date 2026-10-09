// Output-side A/V timing probe; captures only the specified process tree's audio.
// GPL-3.0-or-later. Requires Windows SDK with process loopback support (20348+).
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>

using Microsoft::WRL::ComPtr;
class Activation final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IActivateAudioInterfaceCompletionHandler, Microsoft::WRL::FtmBase> {
public:
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT result = E_PENDING;
    ComPtr<IAudioClient> client;
    ~Activation() { CloseHandle(done); }
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        ComPtr<IUnknown> object;
        HRESULT activation = E_FAIL;
        result = op->GetActivateResult(&activation, &object);
        if (SUCCEEDED(result)) result = activation;
        if (SUCCEEDED(result)) result = object.As(&client);
        SetEvent(done);
        return S_OK;
    }
};
static void checked(HRESULT hr) {
    if (FAILED(hr)) { std::cerr << "HRESULT 0x" << std::hex << hr << "\n"; throw hr; }
}
static int64_t qpc() {
    LARGE_INTEGER value{}, frequency{};
    QueryPerformanceCounter(&value); QueryPerformanceFrequency(&frequency);
    return int64_t((long double)value.QuadPart * 10000000 / frequency.QuadPart);
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 6) {
        std::cerr << "av-capture PID HWND SECONDS OUTPUT.csv READY-file\n";
        return 2;
    }
    try {
        checked(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        const DWORD pid = wcstoul(argv[1], nullptr, 10);
        HWND window = (HWND)(uintptr_t)_wcstoui64(argv[2], nullptr, 10);
        DWORD actual = 0; GetWindowThreadProcessId(window, &actual);
        if (actual != pid || !IsWindowVisible(window)) {std::cerr << "Window unavailable: target=" << pid << " actual=" << actual << " visible=" << IsWindowVisible(window) << " handle=" << (uintptr_t)window << "\n";throw E_INVALIDARG;}
        const double seconds = _wtof(argv[3]);
        if (seconds <= 0 || seconds > 300) throw E_INVALIDARG;
        auto activation = Microsoft::WRL::Make<Activation>();
        AUDIOCLIENT_ACTIVATION_PARAMS params{};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId = pid;
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT property{}; property.vt = VT_BLOB;
        property.blob = {sizeof(params), (BYTE*)&params};
        ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
        checked(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
            __uuidof(IAudioClient), &property, activation.Get(), &operation));
        if (WaitForSingleObject(activation->done, 10000) != WAIT_OBJECT_0) throw HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        checked(activation->result);
        WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0};
        auto client = activation->client;
        checked(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
            0, 0, &format, nullptr));
        HANDLE audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        checked(client->SetEventHandle(audioEvent));
        ComPtr<IAudioCaptureClient> capture;
        checked(client->GetService(IID_PPV_ARGS(&capture)));
        std::ofstream output(argv[4]);
        if (!output) throw E_FAIL;
        output << "kind,start100ns,end100ns,value,flags\n";
        std::atomic<bool> stop{false}; std::atomic<HRESULT> audioError{S_OK};
        checked(client->Start());
        std::thread audio([&]() {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            while (!stop) {
                WaitForSingleObject(audioEvent, 100);
                UINT32 available = 0;
                while (SUCCEEDED(capture->GetNextPacketSize(&available)) && available) {
                    BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                    UINT64 device = 0, timestamp = 0;
                    HRESULT hr = capture->GetBuffer(&data, &frames, &flags, &device, &timestamp);
                    if (FAILED(hr)) { audioError = hr; stop = true; break; }
                    // Audio rows go to a separate stream: do not race the video writer.
                    static std::ofstream sound(std::wstring(argv[4]) + L".audio");
                    for (UINT32 begin = 0; begin < frames; begin += 44) {
                        const UINT32 end = std::min(frames, begin + 44);
                        int peak = 0;
                        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                            auto samples = (const int16_t*)data;
                            for (UINT32 i = begin * 2; i < end * 2; ++i) peak = std::max(peak, std::abs(int(samples[i])));
                        }
                        sound << "a," << timestamp + uint64_t(begin) * 10000000 / 44100 << ','
                              << timestamp + uint64_t(end) * 10000000 / 44100 << ',' << peak << ',' << flags << '\n';
                    }
                    capture->ReleaseBuffer(frames);
                }
            }
            CoUninitialize();
        });
        // Capture only 8x8 pixels in the video center; no desktop screenshots are saved.
        HDC desktop = GetDC(nullptr), memory = CreateCompatibleDC(desktop);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 8; info.bmiHeader.biHeight = -8;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(desktop, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        HGDIOBJ previous = SelectObject(memory, bitmap);
        std::ofstream(argv[5]) << "ready\n";
        const int64_t deadline = qpc() + int64_t(seconds * 10000000);
        while (!stop && qpc() < deadline && IsWindow(window)) {
            RECT rect{}; GetClientRect(window, &rect);
            POINT center{rect.right / 2 - 4, rect.bottom / 2 - 4}; ClientToScreen(window, &center);
            const auto before = qpc();
            DWORD visiblePid=0;
            GetWindowThreadProcessId(WindowFromPoint(center), &visiblePid);
            BOOL ok = visiblePid==pid && BitBlt(memory, 0, 0, 8, 8, desktop, center.x, center.y, SRCCOPY | CAPTUREBLT);
            GdiFlush(); const auto after = qpc();
            unsigned sum = 0;
            if (ok) for (unsigned i = 0; i < 64; ++i) sum += ((BYTE*)pixels)[i * 4];
            output << "v," << before << ',' << after << ',' << (ok ? double(sum) / 64 : -1) << ",0\n";
            Sleep(3);
        }
        stop = true; audio.join(); client->Stop(); CloseHandle(audioEvent);
        SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, desktop);
        checked(audioError); CoUninitialize();
        return 0;
    } catch (HRESULT hr) {std::cerr << "Capture failed 0x" << std::hex << hr << "\n";return 1;} catch (...) {std::cerr << "Capture exception\n";return 1;}
}
