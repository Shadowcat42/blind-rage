#pragma once
#include <windows.h>
#include <array>
#include <atomic>
namespace a11y {
class WindowNotifications {
 public:
  static constexpr UINT focus_message=WM_APP+17;
  bool initialize(const int* keys){
   MSG message;PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);
   thread_=GetCurrentThreadId();
   for(unsigned i=0;i<keys_.size();i++)keys_[i]=keys[i];
   hook_=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,focus,0,0,WINEVENT_OUTOFCONTEXT);
   update();return hook_!=nullptr;
  }
  bool update(){
   DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);
   bool focused=pid==GetCurrentProcessId();
   if(focused!=focused_)for(unsigned i=0;i<keys_.size();i++){
    if(registered_[i]){UnregisterHotKey(nullptr,100+int(i));registered_[i]=false;}
    if(focused&&keys_[i])registered_[i]=RegisterHotKey(nullptr,100+int(i),MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,keys_[i]);
   }
   focused_=focused;return focused_;
  }
  bool focused() const{return focused_;}
  bool hotkey_registered(unsigned i) const{return i<registered_.size()&&registered_[i];}
  ~WindowNotifications(){if(hook_)UnhookWinEvent(hook_);for(unsigned i=0;i<keys_.size();i++)if(registered_[i])UnregisterHotKey(nullptr,100+int(i));thread_=0;}
 private:
  static void CALLBACK focus(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD){auto thread=thread_.load();if(thread)PostThreadMessageW(thread,focus_message,0,0);}
  inline static std::atomic<DWORD> thread_{};
  HWINEVENTHOOK hook_{};
  std::array<int,6> keys_{};
  std::array<bool,6> registered_{};
  bool focused_{};
};
}
