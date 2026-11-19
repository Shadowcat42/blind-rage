#include "../src/speech.h"
#include <objbase.h>
#include <iostream>
using namespace a11y;
int wmain(int argc,wchar_t** argv){
 auto dir=module_path().parent_path();auto root=dir.parent_path().parent_path().parent_path();
 SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);auto cookie=AddDllDirectory((root/L"bin_disc").c_str());
 auto co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int status=1;
 {Speech speech([](const std::string& s){std::cout<<s<<std::endl;});if(speech.init(dir)){status=0;if(argc==2&&std::wstring(argv[1])==L"--speak"){status=speech.speak("Asura accessibility. Prism speech test.")?0:2;Sleep(4000);}}}
 if(SUCCEEDED(co))CoUninitialize();if(cookie)RemoveDllDirectory(cookie);return status;
}
