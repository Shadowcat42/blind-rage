#pragma once
#include <MinHook.h>
#include <vector>

namespace a11y {
// Own only our targets. Never disable MH_ALL_HOOKS: another component may use
// the same MinHook instance. Callers must keep callback state alive until all
// in-flight callbacks have returned, even after entry points are restored.
class NativeHooks {
 public:
  NativeHooks() = default;
  NativeHooks(const NativeHooks&) = delete;
  NativeHooks& operator=(const NativeHooks&) = delete;
  bool initialize() {
    if(initialized_)return true;
    const auto status=MH_Initialize();
    owns_library_=status==MH_OK;
    initialized_=owns_library_||status==MH_ERROR_ALREADY_INITIALIZED;
    return initialized_;
  }
  bool add(void* target,void* detour,void** original) {
    if(!initialized_||enabled_||!target||!detour||!original)return false;
    if(MH_CreateHook(target,detour,original)!=MH_OK)return false;
    targets_.push_back(target);return true;
  }
  bool enable() {
    if(!initialized_)return false;
    if(enabled_)return true;
    // Enable separately: rollback only our targets on any failure.
    for(auto target:targets_)if(MH_EnableHook(target)!=MH_OK){disable();return false;}
    enabled_=true;return true;
  }
  bool disable() {
    bool ok=true;
    for(auto target:targets_){auto s=MH_DisableHook(target);if(s!=MH_OK&&s!=MH_ERROR_DISABLED)ok=false;}
    enabled_=false;return ok;
  }
  bool clear() {
    if(!disable())return false;
    bool ok=true;
    for(auto target:targets_)if(MH_RemoveHook(target)!=MH_OK)ok=false;
    if(ok)targets_.clear();
    return ok;
  }
  ~NativeHooks() {
    if(clear()&&owns_library_)MH_Uninitialize();
  }
 private:
  bool initialized_{},owns_library_{},enabled_{};
  std::vector<void*> targets_;
};
}
