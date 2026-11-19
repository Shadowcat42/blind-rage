#include "platform.h"
#include "menu_reader.h"
#include "speech.h"
#include <atomic>
#include <mutex>
#include <sstream>
#include <objbase.h>
using namespace a11y;
static HMODULE self;
static std::mutex lifecycle;
static HANDLE worker;
static std::atomic<bool> stopping;
struct Page {std::array<unsigned char,4096> bytes{};bool valid{};};
static DWORD WINAPI run(void*) {
 try {
  auto dir=module_path(self).parent_path();auto bin=module_path().parent_path();auto logs=bin.parent_path()/L"userdata"/L"accessibility";fs::create_directories(logs);
  std::ofstream log(logs/L"accessibility.log",std::ios::app);auto write=[&](const std::string& s){log<<GetTickCount64()<<" "<<s<<std::endl;};
  std::string error;if(!supported(bin,error)){write(error);return 1;}
  auto runtime=GetModuleHandleW(L"rexruntime.dll");
  auto instance=reinterpret_cast<void*(*)()>(GetProcAddress(runtime,"?instance@Runtime@rex@@SAPEAV12@XZ"));
  auto membase=reinterpret_cast<unsigned char*(*)(void*)>(GetProcAddress(runtime,"?virtual_membase@Runtime@rex@@QEBAPEAEXZ"));
  if(!instance||!membase){write("Runtime accessors unavailable");return 2;}
  auto co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Speech speech(write);bool speaking=speech.init(dir);if(speaking)speech.speak("Blind Rage ready.");
  std::vector<std::wstring> commands={L"test",L"repeat",L"stop",L"mute",L"dump",L"shutdown"};std::vector<HANDLE> events;
  for(auto& c:commands)events.push_back(CreateEventW(nullptr,FALSE,FALSE,event_name(GetCurrentProcessId(),c).c_str()));
  const int defaults[]={VK_F1,VK_F2,VK_F3,VK_F5,VK_F8,0};int hotkeys[6]{};
  for(size_t i=0;i<commands.size();i++)hotkeys[i]=GetPrivateProfileIntW(L"hotkeys",commands[i].c_str(),defaults[i],(dir/L"accessibility.ini").c_str());
  Reader reader;std::string last,pending,lastSpeech,lastContext,spoken;unsigned stable=0;bool muted=false,wasFocused=false;ULONGLONG discoverAt=0;bool keys[6]{};
  ULONGLONG elapsedTotal=0,elapsedMax=0;unsigned pollCount=0;size_t pageMax=0;
  while(!stopping){
   auto pollStart=GetTickCount64();
   auto base=instance()?membase(instance()):nullptr;
   std::unordered_map<uint32_t,Page> pages;
   Memory memory([&](uint32_t at,void* out,size_t size){if(!base)return false;auto* dest=(unsigned char*)out;while(size){auto page=at&~4095u;auto [it,added]=pages.try_emplace(page);if(added){SIZE_T got{};it->second.valid=ReadProcessMemory(GetCurrentProcess(),base+page,it->second.bytes.data(),4096,&got)&&got==4096;}if(!it->second.valid)return false;auto count=std::min<size_t>(size,4096-(at&4095));memcpy(dest,it->second.bytes.data()+(at&4095),count);size-=count;at+=uint32_t(count);dest+=count;}return true;});
   if(base&&GetTickCount64()>=discoverAt){reader.discover(memory);discoverAt=GetTickCount64()+1000;}
   auto menus=base?reader.menus(memory):std::vector<Menu>{};
   DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);bool focused=foreground==GetCurrentProcessId();
   // A second matching poll prevents speaking a partially rebuilt selector.
   std::string current,context;if(!menus.empty()&&!menus.back().pending){auto& m=menus.back();context=std::to_string(m.movie)+":"+std::to_string(m.selector);current=context+"|"+describe(m,false,true);}
   if(current==pending)stable++;else{pending=current;stable=0;}
   if(stable>=(current.empty()?8u:1u)&&current!=last){bool entered=lastContext!=context;lastContext=context;last=current;if(!menus.empty()&&!menus.back().pending){lastSpeech=describe(menus.back(),entered,true);write("MENU "+lastSpeech);}else{lastSpeech.clear();spoken.clear();speech.stop();}}
   if(focused&&!muted&&speaking&&stable>=1&&!current.empty()&&(spoken!=current||!wasFocused)){
    if(!wasFocused&&!menus.empty())lastSpeech=describe(menus.back(),true,true);
    speech.speak(lastSpeech);spoken=current;
   }
   wasFocused=focused;
   for(size_t i=0;i<events.size();i++){
    int vk=hotkeys[i];
    bool down=focused&&vk&&(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_MENU)&0x8000)&&(GetAsyncKeyState(vk)&0x8000);
    bool trigger=WaitForSingleObject(events[i],0)==WAIT_OBJECT_0||(down&&!keys[i]);keys[i]=down;if(!trigger)continue;
    if(i==0){if(speaking)speech.speak("Prism speech test.");}
    if(i==1){if(speaking&&!muted)speech.speak(lastSpeech.empty()?"No supported menu is active.":lastSpeech);}
    if(i==2)speech.stop();
    if(i==3){muted=!muted;speech.stop();if(speaking)speech.speak(muted?"Menu narration off.":"Menu narration on.");}
    if(i==4){
     auto stem=L"dump-"+std::to_wstring(GetTickCount64());reader.discover(memory);
     std::ofstream dump(logs/(stem+L".txt"));reader.diagnostic(memory,dump);dump.close();
     std::ofstream raw(logs/(stem+L".pages"),std::ios::binary);uint32_t magic=0x41575031,count=0;
     for(const auto& [address,page]:pages)if(page.valid)count++;
     raw.write((char*)&magic,4);raw.write((char*)&count,4);
     for(const auto& [address,page]:pages)if(page.valid){raw.write((char*)&address,4);raw.write((const char*)page.bytes.data(),4096);}
     write("Saved "+utf8(stem)+".txt and .pages (raw UI diagnostics)");
    }
    if(i==5)stopping=true;
   }
   auto elapsed=GetTickCount64()-pollStart;elapsedTotal+=elapsed;elapsedMax=std::max(elapsedMax,elapsed);pageMax=std::max(pageMax,pages.size());
   if(++pollCount==300){write("PERF polls=300 average_ms="+std::to_string(double(elapsedTotal)/pollCount)+" max_ms="+std::to_string(elapsedMax)+" max_pages="+std::to_string(pageMax));pollCount=0;elapsedTotal=0;elapsedMax=0;pageMax=0;}
   Sleep(33);
  }
  speech.stop();speech.close();for(auto h:events)CloseHandle(h);if(SUCCEEDED(co))CoUninitialize();write("Accessibility stopped");
 }catch(...){OutputDebugStringA("Blind Rage worker failed\n");return 3;}return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI A11yInitialize(void*){std::lock_guard lock(lifecycle);if(worker&&WaitForSingleObject(worker,0)==WAIT_TIMEOUT)return 1;if(worker)CloseHandle(worker);stopping=false;worker=CreateThread(nullptr,0,run,nullptr,0,nullptr);return worker?1:0;}
extern "C" __declspec(dllexport) DWORD WINAPI A11yShutdown(void*){std::lock_guard lock(lifecycle);stopping=true;if(worker){if(WaitForSingleObject(worker,5000)!=WAIT_OBJECT_0)return 0;CloseHandle(worker);worker=nullptr;}return 1;}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*){if(reason==DLL_PROCESS_ATTACH){self=module;DisableThreadLibraryCalls(module);}return TRUE;}
