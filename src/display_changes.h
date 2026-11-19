#pragma once
#include <array>
#include <cstdint>
namespace a11y {
// Retain the first pre-mutation value until a stable movie-update boundary.
// A flag toggled and restored during animation produces no notification.
class DisplayChanges {
 public:
  void record(uint32_t object,uint32_t before) noexcept {
   unsigned slot=(object>>4)%entries_.size();
   for(unsigned attempts=0;attempts<entries_.size();attempts++,slot=(slot+1)%entries_.size()){
    auto& entry=entries_[slot];if(entry.object==object)return;
    if(!entry.object){entry={object,before};used_[count_++]=slot;return;}
   }
   overflow_=true;
  }
  template<class Value> bool flush(Value&& value) noexcept {
   bool changed=overflow_;
   for(unsigned i=0;i<count_;i++){auto& entry=entries_[used_[i]];if(value(entry.object)!=entry.before)changed=true;entry={};}
   count_=0;overflow_=false;return changed;
  }
  void clear() noexcept {flush([](uint32_t){return 0u;});}
 private:
  struct Entry {uint32_t object{},before{};};
  std::array<Entry,2048> entries_{};
  std::array<unsigned,2048> used_{};
  unsigned count_{};bool overflow_{};
};
}
