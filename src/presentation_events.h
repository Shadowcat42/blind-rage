#pragma once
#include "ui_changes.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

namespace a11y {
inline std::atomic<uint64_t> cue_order{},button_revision{};
// Selected game actions can create and finish prompts through nested calls.
// Publish their copied events together when the enclosing action returns.
struct CueTransactionState {unsigned depth{},count{};struct Flush {void(*function)(void*);void* context;};std::array<Flush,4> flushes{};};
inline thread_local CueTransactionState cue_transaction;
inline bool defer_cue_flush(void(*function)(void*),void* context){
 if(!cue_transaction.depth)return false;
 for(unsigned i=0;i<cue_transaction.count;++i)if(cue_transaction.flushes[i].function==function&&cue_transaction.flushes[i].context==context)return true;
 if(cue_transaction.count<cue_transaction.flushes.size())cue_transaction.flushes[cue_transaction.count++]={function,context};
 return true;
}
struct CueTransaction {
 CueTransaction(){++cue_transaction.depth;}
 ~CueTransaction(){if(--cue_transaction.depth)return;auto callbacks=cue_transaction.flushes;auto count=cue_transaction.count;cue_transaction.count=0;for(unsigned i=0;i<count;++i)callbacks[i].function(callbacks[i].context);}
 CueTransaction(const CueTransaction&)=delete;CueTransaction& operator=(const CueTransaction&)=delete;
};
enum class PresentationKind {ignore,text_start,text_end,text_end_type,text_clear,tutorial_create,tutorial_start,tutorial_end,tutorial_clear,tutorial_box,
 health,burst,gauge_visible,gauge_end,button_create,button_visible,button_active,button_success,button_failure,button_finish,button_end,sync_start,sync_visible,sync_end,sync_clear};
inline PresentationKind presentation_route(std::string_view owner,std::string_view function){
 if(owner=="BCGFxMovieText"){
  if(function=="CallStartText")return PresentationKind::text_start;
  if(function=="CallEnd")return PresentationKind::text_end;
  if(function=="CallEndType")return PresentationKind::text_end_type;
  if(function=="CallClear")return PresentationKind::text_clear;
 }
 if(owner=="BCGFxUI_Advertise"){
  if(function=="CallCreate")return PresentationKind::tutorial_create;
  if(function=="CallStart")return PresentationKind::tutorial_start;
  if(function=="CallEnd")return PresentationKind::tutorial_end;
  if(function=="CallClear")return PresentationKind::tutorial_clear;
  if(function=="CallSetBox")return PresentationKind::tutorial_box;
 }
 if(owner=="BCGFxUI_Gauge"){
  if(function=="CallSetHealth"||function=="CallSetNextHealth")return PresentationKind::health;
  if(function=="CallSetBurst"||function=="CallSetNextBurst")return PresentationKind::burst;
  if(function=="CallShowGauge")return PresentationKind::gauge_visible;
  if(function=="CallDest")return PresentationKind::gauge_end;
 }
 if(owner=="BCGFxMovieButton"){
  if(function=="CallCreate")return PresentationKind::button_create;
  if(function=="CallSetVisible")return PresentationKind::button_visible;
  if(function=="CallSetActive")return PresentationKind::button_active;
  if(function=="CallSuccess")return PresentationKind::button_success;
  if(function=="CallFailure")return PresentationKind::button_failure;
  if(function=="CallFinish")return PresentationKind::button_finish;
  if(function=="CallDest")return PresentationKind::button_end;
 }
 if(owner=="BCGFxUI_Synchronic"){
  if(function=="CallStart")return PresentationKind::sync_start;
  if(function=="CallSetVisible")return PresentationKind::sync_visible;
  if(function=="CallEnd")return PresentationKind::sync_end;
  if(function=="CallDest")return PresentationKind::sync_clear;
 }
 return PresentationKind::ignore;
}
template<size_t Capacity> struct CapturedString {
 std::array<uint8_t,Capacity*2> bytes{};uint16_t length{};
 std::string utf8() const {
  std::string out;
  for(unsigned i=0;i<length;i++){
   uint32_t u=uint32_t(bytes[i*2])<<8|bytes[i*2+1];
   if(u>=0xd800&&u<=0xdbff){if(i+1>=length)return {};auto v=uint32_t(bytes[(i+1)*2])<<8|bytes[(i+1)*2+1];if(v<0xdc00||v>0xdfff)return {};u=0x10000+((u-0xd800)<<10)+(v-0xdc00);++i;}
   else if(u>=0xdc00&&u<=0xdfff)return {};
   if(u<0x80)out+=char(u);else if(u<0x800){out+=char(0xc0|(u>>6));out+=char(0x80|(u&63));}else if(u<0x10000){out+=char(0xe0|(u>>12));out+=char(0x80|((u>>6)&63));out+=char(0x80|(u&63));}else{out+=char(0xf0|(u>>18));out+=char(0x80|((u>>12)&63));out+=char(0x80|((u>>6)&63));out+=char(0x80|(u&63));}
  }return out;
 }
};
template<class Read,size_t N> bool capture_fstring(Read&& read,uint32_t at,CapturedString<N>& out){
 auto data=ui_word(read,at),count=ui_word(read,at+4),capacity=ui_word(read,at+8);
 if(count>N||count>capacity||capacity>1024*1024||(!data&&count))return false;
 out.length=0;if(!count)return true;
 if(uint64_t(data)+count*2>0x100000000ull||!read(data,out.bytes.data(),count*2))return false;
 if(out.bytes[(count-1)*2]||out.bytes[(count-1)*2+1])return false;
 // Embedded nulls and invalid UTF-16 must not turn a malformed string into
 // plausible, truncated speech. Unicode validation completes on the worker.
 for(unsigned i=0;i+1<count;i++)if(!out.bytes[i*2]&&!out.bytes[i*2+1])return false;
 out.length=uint16_t(count-1);return true;
}
struct PresentationEvent {
 PresentationKind kind=PresentationKind::ignore;uint32_t movie{},index{};uint64_t tick{},order{},revision{};
 float value{},extra{};bool flag{};
 CapturedString<129> tag;CapturedString<65> type;CapturedString<2049> text;
};
// Copy bounded payloads while their script locals exist. No game pointers to
// text survive the callback; localization and speech happen on the worker.
template<class Read> bool capture_presentation(Read&& read,uint32_t frame,PresentationKind kind,PresentationEvent& event){
 if(kind==PresentationKind::ignore)return false;
 const auto movie=ui_word(read,frame+0x14),locals=ui_word(read,frame+0x1c);
 if(!movie||!(ui_word(read,movie+0xc8)&0x80000000))return false;
 event.kind=kind;event.movie=movie;event.index=ui_word(read,movie+0x20);
 auto str=[&](uint32_t offset,auto& destination){return locals&&uint64_t(locals)+offset+12<=0x100000000ull&&capture_fstring(read,locals+offset,destination);};
 auto word=[&](uint32_t at,uint32_t& value){uint8_t bytes[4];if(!at||uint64_t(at)+4>0x100000000ull||!read(at,bytes,4))return false;value=uint32_t(bytes[0])<<24|uint32_t(bytes[1])<<16|uint32_t(bytes[2])<<8|bytes[3];return true;};
 switch(kind){
  case PresentationKind::health:case PresentationKind::burst:{
   uint32_t value{},visible{};if(!word(locals,value)||!word(movie+0x1e0,visible))return false;event.value=std::bit_cast<float>(value);
   event.flag=(visible&0x80000000)!=0; // BCGaugeFlagData.bNow, reflected mask.
   return std::isfinite(event.value)&&event.value>=0&&event.value<=1;
  }
  case PresentationKind::gauge_visible:{uint32_t value{};if(!word(locals,value))return false;event.flag=value!=0;return true;}
  case PresentationKind::button_create:{
   uint32_t value{},extra{};if(!locals||!word(locals+0x18,value)||!word(locals+0x1c,extra))return false;
   event.value=std::bit_cast<float>(value);event.extra=std::bit_cast<float>(extra);
   return str(0,event.type)&&str(0xc,event.tag)&&std::isfinite(event.value)&&std::isfinite(event.extra);
  }
  case PresentationKind::button_visible:case PresentationKind::button_active:case PresentationKind::sync_start:case PresentationKind::sync_visible:{
   uint32_t value{};if(!locals||!word(locals+0xc,value))return false;event.flag=value!=0;return str(0,event.tag);
  }
  case PresentationKind::button_success:case PresentationKind::button_failure:case PresentationKind::button_finish:case PresentationKind::sync_end:return str(0,event.tag);
  case PresentationKind::text_start:return str(0,event.tag)&&str(0xc,event.text)&&str(0x18,event.type);
  case PresentationKind::tutorial_create:return str(0,event.tag)&&str(0xc,event.text);
  case PresentationKind::text_end:return str(0,event.tag);
  case PresentationKind::text_end_type:case PresentationKind::tutorial_start:case PresentationKind::tutorial_end:case PresentationKind::tutorial_box:return str(0,event.type);
  default:return true;
 }
}
// Some setters are called every frame even when nothing changes. Suppress
// unchanged percentages/flags on the producing thread before waking the worker.
// No timer, object scan or speech is involved in this filter.
class PresentationFilter {
 public:
  bool changed(const PresentationEvent& event,uint64_t epoch){
   if(epoch!=epoch_){entries_={};epoch_=epoch;}
   uint64_t tag=14695981039346656037ull;for(size_t i=0;i<event.tag.length*2u;++i){tag^=event.tag.bytes[i];tag*=1099511628211ull;}
   bool end=event.kind==PresentationKind::gauge_end||event.kind==PresentationKind::button_end;
   bool reset=event.kind==PresentationKind::button_create||event.kind==PresentationKind::button_success||event.kind==PresentationKind::button_failure||event.kind==PresentationKind::button_finish;
   if(end||reset)for(auto& entry:entries_)if(entry.movie==event.movie&&(end||entry.tag==tag))entry={};
   bool flag=event.kind==PresentationKind::gauge_visible||event.kind==PresentationKind::button_visible||event.kind==PresentationKind::button_active;
   if(event.kind!=PresentationKind::health&&event.kind!=PresentationKind::burst&&!flag)return true;
   auto value=flag?int(event.flag):int(std::lround(event.value*100))*2+int(event.flag);
   auto& cached=entries_[((event.movie>>4)^uint32_t(event.kind)^tag)%entries_.size()];
   if(cached.movie==event.movie&&cached.index==event.index&&cached.kind==event.kind&&cached.tag==tag&&cached.value==value)return false;
   cached={event.movie,event.index,event.kind,tag,value};return true;
  }
 private:
  struct Entry {uint32_t movie{},index{};PresentationKind kind=PresentationKind::ignore;uint64_t tag{};int value{};};
  std::array<Entry,32> entries_{};uint64_t epoch_{};
};
class PresentationQueue {
 public:
  void push(const PresentationEvent& event){std::lock_guard lock(mutex_);if(count_==events_.size()){overflow_=true;return;}events_[(start_+count_)%events_.size()]=event;++count_;}
  std::vector<PresentationEvent> take(bool& overflow){std::lock_guard lock(mutex_);std::vector<PresentationEvent> out;out.reserve(count_);for(size_t i=0;i<count_;i++)out.push_back(events_[(start_+i)%events_.size()]);count_=start_=0;overflow=overflow_;overflow_=false;return out;}
  void clear(){std::lock_guard lock(mutex_);start_=count_=0;overflow_=false;}
  void mark_overflow(){std::lock_guard lock(mutex_);overflow_=true;}
 private:
  std::mutex mutex_;std::array<PresentationEvent,64> events_{};size_t start_{},count_{};bool overflow_{};
};
}
