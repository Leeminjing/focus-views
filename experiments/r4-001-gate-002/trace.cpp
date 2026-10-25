#include "common.hpp"
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <cwctype>
namespace gate002 {
struct Properties {
    std::vector<BYTE> bytes;
    Properties(const std::filesystem::path& path):bytes(sizeof(EVENT_TRACE_PROPERTIES)+1024*sizeof(wchar_t)+32768*sizeof(wchar_t)){
        auto& p=get();p.Wnode.BufferSize=static_cast<ULONG>(bytes.size());p.Wnode.Guid=session_guid;p.Wnode.ClientContext=1;p.Wnode.Flags=WNODE_FLAG_TRACED_GUID;
        p.BufferSize=64;p.MinimumBuffers=4;p.MaximumBuffers=32;p.MaximumFileSize=64;p.FlushTimer=1;p.LogFileMode=EVENT_TRACE_FILE_MODE_CIRCULAR;
        p.LoggerNameOffset=sizeof(EVENT_TRACE_PROPERTIES);p.LogFileNameOffset=p.LoggerNameOffset+1024*sizeof(wchar_t);
        wcscpy_s(reinterpret_cast<wchar_t*>(bytes.data()+p.LoggerNameOffset),1024,session_name);
        wcscpy_s(reinterpret_cast<wchar_t*>(bytes.data()+p.LogFileNameOffset),32768,path.c_str());
    }
    EVENT_TRACE_PROPERTIES& get(){return *reinterpret_cast<EVENT_TRACE_PROPERTIES*>(bytes.data());}
    std::wstring path(){auto& p=get();if(!p.LogFileNameOffset || p.LogFileNameOffset>=bytes.size())return {};return reinterpret_cast<wchar_t*>(bytes.data()+p.LogFileNameOffset);}
};
struct Session {
    Properties properties;Log& log;TRACEHANDLE handle=0;bool owns=false;
    Session(const std::filesystem::path& path,Log& l):properties(path),log(l){}
    ULONG stop(){if(!owns)return ERROR_SUCCESS;auto& p=properties.get();const auto status=ControlTraceW(handle,session_name,&p,EVENT_TRACE_CONTROL_STOP);log.emit("trace_stopped","\"api\":\"ControlTraceW\",\"status\":"+std::to_string(status)+",\"eventsLost\":"+std::to_string(p.EventsLost)+",\"buffersWritten\":"+std::to_string(p.BuffersWritten)+",\"logBuffersLost\":"+std::to_string(p.LogBuffersLost));if(!status)owns=false;return status;}
    ~Session(){if(owns)stop();}
};
void save_ready(const std::filesystem::path& path,ULONG start,ULONG process,ULONG memory,ULONG marker){std::ofstream f(path,std::ios::binary);f<<"{\"startStatus\":"<<start<<",\"processEnableStatus\":"<<process<<",\"memoryEnableStatus\":"<<memory<<",\"markerEnableStatus\":"<<marker<<",\"collectorPid\":"<<GetCurrentProcessId()<<','<<gate001::identity()<<"}\n";demand(f.good(),"trace readiness write");}
int record(const Args& args){
    const auto etl=std::filesystem::absolute(args.get(L"--etl"));const auto ready=std::filesystem::absolute(args.get(L"--ready"));const auto stop_file=std::filesystem::absolute(args.get(L"--stop"));
    demand(!std::filesystem::exists(etl)&&!std::filesystem::exists(ready),"fresh trace files");Log log(args.get(L"--log"));
    const auto seconds=args.number(L"--seconds",180);demand(seconds>=1&&seconds<=180,"trace maximum 180 seconds");
    Handle parent(OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(args.number(L"--parent"))));check(parent.value!=nullptr,"OpenProcess.trace_parent");
    log.emit("trace_configuration","\"session\":"+json(session_name)+",\"sessionGuid\":"+json(guid_text(session_guid))+",\"etlLocalOnly\":"+json(etl.c_str())+",\"seconds\":"+std::to_string(seconds)+",\"maximumMB\":64,"+gate001::identity());
    Session session(etl,log);const auto started=StartTraceW(&session.handle,session_name,&session.properties.get());session.owns=started==ERROR_SUCCESS;
    log.emit("trace_started","\"api\":\"StartTraceW\",\"status\":"+std::to_string(started));
    if(started){save_ready(ready,started,ERROR_INVALID_HANDLE,ERROR_INVALID_HANDLE,ERROR_INVALID_HANDLE);return 2;}
    ENABLE_TRACE_PARAMETERS params{};params.Version=ENABLE_TRACE_PARAMETERS_VERSION_2;
    const auto enable=[&](const GUID& provider,ULONGLONG keywords){const auto status=EnableTraceEx2(session.handle,&provider,EVENT_CONTROL_CODE_ENABLE_PROVIDER,TRACE_LEVEL_VERBOSE,keywords,0,1000,&params);log.emit("provider_enabled","\"api\":\"EnableTraceEx2\",\"provider\":"+json(guid_text(provider))+",\"keywords\":"+std::to_string(keywords)+",\"status\":"+std::to_string(status));return status;};
    const auto process=enable(process_guid,args.number(L"--process-keywords",0x470)),memory=enable(memory_guid,0x20),marker=enable(marker_guid,1);
    save_ready(ready,started,process,memory,marker);
    const auto end=GetTickCount64()+seconds*1000;
    while(GetTickCount64()<end && !std::filesystem::exists(stop_file) && WaitForSingleObject(parent.value,0)==WAIT_TIMEOUT)Sleep(25);
    log.emit("trace_stop_trigger","\"requested\":"+(std::filesystem::exists(stop_file)?std::string("true"):"false")+",\"deadlineReached\":"+(GetTickCount64()>=end?std::string("true"):"false"));
    const auto result=session.stop();Properties query(etl);const auto queried=ControlTraceW(0,session_name,&query.get(),EVENT_TRACE_CONTROL_QUERY);
    log.emit("trace_cleanup_query","\"status\":"+std::to_string(queried)+",\"absent\":"+(queried==ERROR_WMI_INSTANCE_NOT_FOUND?std::string("true"):"false"));
    return !result && queried==ERROR_WMI_INSTANCE_NOT_FOUND && !process?0:2;
}
int stop_owned(const Args& args){
    const auto path=std::filesystem::absolute(args.get(L"--etl"));Properties properties(path);const auto q=ControlTraceW(0,session_name,&properties.get(),EVENT_TRACE_CONTROL_QUERY);
    std::cout<<"{\"api\":\"ControlTraceW.query\",\"status\":"<<q<<"}\n";
    if(q==ERROR_WMI_INSTANCE_NOT_FOUND)return 0;
    demand(q==ERROR_SUCCESS && IsEqualGUID(properties.get().Wnode.Guid,session_guid) && _wcsicmp(properties.path().c_str(),path.c_str())==0,"stop only verified owned session GUID and ETL path");
    const auto stopped=ControlTraceW(0,session_name,&properties.get(),EVENT_TRACE_CONTROL_STOP);std::cout<<"{\"api\":\"ControlTraceW.stop_owned\",\"status\":"<<stopped<<"}\n";return stopped?2:0;
}
struct Lifetime {DWORD pid=0;ULONGLONG begin=0,end=0;};
struct Field {std::wstring name;std::string value;ULONGLONG number=0;bool numeric=false;};
std::wstring lower(std::wstring text){for(auto& c:text)c=static_cast<wchar_t>(std::towlower(c));return text;}
struct Decoder {
    Log output;std::vector<Lifetime> ranges;ULONGLONG scanned=0,exported=0,markers=0,kernel=0,memory=0,metadata_errors=0,property_errors=0;
    explicit Decoder(const Args& args):output(args.get(L"--log")){
        std::ifstream input(std::filesystem::path(args.get(L"--ranges")));demand(input.good(),"PID lifetime ranges");Lifetime item{};while(input>>item.pid>>item.begin>>item.end)ranges.push_back(item);demand(!ranges.empty(),"nonempty PID lifetime ranges");
    }
    bool owned(DWORD pid,ULONGLONG at)const{for(const auto& r:ranges)if(r.pid==pid && at+20000>=r.begin && at<=r.end+20000)return true;return false;}
    void event(EVENT_RECORD* event){
        ++scanned;const auto& header=event->EventHeader;const auto at=static_cast<ULONGLONG>(header.TimeStamp.QuadPart);
        const bool own_marker=IsEqualGUID(header.ProviderId,marker_guid)!=FALSE;
        const bool is_memory=IsEqualGUID(header.ProviderId,memory_guid)!=FALSE;
        if(own_marker){if(!owned(header.ProcessId,at))return;std::wstring text(static_cast<const wchar_t*>(event->UserData),event->UserDataLength/sizeof(wchar_t));while(!text.empty()&&!text.back())text.pop_back();if(text.rfind(L"GATE002 ",0)!=0)return;++markers;++exported;output.emit("etw_marker","\"eventUtc\":"+std::to_string(at)+",\"pid\":"+std::to_string(header.ProcessId)+",\"tid\":"+std::to_string(header.ThreadId)+",\"message\":"+json(text.c_str()));return;}
        if(!is_memory && !IsEqualGUID(header.ProviderId,process_guid))return;
        ULONG size=0;auto status=TdhGetEventInformation(event,0,nullptr,nullptr,&size);if(status!=ERROR_INSUFFICIENT_BUFFER){++metadata_errors;return;}
        std::vector<BYTE> buffer(size);auto* info=reinterpret_cast<TRACE_EVENT_INFO*>(buffer.data());status=TdhGetEventInformation(event,0,nullptr,info,&size);if(status){++metadata_errors;return;}
        const std::set<std::wstring> selected{L"processid",L"parentprocessid",L"parentid",L"creatingprocessid",L"threadid",L"imagefilename",L"imagename",L"filename",L"imagebase",L"imagesize",L"exitcode",L"exitstatus",L"status",L"statuscode",L"result",L"failurecode",L"createtime",L"processstartkey",L"jobid"};
        std::vector<Field> fields;bool related=owned(header.ProcessId,at);
        for(ULONG i=0;i<info->TopLevelPropertyCount;++i){
            const auto& p=info->EventPropertyInfoArray[i];if(p.Flags&PropertyStruct)continue;
            const auto* name=reinterpret_cast<wchar_t*>(buffer.data()+p.NameOffset);const auto normalized=lower(name);const auto type=p.nonStructType.InType;
            if(!is_memory && !selected.contains(normalized))continue;
            PROPERTY_DATA_DESCRIPTOR descriptor{};descriptor.PropertyName=reinterpret_cast<ULONGLONG>(name);descriptor.ArrayIndex=ULONG_MAX;ULONG length=0;
            status=TdhGetPropertySize(event,0,nullptr,1,&descriptor,&length);if(status||length>8192){++property_errors;continue;}
            std::vector<BYTE> value(length+2,0);status=TdhGetProperty(event,0,nullptr,1,&descriptor,length,value.data());if(status){++property_errors;continue;}
            Field f;f.name=name;
            if(type==TDH_INTYPE_UNICODESTRING){if(is_memory)continue;f.value=json(reinterpret_cast<wchar_t*>(value.data()));}
            else if(type==TDH_INTYPE_ANSISTRING){if(is_memory)continue;f.value=json(std::string(reinterpret_cast<char*>(value.data()),strnlen_s(reinterpret_cast<char*>(value.data()),length)));}
            else if(length>=1&&length<=8 && type!=TDH_INTYPE_BINARY&&type!=TDH_INTYPE_GUID&&type!=TDH_INTYPE_SID){memcpy(&f.number,value.data(),length);f.numeric=true;f.value=std::to_string(f.number);}
            else continue;
            if(f.numeric && (normalized==L"processid"||normalized==L"parentprocessid"||normalized==L"parentid"||normalized==L"creatingprocessid"))related=related||owned(static_cast<DWORD>(f.number),at);
            fields.push_back(std::move(f));
        }
        if(!related && !is_memory)return;
        std::ostringstream body;body<<'{';bool first=true;for(const auto& f:fields){if(!first)body<<',';first=false;body<<json(f.name.c_str())<<':'<<f.value;}body<<'}';
        std::wstring event_name;if(info->EventNameOffset && info->EventNameOffset<size)event_name=reinterpret_cast<wchar_t*>(buffer.data()+info->EventNameOffset);
        ++exported;if(is_memory)++memory;else ++kernel;
        output.emit(is_memory?"etw_global_memory":"etw_kernel_process","\"eventUtc\":"+std::to_string(at)+",\"pid\":"+std::to_string(header.ProcessId)+",\"tid\":"+std::to_string(header.ThreadId)+",\"eventId\":"+std::to_string(header.EventDescriptor.Id)+",\"version\":"+std::to_string(header.EventDescriptor.Version)+",\"opcode\":"+std::to_string(header.EventDescriptor.Opcode)+",\"eventName\":"+json(event_name.c_str())+",\"fields\":"+body.str());
    }
};
void WINAPI callback(EVENT_RECORD* record){auto* decoder=static_cast<Decoder*>(record->UserContext);try{decoder->event(record);}catch(...){++decoder->metadata_errors;}}
int decode(const Args& args){
    Decoder decoder(args);auto path=args.get(L"--etl");EVENT_TRACE_LOGFILEW logfile{};logfile.LogFileName=path.data();logfile.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD;logfile.EventRecordCallback=callback;logfile.Context=&decoder;
    auto handle=OpenTraceW(&logfile);if(handle==INVALID_PROCESSTRACE_HANDLE)throw Error("OpenTraceW",GetLastError());const auto status=ProcessTrace(&handle,1,nullptr,nullptr);const auto closed=CloseTrace(handle);
    decoder.output.emit("etw_decode_complete","\"ProcessTraceStatus\":"+std::to_string(status)+",\"CloseTraceStatus\":"+std::to_string(closed)+",\"scanned\":"+std::to_string(decoder.scanned)+",\"exported\":"+std::to_string(decoder.exported)+",\"markers\":"+std::to_string(decoder.markers)+",\"kernelProcessEvents\":"+std::to_string(decoder.kernel)+",\"globalMemoryEvents\":"+std::to_string(decoder.memory)+",\"metadataErrors\":"+std::to_string(decoder.metadata_errors)+",\"propertyErrors\":"+std::to_string(decoder.property_errors)+",\"rawTimestampMode\":false,\"PIDWindowTolerance100ns\":20000");return status?2:0;
}
}
int wmain(int argc,wchar_t** argv){try{const r3::Args args(argc,argv);const auto mode=args.get(L"--mode");if(mode==L"record")return gate002::record(args);if(mode==L"stop-owned")return gate002::stop_owned(args);if(mode==L"decode")return gate002::decode(args);throw r3::Error("trace.mode",ERROR_INVALID_PARAMETER);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
