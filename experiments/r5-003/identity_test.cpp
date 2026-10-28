#include "../r5-002/clients/fixture_model.hpp"
#include "clients/restart.hpp"

// Deterministic identity counterexamples. These declarations emulate reuse;
// they do not claim that Windows naturally assigned this PID twice.
int wmain(int argc,wchar_t** argv){using namespace r5003;try{
    r3::Args args(argc,argv);const std::filesystem::path out=args.get(L"--output"),baseline=args.get(L"--store"),app=args.get(L"--fixture");
    if(!std::filesystem::create_directory(out))throw Error("identity_test.new_output",ERROR_ALREADY_EXISTS);
    Log log(out/"identity.jsonl");unsigned failed=0;
    auto check_case=[&](const char* name,bool pass){log.emit("check","\"name\":"+json(name)+",\"passed\":"+(pass?"true":"false"));if(!pass)++failed;};
    const Instance current{GetCurrentProcessId(),creation(GetCurrentProcess())},earlier{current.pid,current.created-1};
    check_case("same-pid-different-created-is-gone",gone(earlier));
    check_case("same-live-instance-is-not-gone",!gone(current));
    try{creation(nullptr);check_case("query-error-is-not-gone",false);}catch(const Error& e){log.emit("query_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));check_case("query-error-is-not-gone",e.api=="GetProcessTimes.restart_identity"&&e.code==ERROR_INVALID_HANDLE);}
    check_case("same-pid-new-created-is-distinct",different_instance(current,earlier));
    check_case("same-instance-is-not-distinct",!different_instance(current,current));
    try{gone({0,0});check_case("unknown-identity-refused",false);}catch(const Error& e){check_case("unknown-identity-refused",e.api=="snapshot.instance_identity");}
    for(const bool alive:{false,true}){
        const auto name=alive?"live-previous":"reused-pid";const auto store=out/name;
        std::filesystem::copy(baseline,store,std::filesystem::copy_options::recursive);
        auto session=decode_session(payload(store/"session.fvs",Kind::session));
        session.manager=alive?current:earlier;session.source=session.a=session.b=earlier;
        std::filesystem::remove(store/"session.fvs");publish(store/"session.fvs",Kind::session,encode_session(session));
        auto snapshot=decode(payload(store/"S0.fvs",Kind::snapshot),native::digest(native::read_file(app)));snapshot.meta.abi_version=77;
        std::filesystem::remove(store/"S0.fvs");publish(store/"S0.fvs",Kind::snapshot,encode(snapshot));
        const auto output=out/(std::string(name)+"-manager");std::filesystem::create_directory(output);
        int result=0;{Restart<FixtureClient> experiment(args.get(L"--manager"),app,output,store);result=experiment.restore_phase(L"positive");}
        const auto bytes=native::read_file(output/"controller.jsonl");const std::string rows(bytes.begin(),bytes.end());
        const auto expected=alive?"snapshot.previous_lifetime_alive":"snapshot.runtime_abi";
        check_case(name,result!=0&&rows.find(std::string("\"api\":\"")+expected+"\"")!=std::string::npos&&rows.find("\"type\":\"process_created\"")==std::string::npos&&rows.find("\"type\":\"view_published\"")==std::string::npos);
        log.emit("identity_case","\"case\":"+json(name)+",\"syntheticDeclaration\":true,\"pid\":"+std::to_string(current.pid)+",\"actualCreated\":"+std::to_string(current.created)+",\"declaredCreated\":"+std::to_string(session.manager.created)+",\"expected\":"+json(expected));
    }
    log.emit("complete","\"passed\":"+std::string(failed?"false":"true")+",\"failed\":"+std::to_string(failed));return failed?2:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
