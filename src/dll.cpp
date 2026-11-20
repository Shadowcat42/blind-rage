#include "platform.h"
#include "menu_reader.h"
#include "speech.h"
#include "ui_hook_source.h"
#include "window_notifications.h"
#include "diagnostics.h"
#include "presentation_hooks.h"
#include "gameplay_text.h"
#include "recomp_prompts.h"
#include "gameplay_status.h"
#include "gameplay_hooks.h"
#include "synchronic_reader.h"
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
struct GameplayGuard {GameplayHooks& source;~GameplayGuard(){source.capture(false);}};
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
  auto& gameHooks=GameplayHooks::instance();bool nativeGameplay=gameHooks.install(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),*profile);
  if(!nativeGameplay)write("Gameplay state hooks unavailable; QTEs and shooting aid not started");
  GameplayGuard gameplayGuard{gameHooks};
  auto co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Speech speech(write);bool speaking=speech.init(dir);if(speaking)speech.speak("Blind Rage ready.");
  std::vector<std::wstring> commands={L"test",L"repeat",L"stop",L"mute",L"dump",L"shutdown",L"status"};std::vector<HANDLE> events;
  for(auto& c:commands)events.push_back(CreateEventW(nullptr,FALSE,FALSE,event_name(GetCurrentProcessId(),c).c_str()));
  const int defaults[]={VK_F1,VK_F2,VK_F3,VK_F5,VK_F8,0,VK_F4};int hotkeys[7]{};
  for(size_t i=0;i<commands.size();i++)hotkeys[i]=GetPrivateProfileIntW(L"hotkeys",commands[i].c_str(),defaults[i],(dir/L"accessibility.ini").c_str());
  WindowNotifications notifications;if(!notifications.initialize(hotkeys)){write("Foreground notifications unavailable; reader stopped");for(auto h:events)CloseHandle(h);speech.close();if(SUCCEEDED(co))CoUninitialize();return 5;}
  for(unsigned i=0;i<commands.size();i++)if(notifications.focused()&&hotkeys[i]&&!notifications.hotkey_registered(i))write("Hotkey unavailable: "+utf8(commands[i]));
  events.push_back(source.event());source.capture(true);
  Reader reader(profile->layout);std::string last,pending,lastSpeech,lastContext,spoken;
  bool muted=false,forceContext=notifications.focused(),scheduled=false,discovered=false,metadataReady=false;unsigned stable=0,retries=0;
  auto setting=[&](const wchar_t* key,int fallback){return GetPrivateProfileIntW(L"gameplay",key,fallback,(dir/L"accessibility.ini").c_str())!=0;};
  RecompPrompts prompts;if(!prompts.initialize(runtime,reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),*profile))write("Recomp prompt labels unavailable");
  GameplayText gameplay({setting(L"subtitles",1),setting(L"tutorials",1)},[&](std::string_view tag){return prompts.button(tag);});
  GameplayStatus status([&](std::string_view tag){return prompts.button(tag);},GetPrivateProfileIntW(L"gameplay",L"low_health_percent",25,(dir/L"accessibility.ini").c_str()));
  bool statusWarnings=setting(L"status_warnings",1);
  bool interruptText=setting(L"interrupt",0);
  bool qteEnabled=setting(L"qtes",1),interruptQte=setting(L"qte_interrupt",1);int shootingDegrees=std::clamp<int>(GetPrivateProfileIntW(L"gameplay",L"shooting_cone_degrees",45,(dir/L"accessibility.ini").c_str()),1,89);
  gameHooks.options(textHooks&&qteEnabled,setting(L"shooting_assist",1)?float(shootingDegrees):0.f);
  QteReader qtes([&](std::string_view tag){return prompts.button(tag);},[&](std::string_view tag){return prompts.gesture(tag);});
  SynchronicReader timing([&](std::string_view tag){return prompts.button(tag);});
  if(textHooks){events.push_back(presentations.event());presentations.capture(true);write("Gameplay text observer started (prototype)");}
  if(nativeGameplay){events.push_back(gameHooks.event());gameHooks.capture(true);write("Gameplay state observer started; qtes="+std::to_string(qteEnabled)+" shooting_degrees="+std::to_string(setting(L"shooting_assist",1)?shootingDegrees:0));}
  auto narrate_text=[&]{for(const auto& line:gameplay.take()){write("GAME_TEXT ["+line.channel+"] "+line.text);if(notifications.focused()&&!muted&&speaking)speech.speak(line.text,interruptText);}};
  auto narrate_status=[&]{for(const auto& line:status.take()){write("GAME_STATUS "+line);if(statusWarnings&&notifications.focused()&&!muted&&speaking)speech.speak(line,false);}};
  auto movie_class=[&](const Memory& memory,uint32_t movie,std::string_view expected){for(auto cls=memory.u32(movie+0x34),depth=0u;cls&&depth<20;cls=memory.u32(cls+0x40),++depth)if(reader.object_name(memory,cls)==expected)return true;return false;};
  auto validate_status=[&](const Memory& memory){for(auto movie:status.movies())if(memory.u32(movie.pointer+0x20)!=movie.index||!(memory.u32(movie.pointer+0xc8)&0x80000000)||(!movie_class(memory,movie.pointer,"BCGFxUI_Gauge")&&!movie_class(memory,movie.pointer,"BCGFxMovieButton")))status.closed(movie);};
  auto validate_qtes=[&](const Memory& memory){
   auto validate=[&](auto& cueReader,std::string_view expected){for(auto movie:cueReader.movies()){
    if(memory.u32(movie.pointer+0x20)!=movie.index||!(memory.u32(movie.pointer+0xc8)&0x80000000)||!movie_class(memory,movie.pointer,expected)){cueReader.closed(movie);continue;}
    auto gfx=memory.u32(movie.pointer+0x180);if(memory.u32(gfx+0x40)==0x48){auto root=memory.u32(memory.u32(gfx+0x44)+4);if(root)cueReader.visibility(movie,reader.visible(memory,root));}
   }};validate(qtes,"BCGFxMovieButton");validate(timing,"BCGFxUI_Synchronic");
  };
  auto narrate_qtes=[&]{
   auto lines=timing.take();auto normal=qtes.take();lines.insert(lines.end(),normal.begin(),normal.end());std::string combined;
   for(const auto& line:lines){write("GAME_QTE "+line);append_phrase(combined,line);}
   if(qteEnabled&&!combined.empty()&&notifications.focused()&&!muted&&speaking)speech.speak(combined,interruptQte);
  };
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
   if(base&&nativeGameplay&&discover&&!metadataReady&&gameHooks.configure_names(memory,reader)){metadataReady=true;write("GAMEPLAY_METADATA verified function-name filter ready");}
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
    counts.clear();for(unsigned i=0;i<UiHookSource::hook_count;i++)counts+=" "+std::to_string(source.notifications(i));write("EVENT_SOURCES"+counts);
    write("GAMEPLAY_STATS captured="+std::to_string(gameHooks.captured())+" capture_failures="+std::to_string(gameHooks.failures())+" widened_checks="+std::to_string(gameHooks.widenings()));return;
   }
   auto menus=base?reader.menus(memory):std::vector<Menu>{};
   validate_status(memory);
   validate_qtes(memory);narrate_qtes();
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
   if(i==1&&speaking&&!muted){auto text=lastSpeech;if(last.empty()){text=timing.repeat();if(text.empty())text=qtes.repeat();if(text.empty())text=gameplay.repeat();}speech.speak(text.empty()?"No supported text is active.":text);}
   if(i==2)speech.stop();
   if(i==3){muted=!muted;speech.stop();if(speaking)speech.speak(muted?"Narration off.":"Narration on.");}
   if(i==4)inspect(true);
   if(i==5)stopping=true;
   if(i==6&&speaking&&!muted){
    auto runtimeInstance=instance();auto base=runtimeInstance?membase(runtimeInstance):nullptr;
    Memory memory([&](uint32_t at,void* out,size_t size){SIZE_T got{};return base&&ReadProcessMemory(GetCurrentProcess(),base+at,out,size,&got)&&got==size;});
    validate_status(memory);auto text=status.status();write("GAME_STATUS_REQUEST "+text);speech.speak(text,false);
   }
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
    else if(index>commands.size()){
     bool overflow=false,nativeOverflow=false;auto batch=presentations.take(overflow);auto native=gameHooks.take(nativeOverflow);
     auto now=GetTickCount64();bool stale=std::any_of(batch.begin(),batch.end(),[&](const PresentationEvent& event){return now-event.tick>2000;})||std::any_of(native.begin(),native.end(),[&](const GameplayEvent& event){return now-event.tick>200;});
     if(overflow||nativeOverflow||stale){gameplay.reset();status.reset();qtes.reset();timing.reset();if(textHooks)presentations.capture(true);if(nativeGameplay)gameHooks.capture(true);write("GAME_CUE_RESET discarded stale/overflowed batch; resynchronizing producers");}
     else{
      auto runtimeInstance=instance();auto base=runtimeInstance?membase(runtimeInstance):nullptr;Memory memory([&](uint32_t at,void* out,size_t n){SIZE_T got{};return base&&ReadProcessMemory(GetCurrentProcess(),base+at,out,n,&got)&&got==n;});
      auto name=[&](CueName value){auto text=reader.name(memory,value.index);if(value.number&&!text.empty())text+="_"+std::to_string(value.number-1);return text;};
      struct Ordered {uint64_t order;bool native;size_t index;};std::vector<Ordered> order;for(size_t i=0;i<batch.size();++i)order.push_back({batch[i].order,false,i});for(size_t i=0;i<native.size();++i)order.push_back({native[i].order,true,i});
      std::sort(order.begin(),order.end(),[](const Ordered& left,const Ordered& right){return left.order<right.order;});
      for(const auto& item:order){
       if(!item.native){const auto& event=batch[item.index];gameplay.apply(event);status.apply(event);qtes.apply(event);timing.apply(event);
        if(event.kind==PresentationKind::button_create)write("QTE_ICON tag="+event.tag.utf8()+" input="+event.type.utf8()+" revision="+std::to_string(event.revision));
       }else{const auto& event=native[item.index];
        if(event.timing){timing.apply(event.sync,event.order,name);write("TIMING_STATE source="+std::to_string(event.sync.source)+" track="+std::to_string(event.sync.track)+" active="+std::to_string(event.sync.active)+" window="+std::to_string(event.sync.window));}
        else{qtes.apply(event.group,event.order,name);unsigned visible=0;for(unsigned i=0;i<event.group.count;++i)visible+=event.group.commands[i].visible;write("QTE_STATE source="+std::to_string(event.group.source)+" active="+std::to_string(event.group.active)+" mode="+std::to_string(event.group.mode)+" visible="+std::to_string(visible)+" required="+std::to_string(event.group.required));}
       }
      }
      validate_status(memory);validate_qtes(memory);narrate_text();narrate_status();narrate_qtes();if(!gameplay.boxes().empty())changed();
     }
    }else command(index);
   }else if(wait==WAIT_OBJECT_0+events.size()){
    MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
     if(message.message==WindowNotifications::focus_message){bool was=notifications.focused();bool focused=notifications.update();if(focused&&!was){forceContext=true;source.request_discovery();changed();}}
     else if(message.message==WM_HOTKEY&&message.wParam>=100&&message.wParam<100+commands.size()&&notifications.focused())command(unsigned(message.wParam-100));
     else{TranslateMessage(&message);DispatchMessageW(&message);}
    }
   }else{write("Event wait failed: "+std::to_string(GetLastError()));break;}
  }
  gameHooks.capture(false);presentations.capture(false);source.capture(false);speech.stop();speech.close();for(size_t i=0;i<commands.size();i++)CloseHandle(events[i]);if(SUCCEEDED(co))CoUninitialize();write("Accessibility stopped");
 }catch(...){OutputDebugStringA("Blind Rage worker failed\n");return 3;}return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI A11yInitialize(void*){std::lock_guard lock(lifecycle);if(worker&&WaitForSingleObject(worker,0)==WAIT_TIMEOUT)return 1;if(worker)CloseHandle(worker);stopping=false;worker=CreateThread(nullptr,0,run,nullptr,0,nullptr);return worker?1:0;}
extern "C" __declspec(dllexport) DWORD WINAPI A11yShutdown(void*){std::lock_guard lock(lifecycle);stopping=true;SetEvent(UiHookSource::instance().event());if(worker){if(WaitForSingleObject(worker,5000)!=WAIT_OBJECT_0)return 0;CloseHandle(worker);worker=nullptr;}return 1;}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*){if(reason==DLL_PROCESS_ATTACH){self=module;DisableThreadLibraryCalls(module);}return TRUE;}
