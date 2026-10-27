#include "../r5-001a/runtime/abi.hpp"
#include <filesystem>
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
    const r3::Args args(argc,argv);wchar_t path[32768]{};r3::check(GetModuleFileNameW(nullptr,path,32768)!=0,"GetModuleFileNameW.manager");const auto here=std::filesystem::path(path).parent_path();
    const auto client=std::filesystem::absolute(args.get(L"--client",(here/"fixture-client.dll").wstring()));const auto app=std::filesystem::absolute(args.get(L"--fixture",(here/"fixture.exe").wstring()));const auto output=std::filesystem::absolute(args.get(L"--output"));r3::demand(!args.get(L"--output").empty()&&!std::filesystem::exists(output),"new output directory required");
    const auto module=LoadLibraryExW(client.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);r3::check(module!=nullptr,"LoadLibraryExW.crash_client");struct Release{HMODULE value;~Release(){FreeLibrary(value);}} release{module};
    using Version=unsigned(*)();using Run=int(*)(const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*);
    const auto version=reinterpret_cast<Version>(GetProcAddress(module,"ClientVersion"));const auto run=reinterpret_cast<Run>(GetProcAddress(module,"RunCrashClient"));r3::demand(version&&run&&version()==4,"crash client ABI");std::filesystem::create_directories(output);
    const auto store=std::filesystem::absolute(args.get(L"--store"));r3::demand(!args.get(L"--store").empty()&&!args.get(L"--gate").empty(),"explicit store and independent supervisor gate required");return run(path,app.c_str(),output.c_str(),args.get(L"--mode",L"restore").c_str(),store.c_str(),args.get(L"--scenario",L"positive").c_str(),args.get(L"--gate").c_str(),args.get(L"--previous").c_str());
}catch(const std::exception& e){std::cerr<<"MANAGER_FAILURE "<<e.what()<<'\n';return 1;}}
