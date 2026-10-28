#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbgeng.h>
#include <cstdio>
struct DebugOutput:IDebugOutputCallbacks {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(id==__uuidof(IUnknown)||id==__uuidof(IDebugOutputCallbacks)){*out=static_cast<IDebugOutputCallbacks*>(this);return S_OK;}*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return 1;}
    ULONG STDMETHODCALLTYPE Release()override{return 1;}
    HRESULT STDMETHODCALLTYPE Output(ULONG,PCSTR text)override{std::fputs(text,stdout);return S_OK;}
};
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 1;IDebugClient4* client=nullptr;IDebugControl* control=nullptr;IDebugSymbols3* symbols=nullptr;
    auto hr=DebugCreate(__uuidof(IDebugClient4),reinterpret_cast<void**>(&client));if(FAILED(hr))return 2;
    client->QueryInterface(__uuidof(IDebugControl),reinterpret_cast<void**>(&control));client->QueryInterface(__uuidof(IDebugSymbols3),reinterpret_cast<void**>(&symbols));
    DebugOutput output;client->SetOutputCallbacks(&output);symbols->SetSymbolPathWide(argv[2]);
    hr=client->OpenDumpFileWide(argv[1],0);if(SUCCEEDED(hr))hr=control->WaitForEvent(0,10000);
    if(SUCCEEDED(hr))hr=control->Execute(DEBUG_OUTCTL_THIS_CLIENT,"~* k",DEBUG_EXECUTE_DEFAULT);
    std::printf("\nDBGENG_RESULT 0x%08lx\n",static_cast<unsigned long>(hr));
    client->EndSession(DEBUG_END_PASSIVE);symbols->Release();control->Release();client->Release();return FAILED(hr)?3:0;
}
