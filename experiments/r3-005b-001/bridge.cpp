#include "protocol.hpp"
#include <wincodec.h>

namespace r3 {
namespace {
void hr(HRESULT status, const char* api) { if (FAILED(status)) throw Error(api, static_cast<DWORD>(status), "HRESULT"); }
template<class T> struct Com {
    T* value = nullptr;
    ~Com() { if (value) value->Release(); }
    T** address() { return &value; }
    T* operator->() const { return value; }
};
struct Surface {
    HDC dc = nullptr; HBITMAP bitmap = nullptr; HGDIOBJ old = nullptr; void* pixels = nullptr;
    Surface(int width, int height) {
        dc = CreateCompatibleDC(nullptr); check(dc != nullptr, "CreateCompatibleDC");
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap) { DeleteDC(dc); dc = nullptr; throw Error("CreateDIBSection", GetLastError()); }
        old = SelectObject(dc, bitmap);
        demand(old && old != HGDI_ERROR, "SelectObject failed");
        std::memset(pixels, 0, static_cast<std::size_t>(width) * height * 4);
    }
    ~Surface() { if (old && dc) SelectObject(dc, old); if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); }
};
void encode_png(const std::filesystem::path& output, int width, int height, const void* bgra) {
    // Encode the returned GDI RGB pixels. No controller-rendered application UI.
    const auto stride = static_cast<UINT>((width * 3 + 3) & ~3);
    std::vector<BYTE> bgr(static_cast<std::size_t>(stride) * height);
    const auto source = static_cast<const BYTE*>(bgra);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        std::memcpy(bgr.data() + static_cast<std::size_t>(y) * stride + x * 3,
            source + (static_cast<std::size_t>(y) * width + x) * 4, 3);
    const auto partial = output.wstring() + L".partial";
    hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");
    struct Uninitialize { ~Uninitialize() { CoUninitialize(); } } uninitialize;
    Com<IWICImagingFactory> factory;
    hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.address())), "CoCreateInstance.WIC");
    {
        Com<IWICStream> stream;
        hr(factory->CreateStream(stream.address()), "WIC.CreateStream");
        hr(stream->InitializeFromFilename(partial.c_str(), GENERIC_WRITE), "WIC.InitializeFromFilename");
        Com<IWICBitmapEncoder> encoder;
        hr(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.address()), "WIC.CreateEncoder");
        hr(encoder->Initialize(stream.value, WICBitmapEncoderNoCache), "WIC.EncoderInitialize");
        Com<IWICBitmapFrameEncode> frame;
        hr(encoder->CreateNewFrame(frame.address(), nullptr), "WIC.CreateNewFrame");
        hr(frame->Initialize(nullptr), "WIC.FrameInitialize");
        hr(frame->SetSize(width, height), "WIC.SetSize");
        auto format = GUID_WICPixelFormat24bppBGR;
        hr(frame->SetPixelFormat(&format), "WIC.SetPixelFormat");
        demand(IsEqualGUID(format, GUID_WICPixelFormat24bppBGR), "unexpected encoder pixel format");
        hr(frame->WritePixels(height, stride, static_cast<UINT>(bgr.size()), bgr.data()), "WIC.WritePixels");
        hr(frame->Commit(), "WIC.FrameCommit"); hr(encoder->Commit(), "WIC.EncoderCommit");
    }
    check(MoveFileExW(partial.c_str(), output.c_str(), MOVEFILE_WRITE_THROUGH), "MoveFileExW.publish_png");
}
HWND destination(const Telemetry& t, const std::wstring& name) {
    if (name == L"top") return reinterpret_cast<HWND>(t.top);
    if (name == L"edit") return reinterpret_cast<HWND>(t.edit);
    if (name == L"button") return reinterpret_cast<HWND>(t.button);
    if (name == L"drag") return reinterpret_cast<HWND>(t.drag);
    throw Error("destination", ERROR_INVALID_PARAMETER);
}
void validate(HWND window, const Telemetry& t) {
    DWORD pid = 0;
    const auto tid = GetWindowThreadProcessId(window, &pid);
    if (!IsWindow(window) || tid != t.tid || pid != t.pid)
        throw Error("validate_target_window", ERROR_INVALID_WINDOW_HANDLE, "exited or window identity mismatch");
    demand(thread_desktop() == t.desktop, "bridge not on fixture desktop");
}
}
int bridge_main(const Args& args) {
    Mapping state_map(args.get(L"--state"), sizeof(Shared), false);
    Mapping reply_map(args.get(L"--reply"), sizeof(Reply), false);
    const auto shared = static_cast<const Shared*>(state_map.view);
    const auto reply = static_cast<Reply*>(reply_map.view);
    const auto start = GetTickCount64();
    try {
        const auto desktop = thread_desktop(); wcsncpy_s(reply->desktop, desktop.c_str(), _TRUNCATE);
        const auto before = read_shared(*shared);
        demand(before.signature == magic && before.pid != 0, "fixture metadata not ready");
        const auto window = destination(before, args.get(L"--target", L"top"));
        validate(window, before);
        const auto op = args.get(L"--op");
        if (op == L"capture") {
            const auto output = std::filesystem::path(args.get(L"--path"));
            demand(!output.empty() && !std::filesystem::exists(output) && !std::filesystem::exists(output.wstring() + L".partial"), "capture output must be fresh");
            RECT rect{}; check(GetWindowRect(window, &rect), "GetWindowRect");
            const auto width = rect.right - rect.left, height = rect.bottom - rect.top;
            demand(width > 0 && height > 0 && width <= 2048 && height <= 2048, "capture dimensions invalid");
            Surface surface(width, height);
            reply->width = width; reply->height = height; reply->before_update = before.update; reply->before_prints = before.prints;
            reply->before_draws = before.draws;
            strcpy_s(reply->api, "PrintWindow");
            InterlockedExchange(reinterpret_cast<volatile LONG*>(&reply->print_called), 1);
            reply->api_begin_ms = GetTickCount64();
            SetLastError(ERROR_SUCCESS);
            const auto okay = PrintWindow(window, surface.dc, 0);
            const auto error = okay ? ERROR_SUCCESS : GetLastError();
            if(okay){
                // Explicit native window-tree paint pass. This is real target
                // rendering, not reconstruction from telemetry or saved text.
                DWORD_PTR print_result=0;
                if(!SendMessageTimeoutW(window,WM_PRINT,reinterpret_cast<WPARAM>(surface.dc),PRF_NONCLIENT|PRF_CLIENT|PRF_ERASEBKGND|PRF_CHILDREN,SMTO_ABORTIFHUNG|SMTO_BLOCK,2000,&print_result))throw Error("SendMessageTimeoutW.WM_PRINT",GetLastError());
                strcpy_s(reply->api,"PrintWindow+WM_PRINT_CHILDREN");
            }
            reply->api_end_ms = GetTickCount64();
            reply->api_result = okay;
            if (!okay) throw Error("PrintWindow", error ? error : ERROR_GEN_FAILURE, "no usable frame");
            const auto after = read_shared(*shared);
            reply->after_update = after.update; reply->after_prints = after.prints; reply->after_draws = after.draws;
            validate(window, after);
            POINT origin{}; check(ClientToScreen(window, &origin), "ClientToScreen");
            const int dx = origin.x - rect.left, dy = origin.y - rect.top;
            reply->client_dx = dx; reply->client_dy = dy;
            std::uint64_t drawn_tick = 0;
            for (int bit = 0; bit < 16; ++bit) {
                const int x = dx + 28 + bit * 10, y = dy + 356;
                demand(x >= 0 && y >= 0 && x < width && y < height, "visual counter outside surface");
                const auto pixel = static_cast<const BYTE*>(surface.pixels) + (static_cast<std::size_t>(y) * width + x) * 4;
                const bool white = pixel[0] > 240 && pixel[1] > 240 && pixel[2] > 240;
                const bool black = pixel[0] < 15 && pixel[1] < 15 && pixel[2] < 15;
                demand(white || black, "visual tick pixel invalid; frame rejected");
                if (white) drawn_tick |= 1ULL << bit;
            }
            reply->print_update = drawn_tick;
            // On this Windows build PrintWindow reenters WM_PAINT rather than
            // WM_PRINT. Require fresh target drawing plus the image's own tick.
            demand(after.draws > before.draws && drawn_tick >= before.update && drawn_tick <= after.update,
                "no fresh target drawing matching image tick; frame rejected");
            encode_png(output, width, height, surface.pixels);
        } else if (op == L"post") {
            strcpy_s(reply->api, "PostMessageW");
            check(PostMessageW(window, static_cast<UINT>(args.number(L"--message")),
                static_cast<WPARAM>(args.number(L"--wparam")), static_cast<LPARAM>(args.number(L"--lparam"))), "PostMessageW");
            reply->api_result = 1; // delivery accepted, NOT application completion
        } else if (op == L"send" || op == L"observe") {
            DWORD_PTR result = 0;
            const auto message = op == L"observe" ? observe_message : static_cast<UINT>(args.number(L"--message"));
            strcpy_s(reply->api, "SendMessageTimeoutW");
            SetLastError(ERROR_SUCCESS);
            if (!SendMessageTimeoutW(window, message, static_cast<WPARAM>(args.number(L"--wparam")),
                static_cast<LPARAM>(args.number(L"--lparam")), SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result)) {
                const auto error = GetLastError(); throw Error("SendMessageTimeoutW", error ? error : ERROR_TIMEOUT);
            }
            reply->api_result = 1;
        } else throw Error("bridge_operation", ERROR_INVALID_PARAMETER);
        reply->duration_ms = GetTickCount64() - start;
        MemoryBarrier(); InterlockedExchange(&reply->done, 1);
        return 0;
    } catch (const Error& e) {
        reply->error = e.code; strcpy_s(reply->api, e.api.c_str());
        reply->duration_ms = GetTickCount64() - start;
        MemoryBarrier(); InterlockedExchange(&reply->done, 1);
        return 2;
    }
}
}
