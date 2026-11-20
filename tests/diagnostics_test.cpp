#include "../src/platform.h"
#include "../src/diagnostics.h"
#include "../src/speech.h"
#include <cstdio>
#include <cstdlib>
using namespace a11y;
static void require(bool pass,const char* message){if(!pass){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
 auto dir=module_path().parent_path();auto root=dir/(L"cleanup-test-"+std::to_wstring(GetCurrentProcessId()));
 fs::create_directories(root/L"dump-9.pages");
 for(auto name:{L"accessibility.log",L"dump-1.txt",L"dump-2.pages",L"notes.txt",L"dump-custom.txt"})std::ofstream(root/name)<<"fixture";
 std::ofstream(root/L"dump-9.pages"/L"keep.txt")<<"fixture";
 auto cleanup=clear_previous_diagnostics(root);
 require(cleanup.removed==3&&!cleanup.failed,"clear only owned diagnostic files");
 require(fs::exists(root/L"notes.txt")&&fs::exists(root/L"dump-custom.txt")&&fs::exists(root/L"dump-9.pages"/L"keep.txt"),"retain unrelated files and nested directories");
 require(clear_previous_diagnostics(root).removed==0,"cleanup is idempotent");
 require(!diagnostic_filename(L"dump-.txt")&&!diagnostic_filename(L"dump-1.exe"),"restrict cleanup filenames");
 fs::remove(root/L"notes.txt");fs::remove(root/L"dump-custom.txt");fs::remove(root/L"dump-9.pages"/L"keep.txt");fs::remove(root/L"dump-9.pages");fs::remove(root);
 std::vector<std::string> lines;
 {Speech speech([&](const std::string& line){lines.push_back(line);});
  require(speech.init(dir),"load isolated fake Prism backend");
  require(speech.speak("Repeat selected item")&&speech.speak("Repeat selected item"),"repeat requests succeed");
  require(!speech.speak("fail"),"speech failure reported");
  require(speech.speak("fallback"),"fallback backend succeeds");speech.stop();
 }
 unsigned repeats=0;bool failure=false,fallback=false,stopped=false;
 for(const auto& line:lines){
  if(line=="SPEAK [NVDA test backend] Repeat selected item")repeats++;
  if(line=="Speech failed: synthetic speech failure; text=fail")failure=true;
  if(line=="SPEAK [Fallback test backend] fallback")fallback=true;
  if(line=="SPEECH_STOP [Fallback test backend] result=0")stopped=true;
 }
 require(repeats==2&&failure&&fallback&&stopped,"log every accepted utterance, failed request, fallback backend, and stop");
 std::puts("Diagnostic cleanup and speech logging tests passed (no real speech).");
}
