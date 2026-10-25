#include "../scenario/fixture_contract.hpp"
int wmain(int argc,wchar_t** argv){try{const r3::Args args(argc,argv);r3::demand(args.get(L"--role",L"fixture")==L"fixture","application cannot act as manager/helper");return r305::fixture_main(args);}catch(const std::exception& e){std::cerr<<"FIXTURE_FAILURE "<<e.what()<<'\n';return 1;}}
