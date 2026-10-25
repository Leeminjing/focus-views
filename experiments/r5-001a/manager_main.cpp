#include "runtime/abi.hpp"
#include "runtime/native.hpp"
namespace r305 {int run_scenario(const std::filesystem::path&,const std::filesystem::path&,const std::filesystem::path&,const std::wstring&);}
int wmain(int argc,wchar_t** argv){
    try{const r3::Args args(argc,argv);r3::demand(args.get(L"--role").empty(),"manager has no fixture/helper role");
        wchar_t own[32768]{};r3::check(GetModuleFileNameW(nullptr,own,32768)!=0,"GetModuleFileNameW.manager");
        const auto app=std::filesystem::absolute(args.get(L"--fixture",(std::filesystem::path(own).parent_path()/"fixture.exe").wstring()));
        const auto out=std::filesystem::absolute(args.get(L"--output"));r3::demand(!args.get(L"--output").empty()&&!std::filesystem::exists(out),"new output directory required");std::filesystem::create_directories(out);
        return r305::run_scenario(own,app,out,args.get(L"--scenario",L"positive"));
    }catch(const std::exception& e){std::cerr<<"MANAGER_FAILURE "<<e.what()<<'\n';return 1;}
}
