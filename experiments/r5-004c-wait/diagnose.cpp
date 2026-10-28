#include "../r5-004/crash.hpp"
#include <dbghelp.h>
#include <wct.h>
#include <tlhelp32.h>
int wmain(int argc,wchar_t** argv){using namespace r305;try{
    r3::Args args(argc,argv);Log log(args.get(L"--log"));const auto pid=std::stoul(args.get(L"--pid")),tid=std::stoul(args.get(L"--tid"));const auto created=std::stoull(args.get(L"--created"));
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid));check(process.value!=nullptr,"OpenProcess.cleanup_diagnostic");
    if(r5004::creation(process.value)!=created||WaitForSingleObject(process.value,0)!=WAIT_TIMEOUT)throw Error("diagnostic.pinned_process",ERROR_INVALID_OWNER);
    std::vector<DWORD> threads{tid};
    log.emit("diagnostic_started","\"pid\":"+std::to_string(pid)+",\"tid\":"+std::to_string(tid)+",\"created\":"+std::to_string(created));
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0));
    if(snapshot.value!=INVALID_HANDLE_VALUE){THREADENTRY32 entry{};entry.dwSize=sizeof(entry);if(Thread32First(snapshot.value,&entry))do{if(entry.th32OwnerProcessID==pid&&entry.th32ThreadID!=tid&&threads.size()<8)threads.push_back(entry.th32ThreadID);}while(Thread32Next(snapshot.value,&entry));}
    log.emit("symbols_begin","\"localOnly\":true");
    SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
    const auto local_path=r3::utf8(std::filesystem::path(args.get(L"--log")).parent_path().wstring());const auto symbols=SymInitialize(process.value,local_path.c_str(),TRUE);const auto symbol_error=symbols?0:GetLastError();
    log.emit("symbols","\"result\":"+std::to_string(symbols)+",\"error\":"+std::to_string(symbol_error)+",\"networkSymbols\":false");
    for(const auto thread_id:threads){if(threads.size()>16)throw Error("diagnostic.thread_bound",ERROR_INVALID_DATA);
        Handle thread(OpenThread(THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME|THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,thread_id));
        if(!thread.value){const auto open_error=GetLastError();log.emit("thread_unavailable","\"tid\":"+std::to_string(thread_id)+",\"error\":"+std::to_string(open_error));continue;}
        const auto owner=GetProcessIdOfThread(thread.value);check(owner!=0,"GetProcessIdOfThread.diagnostic");if(owner!=pid)throw Error("diagnostic.thread_owner",ERROR_INVALID_OWNER);
        FILETIME c{},e{},k{},u{};check(GetThreadTimes(thread.value,&c,&e,&k,&u),"GetThreadTimes.diagnostic");const auto thread_created=(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
        const auto suspended=SuspendThread(thread.value);
        if(suspended==DWORD(-1)){const auto suspend_error=GetLastError();log.emit("thread_suspend_failed","\"tid\":"+std::to_string(thread_id)+",\"error\":"+std::to_string(suspend_error));continue;}
        CONTEXT cpu{};cpu.ContextFlags=CONTEXT_FULL;const auto captured=GetThreadContext(thread.value,&cpu);const auto error=captured?0:GetLastError();
        log.emit("thread_context","\"tid\":"+std::to_string(thread_id)+",\"created\":"+std::to_string(thread_created)+",\"result\":"+std::to_string(captured)+",\"error\":"+std::to_string(error)+",\"rip\":"+std::to_string(cpu.Rip)+",\"rsp\":"+std::to_string(cpu.Rsp)+",\"previousSuspend\":"+std::to_string(suspended));
        if(captured&&symbols){STACKFRAME64 frame{};frame.AddrPC.Offset=cpu.Rip;frame.AddrStack.Offset=cpu.Rsp;frame.AddrFrame.Offset=cpu.Rbp;frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
            for(unsigned n=0;n<32;++n){if(!frame.AddrPC.Offset)break;alignas(SYMBOL_INFO) BYTE storage[sizeof(SYMBOL_INFO)+512]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(storage);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=511;DWORD64 displacement=0;const auto named=SymFromAddr(process.value,frame.AddrPC.Offset,&displacement,symbol);const auto name_error=named?0:GetLastError();
                IMAGEHLP_MODULE64 module{};module.SizeOfStruct=sizeof(module);const auto identified=SymGetModuleInfo64(process.value,frame.AddrPC.Offset,&module);const auto module_error=identified?0:GetLastError();
                log.emit("frame","\"tid\":"+std::to_string(thread_id)+",\"index\":"+std::to_string(n)+",\"pc\":"+std::to_string(frame.AddrPC.Offset)+",\"sp\":"+std::to_string(frame.AddrStack.Offset)+",\"module\":"+json(identified?module.ImageName:"")+",\"symbol\":"+json(named?symbol->Name:"")+",\"displacement\":"+std::to_string(displacement)+",\"symbolType\":"+std::to_string(identified?module.SymType:SymNone)+",\"symbolError\":"+std::to_string(name_error)+",\"moduleError\":"+std::to_string(module_error));
                SetLastError(0);const auto walked=StackWalk64(IMAGE_FILE_MACHINE_AMD64,process.value,thread.value,&frame,&cpu,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr);const auto walk_error=walked?0:GetLastError();if(!walked){log.emit("stack_walk_end","\"tid\":"+std::to_string(thread_id)+",\"error\":"+std::to_string(walk_error));break;}
            }
        }
        const auto resumed=ResumeThread(thread.value);const auto resume_error=resumed==DWORD(-1)?GetLastError():0;log.emit("thread_resumed","\"tid\":"+std::to_string(thread_id)+",\"result\":"+std::to_string(resumed)+",\"error\":"+std::to_string(resume_error));
    }
    if(symbols)SymCleanup(process.value);
    // Microsoft's documented WCT COM integration. This diagnostic child holds
    // OLE32 until exit; no privilege adjustment or system service mutation.
    const auto ole=LoadLibraryW(L"ole32.dll");const auto ole_error=ole?0:GetLastError();
    const auto call=ole?reinterpret_cast<PCOGETCALLSTATE>(GetProcAddress(ole,"CoGetCallState")):nullptr;const auto call_error=call?0:GetLastError();
    const auto activation=ole?reinterpret_cast<PCOGETACTIVATIONSTATE>(GetProcAddress(ole,"CoGetActivationState")):nullptr;const auto activation_error=activation?0:GetLastError();
    if(call&&activation)RegisterWaitChainCOMCallback(call,activation);
    log.emit("wct_com_callbacks","\"registered\":"+std::string(call&&activation?"true":"false")+",\"oleError\":"+std::to_string(ole_error)+",\"callError\":"+std::to_string(call_error)+",\"activationError\":"+std::to_string(activation_error));
    log.emit("wait_chain_begin","\"boundedByParentProcess\":true");
    const auto session=OpenThreadWaitChainSession(0,nullptr);
    if(session){std::vector<DWORD> pending{tid};std::set<DWORD> visited;
        while(!pending.empty()&&visited.size()<4){const auto next=pending.front();pending.erase(pending.begin());if(!visited.insert(next).second)continue;
            // A newly encountered worker may have exited. Pin and validate it
            // before querying; do not suspend or walk foreign processes.
            Handle worker(OpenThread(THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,next));
            if(!worker.value){const auto e=GetLastError();log.emit("wct_thread_unavailable","\"tid\":"+std::to_string(next)+",\"error\":"+std::to_string(e));continue;}
            if(GetProcessIdOfThread(worker.value)!=pid){log.emit("wct_thread_owner_changed","\"tid\":"+std::to_string(next));continue;}
            FILETIME c{},e{},k{},u{};check(GetThreadTimes(worker.value,&c,&e,&k,&u),"GetThreadTimes.wct_worker");
            log.emit("wct_query_begin","\"tid\":"+std::to_string(next)+",\"created\":"+std::to_string((std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime));
            WAITCHAIN_NODE_INFO nodes[WCT_MAX_NODE_COUNT]{};DWORD count=WCT_MAX_NODE_COUNT;BOOL cycle=FALSE;const auto result=GetThreadWaitChain(session,0,WCTP_GETINFO_ALL_FLAGS,next,&count,nodes,&cycle);const auto error=result?0:GetLastError();
            log.emit("wait_chain","\"pid\":"+std::to_string(pid)+",\"tid\":"+std::to_string(next)+",\"result\":"+std::to_string(result)+",\"error\":"+std::to_string(error)+",\"cycle\":"+std::to_string(cycle));
            if(result)for(DWORD i=0;i<std::min<DWORD>(count,WCT_MAX_NODE_COUNT);++i){const auto& n=nodes[i];const bool is_thread=n.ObjectType==WctThreadType;log.emit("wait_node","\"queryTid\":"+std::to_string(next)+",\"index\":"+std::to_string(i)+",\"kind\":"+std::to_string(n.ObjectType)+",\"status\":"+std::to_string(n.ObjectStatus)+(is_thread?",\"pid\":"+std::to_string(n.ThreadObject.ProcessId)+",\"tid\":"+std::to_string(n.ThreadObject.ThreadId):",\"name\":"+json(n.LockObject.ObjectName)));
                if(is_thread&&n.ThreadObject.ProcessId==pid&&!visited.contains(n.ThreadObject.ThreadId))pending.push_back(n.ThreadObject.ThreadId);
            }
        }
        CloseThreadWaitChainSession(session);
    }else{const auto wct_error=GetLastError();log.emit("wait_chain_unavailable","\"error\":"+std::to_string(wct_error));}

    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
