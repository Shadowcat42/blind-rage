#pragma once
#include "ui_changes.h"
#include <array>
namespace a11y {
// Coalesce writes to the same member at the movie-update boundary. This tracks
// only members passed through a mutation hook, never the whole object graph.
class AssignmentChanges {
 public:
  bool contains(uint32_t object,uint32_t key) const noexcept {
   if(!object||!key)return false;
   unsigned slot=((object>>4)^(key>>3))%entries_.size();
   for(unsigned attempts=0;attempts<entries_.size();attempts++,slot=(slot+1)%entries_.size()){
    const auto& entry=entries_[slot];if(entry.object==object&&entry.key==key)return true;if(!entry.object)return false;
   }
   return false;
  }
  void record(const UiAssignment& assignment) noexcept {
   if(!assignment.relevant||!assignment.object||!assignment.key||overflow_)return;
   unsigned slot=((assignment.object>>4)^(assignment.key>>3))%entries_.size();
   for(unsigned attempts=0;attempts<entries_.size();attempts++,slot=(slot+1)%entries_.size()){
    auto& entry=entries_[slot];
    if(entry.object==assignment.object&&entry.key==assignment.key)return;
    if(!entry.object){entry=assignment;used_[count_++]=slot;return;}
   }
   overflow_=true;
  }
  template<class Value> bool flush(Value&& value) noexcept {
   bool changed=overflow_;
   for(unsigned i=0;i<count_;i++){
    auto& entry=entries_[used_[i]];
    if(value(entry.object,entry.key)!=entry.before)changed=true;
    entry={};
   }
   count_=0;overflow_=false;return changed;
  }
  void clear() noexcept {flush([](uint32_t,uint32_t){return UiValue{};});}
 private:
  std::array<UiAssignment,2048> entries_{};
  std::array<unsigned,2048> used_{};
  unsigned count_{};bool overflow_{};
};
}
