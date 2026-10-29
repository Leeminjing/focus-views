#include "protocol.hpp"
#include "../r5-001a/runtime/processes.hpp"
#include <wincodec.h>
#include <memory>
using namespace fv_live;
template<class T>struct Com {T* p=nullptr;~Com(){if(p)p->Release();}};
void hr(HRESULT value,const char* api){if(FAILED(value))throw Error(api,static_cast<DWORD>(value));}
struct App {
    bool host=false,valid=false,show=true;Control* control=nullptr;r305::Log& log;
    std::unique_ptr<ReadOnlyFeed> feed;Handle manager;Com<IWICImagingFactory> wic;
    HWND top=nullptr,edit=nullptr,confirmation=nullptr;WNDPROC edit_proc=nullptr;
    std::vector<BYTE> pixels;UINT width=0,height=0;Frame frame{};std::uint64_t last_sequence=0;
    explicit App(r305::Log& l):log(l){}
    void record_host(UINT message){InterlockedIncrement(&control->host_epoch);if(message==WM_CHAR)++control->characters;if(message==WM_KEYDOWN)++control->keys;if(message==WM_MOUSEMOVE)++control->moves;if(message==WM_LBUTTONDOWN)++control->clicks;MemoryBarrier();InterlockedIncrement(&control->host_epoch);}
    static LRESULT CALLBACK edit_window(HWND w,UINT m,WPARAM a,LPARAM b){auto* self=reinterpret_cast<App*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(self){self->record_host(m);return CallWindowProcW(self->edit_proc,w,m,a,b);}return DefWindowProcW(w,m,a,b);}
    void confirm_host(){
        wchar_t text[128]{};GetWindowTextW(edit,text,128);
        InterlockedIncrement(&control->host_epoch);control->matched=!wcscmp(text,control->challenge);
        control->no_interference=SendMessageW(confirmation,BM_GETCHECK,0,0)==BST_CHECKED;
        control->confirmed=1;control->confirmed_tick=GetTickCount64();MemoryBarrier();InterlockedIncrement(&control->host_epoch);
        log.emit("host_operator_confirmation","\"challengeMatched\":"+std::string(control->matched?"true":"false")+",\"noInterferenceReported\":"+(control->no_interference?"true":"false")+",\"characters\":"+std::to_string(control->characters)+",\"keys\":"+std::to_string(control->keys)+",\"moves\":"+std::to_string(control->moves)+",\"clicks\":"+std::to_string(control->clicks)+",\"noRawInputTextRecorded\":true");
    }
    void clear(const char* why){if(valid){log.emit("preview_unavailable","\"reason\":"+json(why)+",\"oldFrameIsNotFresh\":true");}valid=false;pixels.clear();++control->invalid_frames;InvalidateRect(top,nullptr,TRUE);}
    void load(const Frame& next){
        if(next.pid==0||next.created==0||!next.sequence||next.end<next.begin||next.end>GetTickCount64()||GetTickCount64()-next.end>fresh_ms){clear("frame-age-or-identity");return;}
        const auto before=native::digest_text(native::digest(native::read_file(next.path)));if(before!=next.sha256)throw Error("preview.frame_hash",ERROR_CRC);
        Com<IWICBitmapDecoder> decoder;hr(wic.p->CreateDecoderFromFilename(next.path,nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder.p),"WIC.preview.decode");
        Com<IWICBitmapFrameDecode> source;hr(decoder.p->GetFrame(0,&source.p),"WIC.preview.frame");
        Com<IWICFormatConverter> convert;hr(wic.p->CreateFormatConverter(&convert.p),"WIC.preview.converter");hr(convert.p->Initialize(source.p,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"WIC.preview.convert");
        UINT w=0,h=0;hr(convert.p->GetSize(&w,&h),"WIC.preview.size");demand(w&&h&&w<=2048&&h<=2048,"bounded preview dimensions");
        std::vector<BYTE> next_pixels(static_cast<size_t>(w)*h*4);hr(convert.p->CopyPixels(nullptr,w*4,static_cast<UINT>(next_pixels.size()),next_pixels.data()),"WIC.preview.pixels");
        if(native::digest_text(native::digest(native::read_file(next.path)))!=before)throw Error("preview.frame_changed_during_read",ERROR_CRC);
        if(GetTickCount64()-next.end>fresh_ms){clear("decode-completed-after-expiry");return;}
        pixels=std::move(next_pixels);width=w;height=h;frame=next;valid=true;last_sequence=next.sequence;InvalidateRect(top,nullptr,TRUE);UpdateWindow(top);
    }
    void tick(){
        auto& c=*control;MemoryBarrier();const auto request=c.request;
        if(request!=c.applied){if(c.operation==2){InterlockedExchange(&c.applied,request);PostMessageW(top,WM_CLOSE,0,0);return;}if(c.operation==1){demand(c.argument>=0&&c.argument<3,"preview selection");c.selected=c.argument;last_sequence=0;}MemoryBarrier();InterlockedExchange(&c.applied,request);}
        if(host)return;
        Feed copy{};if(!read_feed(feed->get(),copy)){clear("incomplete-feed");return;}
        if(WaitForSingleObject(manager.value,0)!=WAIT_TIMEOUT||!copy.valid||GetTickCount64()<copy.heartbeat||GetTickCount64()-copy.heartbeat>fresh_ms){clear("producer-dead-invalid-or-expired");return;}
        const auto index=c.selected;demand(index>=0&&index<3,"selected Frame slot");const auto& next=copy.frames[index];
        if(valid&&GetTickCount64()-frame.end>fresh_ms)clear("displayed-frame-expired");
        if(next.sequence!=last_sequence)load(next);
    }
    LRESULT dispatch(HWND w,UINT m,WPARAM a,LPARAM b){
        if(host)record_host(m);
        if(m==WM_COMMAND){const auto id=LOWORD(a);if(host&&id==202){confirm_host();return 0;}if(!host&&id>=100&&id<103){control->selected=id-100;last_sequence=0;tick();return 0;}}
        if(m==WM_TIMER){try{tick();}catch(const Error& e){control->error=static_cast<LONG>(e.code);log.emit("aux_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));if(!host)clear(e.api.c_str());}return 0;}
        if(m==WM_MOUSEACTIVATE&&!host)return MA_NOACTIVATE;
        if(m==WM_PAINT){if(valid&&GetTickCount64()-frame.end>fresh_ms){valid=false;pixels.clear();}PAINTSTRUCT ps{};const auto dc=BeginPaint(w,&ps);RECT r{};GetClientRect(w,&r);FillRect(dc,&r,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));SetBkMode(dc,TRANSPARENT);
            if(!host){std::wstring title=L"No fresh Frame / producer unavailable";if(valid){title=std::wstring(frame.view,frame.view+strlen(frame.view))+L" pid="+std::to_wstring(frame.pid)+L" instance="+std::to_wstring(frame.created)+L" Frame="+std::to_wstring(frame.sequence)+L" capture="+std::to_wstring(frame.end)+L" ms";}
                RECT label{10,45,r.right-10,85};DrawTextW(dc,title.c_str(),-1,&label,DT_LEFT|DT_WORDBREAK);
                if(valid){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=static_cast<LONG>(width);info.bmiHeader.biHeight=-static_cast<LONG>(height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
                    const auto lines=StretchDIBits(dc,10,90,static_cast<int>(width),static_cast<int>(height),0,0,static_cast<int>(width),static_cast<int>(height),pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
                    if(lines==static_cast<int>(GDI_ERROR)||lines==0){control->error=ERROR_INVALID_DATA;}else{const auto index=control->selected;strcpy_s(control->displayed_sha[index],frame.sha256);control->last_display_tick=GetTickCount64();++control->painted;log.emit("preview_frame_painted",frame_json(frame)+",\"consumerPid\":"+std::to_string(GetCurrentProcessId())+",\"paintTick\":"+std::to_string(control->last_display_tick));MemoryBarrier();control->displayed[index]=frame.sequence;}
                }
            }
            EndPaint(w,&ps);return 0;}
        if(m==WM_CLOSE){DestroyWindow(w);return 0;}
        if(m==WM_DESTROY){control->closed=1;log.emit("aux_window_destroyed","\"pid\":"+std::to_string(GetCurrentProcessId())+",\"host\":"+(host?"true":"false"));PostQuitMessage(0);return 0;}
        return DefWindowProcW(w,m,a,b);
    }
    static LRESULT CALLBACK window(HWND w,UINT m,WPARAM a,LPARAM b){
        App* self=reinterpret_cast<App*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->top=w;}
        if(!self)return DefWindowProcW(w,m,a,b);
        try{return self->dispatch(w,m,a,b);}
        catch(const Error& e){self->control->error=static_cast<LONG>(e.code);self->log.emit("aux_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));}
        catch(const std::exception& e){self->control->error=ERROR_INVALID_DATA;self->log.emit("aux_failure","\"api\":\"window.callback\",\"error\":13,\"message\":"+json(e.what()));}
        PostMessageW(w,WM_CLOSE,0,0);return 0;
    }
    int run(){
        const auto module=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.hInstance=module;cls.lpfnWndProc=window;cls.lpszClassName=L"FocusViewsReadOnlyAcceptance";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);check(RegisterClassW(&cls)!=0,"RegisterClassW.live_aux");
        const DWORD extended=host?0:WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW;
        top=CreateWindowExW(extended,cls.lpszClassName,host?L"Host acceptance - human input only":L"Recovered Views - read-only Frame preview",WS_OVERLAPPEDWINDOW,40,40,800,500,nullptr,nullptr,module,this);check(top!=nullptr,"CreateWindowExW.live_aux");
        if(host){std::wstring instructions=L"Human-only acceptance. While restored Views run in background, click and move here, type exactly: "+std::wstring(control->challenge)+L". Check the box only if you saw no focus theft, cursor jump or input leakage; then Confirm. No unrelated app input is recorded.";
            check(CreateWindowW(L"STATIC",instructions.c_str(),WS_CHILD|WS_VISIBLE,12,12,750,90,top,nullptr,module,nullptr)!=nullptr,"CreateWindowW.host_instructions");
            edit=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,15,115,650,28,top,reinterpret_cast<HMENU>(200),module,nullptr);check(edit!=nullptr,"CreateWindowW.host_edit");SendMessageW(edit,EM_SETLIMITTEXT,80,0);SetWindowLongPtrW(edit,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));edit_proc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(edit_window)));check(edit_proc!=nullptr,"SetWindowLongPtrW.host_observer");
            confirmation=CreateWindowW(L"BUTTON",L"I performed the test and observed no unexpected interference",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,15,165,730,28,top,reinterpret_cast<HMENU>(201),module,nullptr);check(confirmation!=nullptr,"CreateWindowW.host_checkbox");
            check(CreateWindowW(L"BUTTON",L"Confirm human observation",WS_CHILD|WS_VISIBLE,15,210,300,30,top,reinterpret_cast<HMENU>(202),module,nullptr)!=nullptr,"CreateWindowW.host_confirm");
        }else{for(int i=0;i<3;++i)check(CreateWindowW(L"BUTTON",i==0?L"RA":i==1?L"RB":L"C",WS_CHILD|WS_VISIBLE,10+i*95,8,85,28,top,reinterpret_cast<HMENU>(static_cast<INT_PTR>(100+i)),module,nullptr)!=nullptr,"CreateWindowW.preview_selector");}
        control->pid=GetCurrentProcessId();control->created=creation(GetCurrentProcess());control->window=reinterpret_cast<std::uint64_t>(top);control->started=GetTickCount64();control->read_only=host?0:feed->read_only();
        ShowWindow(top,show?SW_SHOWNOACTIVATE:SW_HIDE);UpdateWindow(top);check(SetTimer(top,1,100,nullptr)!=0,"SetTimer.live_aux");
        log.emit("aux_ready","\"role\":"+json(host?"host":"preview")+",\"pid\":"+std::to_string(control->pid)+",\"created\":"+std::to_string(control->created)+",\"feedReadOnly\":"+(control->read_only?"true":"false")+",\"shownWithoutActivation\":true,\"visible\":"+(show?"true":"false"));MemoryBarrier();InterlockedExchange(&control->ready,1);
        MSG msg{};BOOL result=0;while((result=GetMessageW(&msg,nullptr,0,0))>0){TranslateMessage(&msg);DispatchMessageW(&msg);}if(result<0)throw Error("GetMessageW.live_aux",GetLastError());return control->error?2:0;
    }
};
int wmain(int argc,wchar_t** argv){try{
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);hr(initialized,"CoInitializeEx.live_aux");struct Uninit{~Uninit(){CoUninitialize();}} done;
    const Args args(argc,argv);r305::Log log(args.get(L"--log"));Mapping control(args.get(L"--control"),sizeof(Control),false);App app(log);app.control=static_cast<Control*>(control.view);app.host=args.get(L"--role")==L"host";app.show=args.number(L"--visible",1)!=0;
    if(!app.host){app.feed=std::make_unique<ReadOnlyFeed>(args.get(L"--feed"));Feed f{};demand(read_feed(app.feed->get(),f),"coherent initial Frame feed");app.manager.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,f.manager_pid);check(app.manager.value!=nullptr,"OpenProcess.preview_manager");demand(creation(app.manager.value)==f.manager_created,"pinned preview producer instance");hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&app.wic.p)),"CoCreateInstance.preview_WIC");}
    return app.run();
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
