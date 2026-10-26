#include "../../r5-001a/scenario/fixture_contract.hpp"
#include "host.hpp"
namespace {
using namespace r305;
struct FixtureClient {
    static constexpr unsigned contract_version=1;
    static constexpr size_t state_bytes=sizeof(Work);
    static constexpr std::uint64_t return_mask=7;
    static const char* kind(){return "original-fixture";}
    static const char* contract_export(){return "R5FixtureContract";}
    static const char* mailbox_export(){return "R5FixtureMailbox";}
    static const char* file_name(){return "document.txt";}
    static std::string schema(){return "\"kind\":\"original-fixture\",\"stateBytes\":"+std::to_string(sizeof(Work))+",\"seedOffset\":"+std::to_string(offsetof(Work,seed))+",\"returnMask\":7,\"editId\":101,\"buttonId\":102,\"file\":\"document.txt\"";}
    static void validate(Child& v,DWORD rva){const auto actual=native::read_value<FixtureContract>(v.p.process.value,v.image+rva);FixtureContract expected{};if(actual.magic!=expected.magic||actual.version!=1||actual.work_bytes!=sizeof(Work)||actual.mailbox_bytes!=sizeof(ClientCommand))throw Error("client.contract_mismatch",ERROR_REVISION_MISMATCH);}
    static Observation observe(Child& v){const auto a=native::read_value<Work>(v.p.process.value,v.m.root);Observation x;x.seed=a.seed;x.counter=a.counter;x.receipt=a.receipt;x.returned=a.returned;x.clicks=a.clicks;x.window=a.window;wcscpy_s(x.text,a.text);x.length=a.length;x.point_x=a.point_x;x.point_y=a.point_y;x.document_reads=a.document_reads;x.document_writes=a.document_writes;x.processed_bytes=a.processed_bytes;return x;}
    static std::uint64_t expected(const Snapshot& s){const auto d=raw_state<Work>(s);return std::rotl((s.cpu.R10^local_value(s,0))+local_value(s,1),13)^local_value(s,2)^d.seed;}
};
}
extern "C" __declspec(dllexport) unsigned ClientVersion(){return 1;}
extern "C" __declspec(dllexport) int RunClient(const wchar_t* m,const wchar_t* a,const wchar_t* o,const wchar_t* s){try{r305::Experiment<FixtureClient> e(m,a,o);return e.run(s);}catch(const std::exception& e){std::cerr<<"CLIENT_FAILURE "<<e.what()<<'\n';return 1;}}
