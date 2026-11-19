#pragma once
#include "native_hooks.h"
#include "ui_changes.h"
#include "display_changes.h"
#include <atomic>
#include <algorithm>
#include <windows.h>

namespace a11y {
// Process-lifetime storage: restored entry points alone cannot prove that a
// callback already running has left its trampoline. The injected DLL remains
// loaded, so keep trampolines and the signal alive until process teardown.
class UiHookSource {
 public:
  using Function=void(*)(void*,uint8_t*);
  static constexpr unsigned hook_count=10;
  static UiHookSource& instance(){static auto* source=new UiHookSource;return *source;}
  HANDLE event() const{return event_;}
  bool install(uintptr_t executable){
   constexpr uintptr_t rvas[]={0x029b5050,0x045f72b0,0x01a647d0,0x0212c6c0,0x03b841c0,0x02b146b0,0x0040ce60,0x008071c0,0x0164bd00,0x01ce3ce0};
   std::array<void*,hook_count> targets{};for(unsigned i=0;i<hook_count;i++)targets[i]=(void*)(executable+rvas[i]);
   return install_targets(targets);
  }
  bool install_targets(const std::array<void*,hook_count>& targets){
   if(installed_)return true;
   if(!event_||!hooks_.initialize())return false;
   void* callbacks[]={(void*)assignment,(void*)raw_assignment,(void*)opened,(void*)closed,(void*)text,(void*)display,(void*)script_assignment,(void*)advance,(void*)visibility,(void*)sprite_visibility};
   for(unsigned i=0;i<hook_count;i++)if(!hooks_.add(targets[i],callbacks[i],(void**)&original_[i])){hooks_.clear();return false;}
   if(!hooks_.enable()){hooks_.clear();return false;}
   installed_=true;return true;
  }
  void capture(bool enabled){enabled_.store(enabled);epoch_.fetch_add(1);if(enabled){discovery_=true;SetEvent(event_);}}
  void request_discovery(){discovery_=true;}
  bool take_discovery(){return discovery_.exchange(false);}
  uint64_t changes() const{return changes_.load();}
  uint64_t calls(unsigned i) const{return i<hook_count?calls_[i].load():0;}
  uint64_t notifications(unsigned i) const{return i<hook_count?notifications_[i].load():0;}
  void watch_title(uint32_t movie){title_.store(movie,std::memory_order_relaxed);}
 private:
  UiHookSource():event_(CreateEventW(nullptr,FALSE,FALSE,nullptr)){}
  struct Read {
   uint8_t* base;
   bool operator()(uint32_t at,void* out,size_t n) const noexcept {
    if(!at||uint64_t(at)+n>0x100000000ull)return false;
    SIZE_T got{};return ReadProcessMemory(GetCurrentProcess(),base+at,out,n,&got)&&got==n;
   }
  };
  void changed(unsigned kind){changes_.fetch_add(1,std::memory_order_relaxed);notifications_[kind].fetch_add(1,std::memory_order_relaxed);SetEvent(event_);}
  template<unsigned I> static void member(void* context,uint8_t* base){
   auto& self=instance();
   if(!self.enabled_.load(std::memory_order_relaxed)){self.original_[I](context,base);return;}
   self.calls_[I].fetch_add(1,std::memory_order_relaxed);
   Read read{base};auto before=ui_before_assignment(read,context);
   self.original_[I](context,base);
   if(ui_after_assignment(read,before))self.changed(I);
  }
  static void assignment(void* c,uint8_t* b){member<0>(c,b);}
  static void raw_assignment(void* c,uint8_t* b){member<1>(c,b);}
  template<unsigned I> static void lifecycle(void* context,uint8_t* base){
   auto& self=instance();const bool enabled=self.enabled_.load(std::memory_order_relaxed);
   const auto movie=ui_argument(context,3);
   self.original_[I](context,base);
   if(enabled){
    if constexpr(I==2){
     Read read{base};auto cls=ui_word(read,movie+0x34),index=ui_word(read,cls+0x2c);
     auto names=ui_word(read,0x832a9c58),entry=ui_word(read,names+index*4);char name[32]{};
     if(entry&&read(entry+8,name,sizeof(name))&&!std::memcmp(name,"BCGFxUI_TitleMenu",18))self.watch_title(movie);
    }else if(self.title_.load(std::memory_order_relaxed)==movie)self.watch_title(0);
    self.discovery_=true;self.calls_[I].fetch_add(1,std::memory_order_relaxed);self.changed(I);
   }
  }
  static void opened(void* c,uint8_t* b){lifecycle<2>(c,b);}
  static void closed(void* c,uint8_t* b){lifecycle<3>(c,b);}
  static uint64_t text_hash(Read read,uint32_t character){
   auto view=ui_word(read,character+0xbc),styled=ui_word(read,view+8);
   auto array=ui_word(read,styled+0xc),count=ui_word(read,styled+0x10);
   uint64_t hash=14695981039346656037ull;
   if(count>256)return 0;
   for(uint32_t i=0,total=0;i<count;i++){
    auto p=ui_word(read,array+i*4),data=ui_word(read,p),size=ui_word(read,p+4);
    if(size>32768||total+size>32768)return 0;total+=size;
    std::array<uint8_t,256> bytes{};
    for(uint32_t offset=0;offset<size*2;){auto n=std::min<uint32_t>(bytes.size(),size*2-offset);if(!read(data+offset,bytes.data(),n))return 0;for(uint32_t j=0;j<n;j++){hash^=bytes[j];hash*=1099511628211ull;}offset+=n;}
    hash^=0xff;hash*=1099511628211ull;
   }
   return hash;
  }
  static void text(void* context,uint8_t* base){
   auto& self=instance();if(!self.enabled_.load(std::memory_order_relaxed)){self.original_[4](context,base);return;}
   self.calls_[4].fetch_add(1,std::memory_order_relaxed);
   const auto character=ui_argument(context,3);const auto before=text_hash({base},character);
   self.original_[4](context,base);
   const auto after=text_hash({base},character);
   if(!before||!after||before!=after)self.changed(4);
  }
  static DisplayChanges& display_batch(UiHookSource& self){
   static thread_local DisplayChanges batch;
   static thread_local uint64_t epoch;
   auto current=self.epoch_.load(std::memory_order_relaxed);if(epoch!=current){batch.clear();epoch=current;}return batch;
  }
  template<unsigned I> static void display_change(void* context,uint8_t* base){
   auto& self=instance();if(!self.enabled_.load(std::memory_order_relaxed)){self.original_[I](context,base);return;}
   self.calls_[I].fetch_add(1,std::memory_order_relaxed);
   Read read{base};const auto character=ui_argument(context,3);const auto before=ui_word(read,character+0xa0)&3;
   self.original_[I](context,base);
   if(before!=(ui_word(read,character+0xa0)&3))display_batch(self).record(character,before);
  }
  static void display(void* c,uint8_t* b){display_change<5>(c,b);}
  static void visibility(void* c,uint8_t* b){display_change<8>(c,b);}
  static void sprite_visibility(void* c,uint8_t* b){display_change<9>(c,b);}
  static void advance(void* context,uint8_t* base){
   auto& self=instance();self.original_[7](context,base);
   if(!self.enabled_.load(std::memory_order_relaxed))return;
   self.calls_[7].fetch_add(1,std::memory_order_relaxed);
   Read read{base};if(display_batch(self).flush([&](uint32_t object){return ui_word(read,object+0xa0)&3;}))self.changed(7);
  }
  static void script_assignment(void* context,uint8_t* base){
   auto& self=instance();const auto movie=ui_argument(context,3);
   if(!self.enabled_.load(std::memory_order_relaxed)||!movie||movie!=self.title_.load(std::memory_order_relaxed)){self.original_[6](context,base);return;}
   self.calls_[6].fetch_add(1,std::memory_order_relaxed);
   Read read{base};uint8_t before{},after{};const bool valid=read(movie+0x1f1,&before,1);
   self.original_[6](context,base);
   if(valid&&read(movie+0x1f1,&after,1)&&before!=after)self.changed(6);
  }
  NativeHooks hooks_;
  HANDLE event_{};
  bool installed_{};
  std::atomic<bool> enabled_{};
  std::atomic<bool> discovery_{true};
  std::atomic<uint64_t> epoch_{};
  std::atomic<uint64_t> changes_{};
  std::atomic<uint32_t> title_{};
  std::array<std::atomic<uint64_t>,hook_count> calls_{};
  std::array<std::atomic<uint64_t>,hook_count> notifications_{};
  std::array<Function,hook_count> original_{};
};
}
