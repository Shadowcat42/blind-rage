#pragma once
#include "qte_events.h"

namespace a11y {
// Scope only the native game's angular test. Its range, candidate eligibility,
// occlusion trace, acquisition time, lock cap and projectile logic still run.
class AcquisitionScope {
 public:
  using Write=std::function<bool(uint32_t,const void*,size_t)>;
  AcquisitionScope(const Memory& memory,Write write,uint32_t controller,float degrees,std::optional<float> safeRadius={}):memory_(memory),write_(std::move(write)),at_(controller+0x7d4){
   if(!controller||controller>UINT32_MAX-0x7dc||!std::isfinite(degrees)||degrees<1||degrees>89||!memory_.copy(at_,original_.data(),original_.size()))return;
   auto minimum=std::bit_cast<float>(cue_word(original_.data())),maximum=std::bit_cast<float>(cue_word(original_.data()+4));
   if(!std::isfinite(minimum)||!std::isfinite(maximum)||minimum<0||maximum<minimum||maximum>180)return;
   auto put=[&](size_t offset,float value){auto bits=std::bit_cast<uint32_t>(value);for(unsigned i=0;i<4;++i)widened_[offset+i]=uint8_t(bits>>(24-i*8));};
   if(safeRadius&&(!std::isfinite(*safeRadius)||*safeRadius<0||*safeRadius>180))return;
   put(0,safeRadius?std::min(std::max(minimum,degrees),*safeRadius):std::max(minimum,degrees));put(4,safeRadius?std::min(std::max(maximum,degrees),*safeRadius):std::max(maximum,degrees));
   if(widened_!=original_){changed_=write_(at_,widened_.data(),widened_.size());if(!changed_)write_(at_,original_.data(),original_.size());}
  }
  ~AcquisitionScope(){
   if(!changed_)return;std::array<uint8_t,8> current{};if(!memory_.copy(at_,current.data(),current.size()))return;
   // Preserve a legitimate game-side change made during the call instead of
   // overwriting it with the saved value.
   for(size_t offset:{size_t(0),size_t(4)})if(std::equal(current.begin()+offset,current.begin()+offset+4,widened_.begin()+offset))write_(at_+uint32_t(offset),original_.data()+offset,4);
  }
  bool changed() const{return changed_;}
  AcquisitionScope(const AcquisitionScope&)=delete;
  AcquisitionScope& operator=(const AcquisitionScope&)=delete;
 private:
  const Memory& memory_;Write write_;uint32_t at_;bool changed_{};std::array<uint8_t,8> original_{},widened_{};
};
}
