#pragma once
#include "platform.h"
#include <prism.h>
#include <functional>
namespace a11y {
class Speech {
 public:
  using Log=std::function<void(const std::string&)>;
  explicit Speech(Log log):log_(std::move(log)){}
  bool init(const fs::path& dir){
   library_=LoadLibraryExW((dir/L"prism.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
   if(!library_){log_("Prism DLL failed to load; Windows error "+std::to_string(GetLastError()));return false;}
#define GET(name) name##_ = reinterpret_cast<decltype(&name)>(GetProcAddress(library_,#name)); if(!name##_){log_("Missing Prism export: " #name);return false;}
   GET(prism_init);GET(prism_shutdown);GET(prism_registry_create);GET(prism_registry_create_best);GET(prism_backend_free);GET(prism_backend_speak);GET(prism_backend_stop);GET(prism_backend_name);GET(prism_error_string);
#undef GET
   ctx_=prism_init_(nullptr);if(!ctx_){log_("Prism initialization failed");return false;}
   backend_=prism_registry_create_(ctx_,PRISM_BACKEND_NVDA);
   if(!backend_)backend_=prism_registry_create_best_(ctx_);
   if(!backend_){log_("No Prism speech backend available");return false;}
   log_(std::string("Speech backend: ")+prism_backend_name_(backend_));return true;
  }
  bool speak(const std::string& s){
   if(!backend_||s.empty())return false;
   auto err=prism_backend_speak_(backend_,s.c_str(),true);
   if(err==PRISM_ERROR_BACKEND_NOT_AVAILABLE){prism_backend_free_(backend_);backend_=prism_registry_create_best_(ctx_);if(backend_)err=prism_backend_speak_(backend_,s.c_str(),true);}
   if(err!=PRISM_OK)log_(std::string("Speech failed: ")+prism_error_string_(err)+"; text="+s);
   else log_(std::string("SPEAK [")+prism_backend_name_(backend_)+"] "+s);
   return err==PRISM_OK;
  }
  void stop(){if(backend_){auto result=prism_backend_stop_(backend_);log_(std::string("SPEECH_STOP [")+prism_backend_name_(backend_)+"] result="+std::to_string(result));}}
  void close(){if(backend_){prism_backend_free_(backend_);backend_=nullptr;}if(ctx_){prism_shutdown_(ctx_);ctx_=nullptr;}if(library_){FreeLibrary(library_);library_=nullptr;}}
  ~Speech(){close();}
 private:
#define FN(name) decltype(&name) name##_{};
  FN(prism_init);FN(prism_shutdown);FN(prism_registry_create);FN(prism_registry_create_best);FN(prism_backend_free);FN(prism_backend_speak);FN(prism_backend_stop);FN(prism_backend_name);FN(prism_error_string);
#undef FN
  Log log_;HMODULE library_{};PrismContext* ctx_{};PrismBackend* backend_{};
};
}
