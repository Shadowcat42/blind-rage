#pragma once
#include "qte_events.h"
#include <mutex>

namespace a11y {
struct TimingState {
 uint32_t source{},index{},track{};CueName name;uint64_t generation{};bool active{},window{},mash{};
 bool operator==(const TimingState&) const=default;
};
inline bool capture_timing(const Memory& m,uint32_t source,int32_t track,bool window,TimingState& state){
 state={};state.source=source;state.index=m.u32(source+0x20);state.track=uint32_t(track);state.name={m.u32(source+0x2c),m.u32(source+0x30)};
 std::array<uint8_t,12> array{};
 if(!source||!state.index||track<0||!m.copy(source+0x8c,array.data(),array.size()))return false;
 auto rows=cue_word(array.data()),count=cue_word(array.data()+4),capacity=cue_word(array.data()+8);
 if(!rows||count>256||uint32_t(track)>=count||count>capacity||capacity>4096)return false;
 std::array<uint8_t,24> row{};if(!m.copy(rows+uint32_t(track)*24,row.data(),row.size())||row[0]>1)return false;
 auto flags=cue_word(row.data()+20);state.active=(flags&0xc0000000)==0x40000000;
 if(track>0)state.active=state.active&&(m.u32(rows+uint32_t(track-1)*24+20)&0x80000000);
 state.mash=row[0]==1;state.window=state.active&&window;return true;
}
struct GameplayEvent {bool timing{};QteGroup group;TimingState sync;uint64_t tick{},order{};};
class GameplayQueue {
 public:
  void push(const GameplayEvent& event){std::lock_guard lock(mutex_);if(count_==events_.size()){overflow_=true;return;}events_[count_++]=event;}
  std::vector<GameplayEvent> take(bool& overflow){std::lock_guard lock(mutex_);std::vector<GameplayEvent> out(events_.begin(),events_.begin()+count_);count_=0;overflow=overflow_;overflow_=false;return out;}
  void clear(){std::lock_guard lock(mutex_);count_=0;overflow_=false;}
  void mark_overflow(){std::lock_guard lock(mutex_);overflow_=true;}
 private:
  std::mutex mutex_;std::array<GameplayEvent,64> events_{};size_t count_{};bool overflow_{};
};
}
