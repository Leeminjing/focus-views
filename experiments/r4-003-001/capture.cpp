#include "protocol.hpp"

namespace r305 {
namespace {
struct Surface {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;void* pixels=nullptr;
    ~Surface(){if(old&&dc)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
};
}
void capture_owned_window(Metadata& metadata,HWND top){
    // No cross-process HDC: the responsive owner thread creates, renders, and
    // flushes its own surface. Only real rendered bytes cross the IPC boundary.
    Mapping mapping(frame_name(GetCurrentProcessId(),GetCurrentThreadId()),sizeof(NativeFrame),false);
    auto& frame=*static_cast<NativeFrame*>(mapping.view);
    try{
        demand(metadata.tid==GetCurrentThreadId()&&frame.pid==GetCurrentProcessId()&&frame.tid==GetCurrentThreadId()&&frame.token&&!frame.done,"owned frame request identity");
        DWORD pid=0;demand(GetWindowThreadProcessId(top,&pid)==frame.tid&&pid==frame.pid,"owned native frame window");
        RECT rect{};check(GetWindowRect(top,&rect),"GetWindowRect.native_frame");
        frame.width=rect.right-rect.left;frame.height=rect.bottom-rect.top;
        demand(frame.width>0&&frame.height>0&&frame.width<=frame_limit&&frame.height<=frame_limit,"native frame dimensions");
        POINT origin{};check(ClientToScreen(top,&origin),"ClientToScreen.native_frame");
        frame.client_dx=origin.x-rect.left;frame.client_dy=origin.y-rect.top;
        Surface surface;surface.dc=CreateCompatibleDC(nullptr);check(surface.dc!=nullptr,"CreateCompatibleDC.native_frame");
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=frame.width;info.bmiHeader.biHeight=-frame.height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        surface.bitmap=CreateDIBSection(surface.dc,&info,DIB_RGB_COLORS,&surface.pixels,nullptr,0);check(surface.bitmap!=nullptr,"CreateDIBSection.native_frame");
        surface.old=SelectObject(surface.dc,surface.bitmap);demand(surface.old&&surface.old!=HGDI_ERROR,"select native frame bitmap");
        memset(surface.pixels,0,static_cast<size_t>(frame.width)*frame.height*4);
        frame.begin_ms=GetTickCount64();
        SendMessageW(top,WM_PRINT,reinterpret_cast<WPARAM>(surface.dc),PRF_NONCLIENT|PRF_CLIENT|PRF_ERASEBKGND|PRF_CHILDREN);
        check(GdiFlush(),"GdiFlush.native_frame");
        frame.end_ms=GetTickCount64();
        memcpy(frame.pixels,surface.pixels,static_cast<size_t>(frame.width)*frame.height*4);
        strcpy_s(frame.api,"owner-GUI-thread/WM_PRINT/native-DIB");
    }catch(const Error& e){frame.error=e.code;strncpy_s(frame.api,e.api.c_str(),_TRUNCATE);}
    MemoryBarrier();InterlockedExchange(&frame.done,1);
}
}
