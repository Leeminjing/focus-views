#include "../recorder/contract.hpp"
#include "host.hpp"
namespace {
using namespace r305;
struct RecorderClient {
    static constexpr unsigned contract_version=2;
    static constexpr size_t state_bytes=sizeof(recorder::State);
    static constexpr std::uint64_t return_mask=120;
    static const char* kind(){return "native-data-recorder";}
    static const char* contract_export(){return "RecorderContract";}
    static const char* mailbox_export(){return "RecorderMailbox";}
    static const char* file_name(){return "records.csv";}
    static std::string schema(){return "\"kind\":\"native-data-recorder\",\"stateBytes\":"+std::to_string(sizeof(recorder::State))+",\"seedOffset\":"+std::to_string(offsetof(recorder::State,run_id))+",\"descriptorOffset\":"+std::to_string(offsetof(recorder::State,descriptor))+",\"samplesOffset\":"+std::to_string(offsetof(recorder::State,samples))+",\"sampleStride\":"+std::to_string(sizeof(recorder::Sample))+",\"valueOffset\":"+std::to_string(offsetof(recorder::Sample,value))+",\"returnMask\":120,\"editId\":201,\"buttonId\":202,\"file\":\"records.csv\"";}
    static void validate(Child& v,DWORD rva){const auto a=native::read_value<recorder::Contract>(v.p.process.value,v.image+rva);const recorder::Contract expected{};if(a.magic!=expected.magic||a.version!=2||a.state_bytes!=state_bytes||a.mailbox_bytes!=sizeof(ClientCommand))throw Error("client.contract_mismatch",ERROR_REVISION_MISMATCH);if(a.frames!=4||a.returns!=6)throw Error("client.execution_layout_unsupported",ERROR_NOT_SUPPORTED);}
    static Observation observe(Child& v){const auto a=native::read_value<recorder::State>(v.p.process.value,v.m.root);Observation x;x.seed=a.run_id;x.counter=a.ticks;x.receipt=a.outcome;x.returned=a.return_mask;x.clicks=a.clicks;x.window=a.top;wcscpy_s(x.text,a.note);x.length=a.note_length;x.document_reads=a.imports;x.document_writes=a.exports;x.processed_bytes=a.processed;std::string rows="[";for(unsigned i=0;i<8;++i){if(i)rows+=',';rows+=std::to_string(a.samples[i].value);}x.details="{\"kind\":\"recorder\",\"descriptor\":"+std::to_string(a.descriptor)+",\"values\":"+rows+"]}";return x;}
    static std::uint64_t expected(const Snapshot& s){const auto a=raw_state<recorder::State>(s);return (((std::rotl(((s.cpu.R10+local_value(s,0))^local_value(s,1))*1099511628211ULL,7)+local_value(s,2))^local_value(s,3))+0x10591)^a.descriptor^a.samples[7].value;}
};
}
extern "C" __declspec(dllexport) unsigned ClientVersion(){return 1;}
extern "C" __declspec(dllexport) int RunClient(const wchar_t* m,const wchar_t* a,const wchar_t* o,const wchar_t* s){try{r305::Experiment<RecorderClient> e(m,a,o);return e.run(s);}catch(const std::exception& e){std::cerr<<"CLIENT_FAILURE "<<e.what()<<'\n';return 1;}}
