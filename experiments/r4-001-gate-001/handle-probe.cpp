#include "../r3/protocol.hpp"
#include <map>
// Positive and deliberate historical-misuse controls, confined to this process.
DWORD WINAPI worker(void*) { return 0; }
int wmain(int argc,wchar_t** argv) {
    if(argc>1 && wcscmp(argv[1],L"--child")==0){HANDLE t=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!t)return 2;WaitForSingleObject(t,2000);CloseHandle(t);return 0;}
    try {
        const bool misuse=argc>1 && wcscmp(argv[1],L"--early-close")==0;
        wchar_t path[32768]{};r3::check(GetModuleFileNameW(nullptr,path,32768)!=0,"GetModuleFileNameW");std::wstring cmd=L"\""+std::wstring(path)+L"\" --child";
        STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};r3::check(CreateProcessW(path,cmd.data(),nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS|CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi),"CreateProcessW");
        std::map<DWORD,HANDLE> victims;std::vector<HANDLE> allocated;unsigned damaged=0,checked=0;bool exited=false;
        const auto end=GetTickCount64()+8000;
        while(!exited && GetTickCount64()<end){DEBUG_EVENT e{};r3::check(WaitForDebugEvent(&e,2000),"WaitForDebugEvent");
            HANDLE event_thread=nullptr;
            if(e.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){CloseHandle(e.u.CreateProcessInfo.hFile);event_thread=e.u.CreateProcessInfo.hThread;}
            if(e.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT)event_thread=e.u.CreateThread.hThread;
            if(e.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT && e.u.LoadDll.hFile)CloseHandle(e.u.LoadDll.hFile);
            if(event_thread){
                if(misuse)r3::check(CloseHandle(event_thread),"CloseHandle.deliberate_historical_misuse");
                HANDLE sentinel=nullptr;
                for(unsigned n=0;n<(misuse?512u:1u);++n){auto h=CreateEventW(nullptr,TRUE,FALSE,nullptr);r3::check(h!=nullptr,"CreateEventW");allocated.push_back(h);if(!misuse || h==event_thread){sentinel=h;break;}}
                r3::demand(sentinel!=nullptr,"recycled event handle slot found");victims[e.dwThreadId]=sentinel;
                std::cout<<"{\"type\":\"probe_slot\",\"tid\":"<<e.dwThreadId<<",\"debugHandle\":"<<reinterpret_cast<uintptr_t>(event_thread)<<",\"sentinel\":"<<reinterpret_cast<uintptr_t>(sentinel)<<",\"earlyClose\":"<<misuse<<"}\n";
            }
            const bool exit=e.dwDebugEventCode==EXIT_THREAD_DEBUG_EVENT || e.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT;
            HANDLE sentinel=exit?victims[e.dwThreadId]:nullptr;DWORD info=0;
            if(sentinel)r3::check(GetHandleInformation(sentinel,&info),"sentinel.valid_before_exit_continue");
            r3::check(ContinueDebugEvent(e.dwProcessId,e.dwThreadId,DBG_CONTINUE),"ContinueDebugEvent");
            if(sentinel){const auto valid=GetHandleInformation(sentinel,&info);const auto error=valid?0:GetLastError();++checked;if(!valid)++damaged;
                std::cout<<"{\"type\":\"probe_exit\",\"tid\":"<<e.dwThreadId<<",\"sentinelSurvived\":"<<(valid?"true":"false")<<",\"error\":"<<error<<"}\n";
                if(!valid)for(auto& h:allocated)if(h==sentinel)h=nullptr;
            }
            exited=e.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT;
        }
        r3::demand(exited,"owned probe process exited");r3::check(WaitForSingleObject(pi.hProcess,1000)==WAIT_OBJECT_0,"probe.exit_confirmed");CloseHandle(pi.hProcess);CloseHandle(pi.hThread);
        for(auto h:allocated)if(h)CloseHandle(h);
        const auto okay=checked>=2 && (misuse?damaged==checked:damaged==0);
        std::cout<<"{\"type\":\"probe_result\",\"historicalMisuse\":"<<(misuse?"true":"false")<<",\"checked\":"<<checked<<",\"damaged\":"<<damaged<<",\"expectedControlObserved\":"<<(okay?"true":"false")<<"}\n";return okay?0:2;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
