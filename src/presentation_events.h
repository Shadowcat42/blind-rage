#pragma once
#include "ui_changes.h"
#include <algorithm>
#include <array>
#include <mutex>
#include <string>
#include <vector>

namespace a11y {
enum class PresentationKind {ignore,text_start,text_end,text_end_type,text_clear,tutorial_create,tutorial_start,tutorial_end,tutorial_clear,tutorial_box};
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
 PresentationKind kind=PresentationKind::ignore;uint32_t movie{},index{};uint64_t tick{};
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
 switch(kind){
  case PresentationKind::text_start:return str(0,event.tag)&&str(0xc,event.text)&&str(0x18,event.type);
  case PresentationKind::tutorial_create:return str(0,event.tag)&&str(0xc,event.text);
  case PresentationKind::text_end:return str(0,event.tag);
  case PresentationKind::text_end_type:case PresentationKind::tutorial_start:case PresentationKind::tutorial_end:case PresentationKind::tutorial_box:return str(0,event.type);
  default:return true;
 }
}
class PresentationQueue {
 public:
  void push(const PresentationEvent& event){std::lock_guard lock(mutex_);if(count_==events_.size()){overflow_=true;return;}events_[(start_+count_)%events_.size()]=event;++count_;}
  std::vector<PresentationEvent> take(bool& overflow){std::lock_guard lock(mutex_);std::vector<PresentationEvent> out;out.reserve(count_);for(size_t i=0;i<count_;i++)out.push_back(events_[(start_+i)%events_.size()]);count_=start_=0;overflow=overflow_;overflow_=false;return out;}
  void clear(){std::lock_guard lock(mutex_);start_=count_=0;overflow_=false;}
 private:
  std::mutex mutex_;std::array<PresentationEvent,64> events_{};size_t start_{},count_{};bool overflow_{};
};
}
