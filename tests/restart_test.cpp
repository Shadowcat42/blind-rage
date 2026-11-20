#include "../src/restart_hook.h"
#include <shellapi.h>
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> static bool rejects(F run){try{run();return false;}catch(const std::exception&){return true;}}
static std::wstring applicationSeen,commandSeen,directorySeen;
static uintptr_t callerSeen{};static unsigned calls{};static DWORD flagsSeen{};static BOOL inheritSeen{};
static LPVOID environmentSeen{};static bool successful=true;
static BOOL WINAPI fake_create(LPCWSTR application,LPWSTR command,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,LPSTARTUPINFOW,LPPROCESS_INFORMATION process){
 ++calls;callerSeen=reinterpret_cast<uintptr_t>(__builtin_return_address(0));applicationSeen=application?application:L"";commandSeen=command?command:L"";directorySeen=directory?directory:L"";flagsSeen=flags;inheritSeen=inherit;environmentSeen=environment;
 if(successful&&process)process->dwProcessId=123;
 SetLastError(successful?ERROR_SUCCESS:ERROR_ACCESS_DENIED);return successful;
}
static RestartHook::Function volatile importSlot=fake_create;
static wchar_t originalCommand[]=L"\"C:\\Game Folder\\bin\\asura_wrath_recomp.exe\" --example \"quoted value\"";
static PROCESS_INFORMATION processInfo{};
static __declspec(noinline) BOOL native_restart(){
 STARTUPINFOW startup{};startup.cb=sizeof(startup);
 volatile BOOL result=importSlot(L"C:\\Game Folder\\bin\\asura_wrath_recomp.exe",originalCommand,nullptr,nullptr,FALSE,0,reinterpret_cast<void*>(0x1234),L"C:\\Game Folder\\bin",&startup,&processInfo);
 return result;
}
int wmain(int argc,wchar_t** argv){try{
 if(argc==3&&std::wstring_view(argv[1])==L"--wait-event"){
  auto handle=reinterpret_cast<HANDLE>(uintptr_t(wcstoull(argv[2],nullptr,10)));
  return WaitForSingleObject(handle,5000)==WAIT_OBJECT_0?0:2;
 }
 for(const auto& value:std::array<std::wstring,6>{L"",L"plain",L"folder with spaces\\",L"quote\" and \\\"",L"C:\\Game Folder\\",L"Unicode \u00e9 \u65e5\"\\"}){
  auto line=L"fixture.exe "+windows_argument(value);int count{};auto parsed=CommandLineToArgvW(line.c_str(),&count);
  check(parsed&&count==2&&std::wstring(parsed[1])==value,"restart argument quoting must survive Windows argv parsing");LocalFree(parsed);
 }
 check(restart_parent_id(L"123")==123,"valid parent PID");
 for(auto bad:{L"",L"0",L"-1",L"+5",L" 5",L"5junk",L"4294967296",L"999999999999999999999"})check(rejects([&]{restart_parent_id(bad);}),"malformed restart parent rejected");
 check(rejects([&]{wait_for_restart_parent(GetCurrentProcessId(),module_path(),0);}),"cannot wait on the launcher itself");
 // Only this test's hidden, bounded child process is started; never the game.
 SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE event=CreateEventW(&security,TRUE,FALSE,nullptr);check(event!=nullptr,"fixture event");
 auto self=module_path();auto childLine=windows_argument(self.wstring())+L" --wait-event "+std::to_wstring(uintptr_t(event));STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
 check(CreateProcessW(self.c_str(),childLine.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child),"create hidden restart fixture");
 check(rejects([&]{wait_for_restart_parent(child.dwProcessId,self.parent_path()/L"wrong-game.exe",0);}),"parent from another installation rejected");
 check(rejects([&]{wait_for_restart_parent(child.dwProcessId,self,0);})&&WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT,"timeout leaves the old process alive");
 SetEvent(event);wait_for_restart_parent(child.dwProcessId,self,3000);check(WaitForSingleObject(child.hProcess,0)==WAIT_OBJECT_0,"wait completes only after the old process exits");
 wait_for_restart_parent(child.dwProcessId,self,0);CloseHandle(child.hThread);CloseHandle(child.hProcess);CloseHandle(event);
 check(native_restart(),"fixture native restart");auto nativeCaller=callerSeen;
 auto& hook=RestartHook::instance();check(hook.install_slot(reinterpret_cast<void**>(const_cast<RestartHook::Function*>(&importSlot)),nativeCaller,L"C:\\Game Folder\\BlindRage.exe",L"C:\\Game Folder\\bin\\asura_wrath_recomp.exe","updated"),"install process-local import shim");hook.capture(true);
 auto before=calls;check(native_restart()&&calls==before+1&&processInfo.dwProcessId==123,"redirect preserves CreateProcess return and process info");
 check(applicationSeen==L"C:\\Game Folder\\BlindRage.exe"&&flagsSeen==CREATE_NO_WINDOW&&!inheritSeen&&environmentSeen==reinterpret_cast<void*>(0x1234)&&directorySeen==L"C:\\Game Folder\\bin","only launcher application/arguments and hidden-console flag change");
 int count{};auto args=CommandLineToArgvW(commandSeen.c_str(),&count);
 check(args&&count==7&&std::wstring(args[1])==L"--build"&&std::wstring(args[2])==L"updated"&&std::wstring(args[3])==L"--restart-parent"&&restart_parent_id(args[4])==GetCurrentProcessId()&&std::wstring(args[5])==L"--restart-command"&&std::wstring(args[6])==originalCommand,"build, parent and original game command survive redirect");LocalFree(args);
 auto redirected=hook.redirects();successful=false;before=calls;
 check(!native_restart()&&GetLastError()==ERROR_ACCESS_DENIED&&calls==before+1&&hook.redirects()==redirected,"launcher creation failure preserves native failure without launching an unmodded fallback");successful=true;
 std::wstring unrelated=L"other";before=calls;check(importSlot(L"other.exe",unrelated.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED,nullptr,nullptr,&startup,&processInfo)&&calls==before+1&&applicationSeen==L"other.exe"&&flagsSeen==CREATE_SUSPENDED&&inheritSeen,"unrelated process creation passes through");
 auto same=std::wstring(originalCommand);check(importSlot(L"C:\\Game Folder\\bin\\asura_wrath_recomp.exe",same.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&processInfo)&&applicationSeen.find(L"asura_wrath_recomp.exe")!=std::wstring::npos,"same executable from another call site passes through");
 hook.capture(false);check(native_restart()&&applicationSeen.find(L"asura_wrath_recomp.exe")!=std::wstring::npos,"shutdown keeps an in-flight-safe pass-through shim");
 std::cout<<"Settings restart redirect, argument and parent-wait tests passed (no game or speech).\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
