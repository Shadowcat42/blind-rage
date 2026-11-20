#include "platform.h"
#include "menu_reader.h"
#include "speech.h"
#include "ui_hook_source.h"
#include "window_notifications.h"
#include "diagnostics.h"
#include "presentation_hooks.h"
#include "gameplay_text.h"
#include <atomic>
#include <mutex>
#include <objbase.h>
using namespace a11y;
static HMODULE self;
static std::mutex lifecycle;
static HANDLE worker;
static std::atomic<bool> stopping;
static std::atomic<bool> diagnosticsStarted;
struct Page {std::array<unsigned char,4096> bytes{};bool valid{};};
struct CaptureGuard {UiHookSource& source;~CaptureGuard(){source.capture(false);}};
struct PresentationGuard {PresentationHooks& source;~PresentationGuard(){source.capture(false);}};
static DWORD WINAPI run(void*) {
 try {
  auto dir=module_path(self).parent_path();auto bin=module_path().parent_path();auto logs=bin.parent_path()/L"userdata"/L"accessibility";fs::create_directories(logs);
  DiagnosticCleanup cleanup;if(!diagnosticsStarted.exchange(true))cleanup=clear_previous_diagnostics(logs);
  std::ofstream log(logs/L"accessibility.log",std::ios::app);auto write=[&](const std::string& s){log<<GetTickCount64()<<" "<<s<<std::endl;};
  write("SESSION pid="+std::to_string(GetCurrentProcessId())+" previous_diagnostics_removed="+std::to_string(cleanup.removed)+" cleanup_failures="+std::to_string(cleanup.failed));
  std::string error;auto profile=identify_build(bin,error);if(!profile||!supported_game_data(bin.parent_path(),*profile,error)){write(error);return 1;}
  write(std::string("BUILD profile=")+profile->id);
  auto runtime=GetModuleHandleW(L"rexruntime.dll");
  auto instance=reinterpret_cast<void*(*)()>(GetProcAddress(runtime,"?instance@Runtime@rex@@SAPEAV12@XZ"));
  auto membase=reinterpret_cast<unsigned char*(*)(void*)>(GetProcAddress(runtime,"?virtual_membase@Runtime@rex@@QEBAPEAEXZ"));
  if(!instance||!membase){write("Runtime accessors unavailable");return 2;}
  auto& source=UiHookSource::instance();
  if(!source.install(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),*profile)){write("UI hook installation failed; event reader not started");return 4;}
  CaptureGuard captureGuard{source};
  auto& presentations=PresentationHooks::instance();
  bool textHooks=presentations.install(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),*profile);
  if(!textHooks)write("Gameplay text hooks unavailable; menu reader remains active");
  PresentationGuard presentationGuard{presentations};
  auto co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Speech speech(write);bool speaking=speech.init(dir);if(speaking)speech.speak("Blind Rage ready.");
  std::vector<std::wstring> commands={L"test",L"repeat",L"stop",L"mute",L"dump",L"shutdown"};std::vector<HANDLE> events;
  for(auto& c:commands)events.push_back(CreateEventW(nullptr,FALSE,FALSE,event_name(GetCurrentProcessId(),c).c_str()));
  const int defaults[]={VK_F1,VK_F2,VK_F3,VK_F5,VK_F8,0};int hotkeys[6]{};
  for(size_t i=0;i<commands.size();i++)hotkeys[i]=GetPrivateProfileIntW(L"hotkeys",commands[i].c_str(),defaults[i],(dir/L"accessibility.ini").c_str());
  WindowNotifications notifications;if(!notifications.initialize(hotkeys)){write("Foreground notifications unavailable; reader stopped");for(auto h:events)CloseHandle(h);speech.close();if(SUCCEEDED(co))CoUninitialize();return 5;}
  for(unsigned i=0;i<6;i++)if(notifications.focused()&&hotkeys[i]&&!notifications.hotkey_registered(i))write("Hotkey unavailable: "+utf8(commands[i]));
  events.push_back(source.event());source.capture(true);
  Reader reader(profile->layout);std::string last,pending,lastSpeech,lastContext,spoken;
  bool muted=false,forceContext=notifications.focused(),scheduled=false,discovered=false;unsigned stable=0,retries=0;
  auto setting=[&](const wchar_t* key,int fallback){return GetPrivateProfileIntW(L"gameplay",key,fallback,(dir/L"accessibility.ini").c_str())!=0;};
  GameplayText gameplay({setting(L"subtitles",1),setting(L"tutorials",1)});
  bool interruptText=setting(L"interrupt",0);
  if(textHooks){events.push_back(presentations.event());presentations.capture(true);write("Gameplay text observer started (prototype)");}
  auto narrate_text=[&]{for(const auto& line:gameplay.take()){write("GAME_TEXT ["+line.channel+"] "+line.text);if(notifications.focused()&&!muted&&speaking)speech.speak(line.text,interruptText);}};
  ULONGLONG due=0,burst=0,emptySince=0;uint64_t reads=0;
  auto changed=[&]{auto now=GetTickCount64();if(!scheduled)burst=now;scheduled=true;due=std::min(now+20,burst+80);retries=10;};
  auto inspect=[&](bool dumpOnly){
   auto started=GetTickCount64();auto runtimeInstance=instance();auto base=runtimeInstance?membase(runtimeInstance):nullptr;
   std::unordered_map<uint32_t,Page> pages;
   Memory memory([&](uint32_t at,void* out,size_t size){if(!base)return false;auto* dest=(unsigned char*)out;while(size){auto page=at&~4095u;auto [it,added]=pages.try_emplace(page);if(added){SIZE_T got{};it->second.valid=ReadProcessMemory(GetCurrentProcess(),base+page,it->second.bytes.data(),4096,&got)&&got==4096;}if(!it->second.valid)return false;auto count=std::min<size_t>(size,4096-(at&4095));memcpy(dest,it->second.bytes.data()+(at&4095),count);size-=count;at+=uint32_t(count);dest+=count;}return true;});
   // Movie lifecycle and explicit requests refresh the registry. Cursor/value
   // changes reuse validated movie handles and only inspect their UI trees.
   bool discover=source.take_discovery()||dumpOnly||!discovered;
   if(base&&discover){reader.discover(memory);discovered=reader.ready(memory);}
   if(dumpOnly){
    auto stem=L"dump-"+std::to_wstring(GetTickCount64());
    std::ofstream dump(logs/(stem+L".txt"));reader.diagnostic(memory,dump);dump.close();
    std::ofstream raw(logs/(stem+L".pages"),std::ios::binary);uint32_t magic=0x41575031,count=0;
    for(const auto& [address,page]:pages)if(page.valid)count++;
    raw.write((char*)&magic,4);raw.write((char*)&count,4);
    for(const auto& [address,page]:pages)if(page.valid){raw.write((char*)&address,4);raw.write((const char*)page.bytes.data(),4096);}
    write("Saved "+utf8(stem)+".txt and .pages (raw UI diagnostics)");
    std::string counts;for(unsigned i=0;i<UiHookSource::hook_count;i++)counts+=" "+std::to_string(source.calls(i));
    write("EVENT_STATS reads="+std::to_string(reads)+" mutations="+std::to_string(source.changes())+" hook_calls="+counts);
    counts.clear();for(unsigned i=0;i<UiHookSource::hook_count;i++)counts+=" "+std::to_string(source.notifications(i));write("EVENT_SOURCES"+counts);return;
   }
   auto menus=base?reader.menus(memory):std::vector<Menu>{};
   for(auto movie:gameplay.movies())if(memory.u32(movie.pointer+0x20)!=movie.index||!(memory.u32(movie.pointer+0xc8)&0x80000000))gameplay.closed(movie);
   for(auto movie:gameplay.boxes()){
    if(memory.u32(movie.pointer+0x20)!=movie.index||!(memory.u32(movie.pointer+0xc8)&0x80000000)){gameplay.closed(movie);continue;}
    // Box tutorials are authored inside the movie rather than delivered as a
    // FString. Reuse the visible-tree decoder only after actual UI events.
    auto gfx=memory.u32(movie.pointer+0x180);if(memory.u32(gfx+0x40)!=0x48)continue;
    auto root=memory.u32(memory.u32(gfx+0x44)+4);std::string text;
    for(const auto& line:reader.text(memory,root)){if(text.size()+line.size()>8192)break;append_phrase(text,line);}
    gameplay.box_text(movie,std::move(text));
   }
   narrate_text();
   for(const auto& menu:menus)if(reader.class_name(memory,menu.movie)=="BCGFxUI_TitleMenu")source.watch_title(menu.movie);
   std::string current,context;bool incomplete=!menus.empty()&&menus.back().pending;
   if(!menus.empty()&&!incomplete){auto& menu=menus.back();context=menu_context(menu);current=context+":"+std::to_string(menu.selection_key)+"|"+describe(menu,false,true);}
   if(current==pending)stable++;else{pending=current;stable=0;}
   if(!current.empty())emptySince=0;else if(!emptySince)emptySince=GetTickCount64();
   bool settled=!incomplete&&(current.empty()?GetTickCount64()-emptySince>=250:stable>=1);
   if(settled){
    scheduled=false;
    if(current!=last){bool entered=lastContext!=context;lastContext=context;last=current;
     if(!menus.empty()){lastSpeech=describe(menus.back(),entered,true);write("MENU "+lastSpeech);}
     // Losing a menu clears our narration state, but must not cancel NVDA
     // speech that may belong to another application or notification.
     else{lastSpeech.clear();spoken.clear();}
    }
    if(notifications.focused()&&!muted&&speaking&&!current.empty()&&(spoken!=current||forceContext)){
     if(forceContext)lastSpeech=describe(menus.back(),true,true);
     speech.speak(lastSpeech);spoken=current;
    }
    forceContext=false;
   }else if(retries){--retries;scheduled=true;due=GetTickCount64()+(current.empty()&&!incomplete?250:33);}
   else scheduled=false;
   burst=GetTickCount64();
   ++reads;write("EVENT_READ "+std::to_string(reads)+" ms="+std::to_string(GetTickCount64()-started)+" pages="+std::to_string(pages.size())+" discover="+std::to_string(discover)+" mutations="+std::to_string(source.changes()));
  };
  auto command=[&](unsigned i){
   if(i==0&&speaking)speech.speak("Prism speech test.");
   if(i==1&&speaking&&!muted){auto text=last.empty()?gameplay.repeat():lastSpeech;speech.speak(text.empty()?"No supported text is active.":text);}
   if(i==2)speech.stop();
   if(i==3){muted=!muted;speech.stop();if(speaking)speech.speak(muted?"Menu narration off.":"Menu narration on.");}
   if(i==4)inspect(true);
   if(i==5)stopping=true;
  };
  write("Event reader started: assignment, raw assignment, movie open/close, text, display flags, title script assignment");
  while(!stopping){
   auto now=GetTickCount64();DWORD timeout=scheduled?DWORD(due>now?due-now:0):INFINITE;
   if(scheduled&&due<=now){inspect(false);continue;}
   auto wait=MsgWaitForMultipleObjectsEx(DWORD(events.size()),events.data(),timeout,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
   if(stopping)break;
   if(wait==WAIT_TIMEOUT){inspect(false);continue;}
   if(wait>=WAIT_OBJECT_0&&wait<WAIT_OBJECT_0+events.size()){
    auto index=wait-WAIT_OBJECT_0;
    if(index==commands.size())changed();
    else if(index==commands.size()+1){
     bool overflow=false;auto batch=presentations.take(overflow);
     if(overflow){gameplay.reset();write("GAME_TEXT_OVERFLOW discarded stale presentation batch");}
     else{for(const auto& event:batch){if(GetTickCount64()-event.tick>2000){gameplay.reset();continue;}gameplay.apply(event);}narrate_text();if(!gameplay.boxes().empty())changed();}
    }else command(index);
   }else if(wait==WAIT_OBJECT_0+events.size()){
    MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
     if(message.message==WindowNotifications::focus_message){bool was=notifications.focused();bool focused=notifications.update();if(focused&&!was){forceContext=true;source.request_discovery();changed();}}
     else if(message.message==WM_HOTKEY&&message.wParam>=100&&message.wParam<106&&notifications.focused())command(unsigned(message.wParam-100));
     else{TranslateMessage(&message);DispatchMessageW(&message);}
    }
   }else{write("Event wait failed: "+std::to_string(GetLastError()));break;}
  }
  presentations.capture(false);source.capture(false);speech.stop();speech.close();for(size_t i=0;i<commands.size();i++)CloseHandle(events[i]);if(SUCCEEDED(co))CoUninitialize();write("Accessibility stopped");
 }catch(...){OutputDebugStringA("Blind Rage worker failed\n");return 3;}return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI A11yInitialize(void*){std::lock_guard lock(lifecycle);if(worker&&WaitForSingleObject(worker,0)==WAIT_TIMEOUT)return 1;if(worker)CloseHandle(worker);stopping=false;worker=CreateThread(nullptr,0,run,nullptr,0,nullptr);return worker?1:0;}
extern "C" __declspec(dllexport) DWORD WINAPI A11yShutdown(void*){std::lock_guard lock(lifecycle);stopping=true;SetEvent(UiHookSource::instance().event());if(worker){if(WaitForSingleObject(worker,5000)!=WAIT_OBJECT_0)return 0;CloseHandle(worker);worker=nullptr;}return 1;}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*){if(reason==DLL_PROCESS_ATTACH){self=module;DisableThreadLibraryCalls(module);}return TRUE;}
