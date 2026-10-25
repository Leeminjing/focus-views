#include "runtime/abi.hpp"
#include "../r4-001-gate-001/courier.hpp"
int wmain(int argc,wchar_t** argv){try{const r3::Args args(argc,argv);const auto role=args.get(L"--role");if(role==L"bridge")return r3::bridge_main(args);if(role==L"input")return r304::input_main(args);if(role==L"courier")return gate001::courier_main(args);throw r3::Error("helper.role",ERROR_INVALID_PARAMETER);}catch(const std::exception& e){std::cerr<<"HELPER_FAILURE "<<e.what()<<'\n';return 1;}}
