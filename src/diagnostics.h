#pragma once
#include <filesystem>
#include <string_view>
namespace a11y {
inline bool diagnostic_filename(std::wstring_view name){
 if(name==L"accessibility.log"||name==L"lock-trace.jsonl")return true;
 if(!name.starts_with(L"dump-"))return false;
 auto end=name.ends_with(L".pages")?name.size()-6:name.ends_with(L".txt")?name.size()-4:0;
 if(end<=5)return false;
 for(size_t i=5;i<end;i++)if(name[i]<L'0'||name[i]>L'9')return false;
 return true;
}
struct DiagnosticCleanup {unsigned removed{},failed{};};
inline DiagnosticCleanup clear_previous_diagnostics(const std::filesystem::path& dir){
 namespace fs=std::filesystem;DiagnosticCleanup result;std::error_code error;
 auto status=fs::symlink_status(dir,error);
 if(error||!fs::is_directory(status)||fs::is_symlink(status)){result.failed=1;return result;}
 fs::directory_iterator files(dir,error),end;if(error){result.failed=1;return result;}
 while(files!=end){
  const auto path=files->path();auto type=files->symlink_status(error);
  if(error){result.failed++;error.clear();}
  else if(fs::is_regular_file(type)&&diagnostic_filename(path.filename().wstring())){
   if(fs::remove(path,error))result.removed++;else result.failed++;
   error.clear();
  }
  files.increment(error);if(error){result.failed++;break;}
 }
 return result;
}
}
