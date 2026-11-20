#pragma once
#include "build_profile.h"
#include "native_hooks.h"
#include "presentation_events.h"
#include <atomic>
#include <windows.h>

namespace a11y {
class PresentationHooks {
 public:
  using Function=void(*)(void*,uint8_t*);
  static PresentationHooks& instance(){static auto* value=new PresentationHooks;return *value;}
  bool install(uintptr_t executable,const BuildProfile& profile){
   if(installed_)return profile_==&profile;profile_=&profile;names_global_=profile.layout.names_global;
   return install_targets({(void*)(executable+profile.presentation_rvas[0]),(void*)(executable+profile.presentation_rvas[1])});
  }
  bool install_targets(const std::array<void*,2>& targets){
   if(installed_)return true;if(!event_||!hooks_.initialize())return false;
   void* callbacks[]={(void*)object_call,(void*)movie_call};
   for(unsigned i=0;i<2;i++)if(!hooks_.add(targets[i],callbacks[i],(void**)&original_[i])){hooks_.clear();return false;}
   if(!hooks_.enable()){hooks_.clear();return false;}installed_=true;return true;
  }
  void capture(bool enabled){enabled_=false;++epoch_;queue_.clear();enabled_=enabled;}
  HANDLE event() const{return event_;}
  std::vector<PresentationEvent> take(bool& overflow){return queue_.take(overflow);}
  uint64_t captured() const{return captured_.load();}
 private:
  PresentationHooks():event_(CreateEventW(nullptr,FALSE,FALSE,nullptr)){}
  struct Read {
   uint8_t* base;
   bool operator()(uint32_t at,void* out,size_t n) const {if(!at||uint64_t(at)+n>0x100000000ull)return false;SIZE_T got{};return ReadProcessMemory(GetCurrentProcess(),base+at,out,n,&got)&&got==n;}
  };
  struct Staged {PresentationQueue queue;uint64_t epoch{};};
  static Staged& staged(){static thread_local Staged value;return value;}
  static void flush(void* context){
   auto& self=*static_cast<PresentationHooks*>(context);auto& local=staged();bool overflow=false;auto events=local.queue.take(overflow);
   if(!self.enabled_.load()||local.epoch!=self.epoch_.load())return;
   for(const auto& event:events)self.queue_.push(event);if(overflow)self.queue_.mark_overflow();if(overflow||!events.empty())SetEvent(self.event_);
  }
  template<class Read> static bool name(Read&& read,uint32_t globals,uint32_t index,std::array<char,64>& text){
   const auto names=ui_word(read,globals),count=ui_word(read,globals+4);if(!names||index>=count||count>250000)return false;
   const auto entry=ui_word(read,names+index*4);if(!entry||!read(entry+8,text.data(),text.size()))return false;
   return std::find(text.begin(),text.end(),'\0')!=text.end();
  }
  template<class Read> PresentationKind classify(Read&& read,uint32_t frame){
   const auto node=ui_word(read,frame+0x10);if(!node)return PresentationKind::ignore;
   const auto owner=ui_word(read,node+0x28),functionName=ui_word(read,node+0x2c);
   // Only script metadata is cached; content and UObject instance pointers are
   // copied for each call. Revalidate the identity when a metadata slot is reused.
   struct Entry {uint32_t node{},owner{},name{};PresentationKind kind=PresentationKind::ignore;};
   static thread_local std::array<Entry,128> cache;
   auto& cached=cache[(node>>4)%cache.size()];
   if(cached.node==node&&cached.owner==owner&&cached.name==functionName)return cached.kind;
   std::array<char,64> function{},cls{};
   if(!name(read,names_global_,functionName,function)||!name(read,names_global_,ui_word(read,owner+0x2c),cls))return PresentationKind::ignore;
   auto kind=presentation_route(cls.data(),function.data());cached={node,owner,functionName,kind};return kind;
  }
  template<unsigned I> static void observe(void* context,uint8_t* base){
   auto& self=instance();if(!self.enabled_.load(std::memory_order_relaxed)){self.original_[I](context,base);return;}
   auto epoch=self.epoch_.load();Read read{base};auto frame=ui_argument(context,4);
   PresentationEvent event;bool captured=capture_presentation(read,frame,self.classify(read,frame),event);event.tick=GetTickCount64();
   self.original_[I](context,base);
   static thread_local PresentationFilter filter;
   if(captured&&self.enabled_.load()&&epoch==self.epoch_.load()&&filter.changed(event,epoch)){
    event.order=++cue_order;if(event.kind>=PresentationKind::button_create&&event.kind<=PresentationKind::button_end)event.revision=++button_revision;
    ++self.captured_;
    if(cue_transaction.depth){auto& local=staged();if(local.epoch!=epoch){local.queue.clear();local.epoch=epoch;}local.queue.push(event);defer_cue_flush(flush,&self);}
    else{self.queue_.push(event);SetEvent(self.event_);}
   }
  }
  static void object_call(void* c,uint8_t* b){observe<0>(c,b);}
  static void movie_call(void* c,uint8_t* b){observe<1>(c,b);}
  NativeHooks hooks_;const BuildProfile* profile_{};uint32_t names_global_=disc_profile.layout.names_global;
  std::array<Function,2> original_{};HANDLE event_{};bool installed_{};std::atomic<bool> enabled_{};std::atomic<uint64_t> epoch_{},captured_{};PresentationQueue queue_;
};
}
