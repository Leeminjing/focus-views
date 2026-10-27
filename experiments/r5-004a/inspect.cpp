#include "desktop-diagnostic.hpp"
int wmain(int argc,wchar_t** argv){if(argc!=3)return 1;try{r305::Log log(argv[2]);r5004a::inspect_desktop(r3::utf8(argv[1]),log);return 0;}catch(const std::exception& e){std::cerr<<e.what();return 2;}}
