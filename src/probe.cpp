#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

// Development-only, read-only inspection of the recompilation's guest memory.
static HANDLE process;
static uintptr_t guest;
template<class T> T read(uintptr_t at) { T value{}; SIZE_T n{}; ReadProcessMemory(process,(void*)at,&value,sizeof(value),&n); return value; }
static bool bytes(uintptr_t at,void* out,size_t n) { SIZE_T got{}; return ReadProcessMemory(process,(void*)at,out,n,&got)&&got==n; }
static uint32_t be32(const void* p) { const auto* b=(const unsigned char*)p; return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3]; }
static uintptr_t find_module(DWORD pid,const wchar_t* name) {
 HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid); MODULEENTRY32W m{sizeof(m)}; uintptr_t result{};
 if(Module32FirstW(s,&m)) do { if(!_wcsicmp(m.szModule,name)){ result=(uintptr_t)m.modBaseAddr; break; } }while(Module32NextW(s,&m)); CloseHandle(s); return result;
}
static uintptr_t exported(uintptr_t base,const char* name) {
 auto pe=read<uint32_t>(base+60); auto d=read<IMAGE_EXPORT_DIRECTORY>(base+read<uint32_t>(base+pe+24+112));
 for(uint32_t i=0;i<d.NumberOfNames;i++) { char n[256]{}; bytes(base+read<uint32_t>(base+d.AddressOfNames+4*i),n,sizeof(n)-1); if(!strcmp(n,name))return base+read<uint32_t>(base+d.AddressOfFunctions+4*read<uint16_t>(base+d.AddressOfNameOrdinals+2*i)); } return 0;
}
int main(int argc,char** argv) {
 if(argc<3){ puts("probe PID info | scan [text] | dump GUEST_HEX SIZE_HEX FILE | words GUEST_HEX COUNT | floats GUEST_HEX COUNT"); return 1; }
 DWORD pid=strtoul(argv[1],nullptr,10); process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid); if(!process){printf("OpenProcess failed: %lu\n",GetLastError());return 2;}
 auto base=find_module(pid,L"rexruntime.dll"); auto slot=exported(base,"?instance_@Runtime@rex@@0PEAV12@EA");
 // The fingerprinted runtime's virtual_membase accessor is Runtime+0xc8 -> Memory+0x28.
 guest=read<uintptr_t>(read<uintptr_t>(read<uintptr_t>(slot)+0xc8)+0x28);
 printf("runtime=%llx instance-slot=%llx guest=%llx\n",(unsigned long long)base,(unsigned long long)slot,(unsigned long long)guest);
 if(!guest)return 3;
 std::string command=argv[2];
 if(command=="info")return 0;
 if((command=="words"||command=="floats")&&argc>=5) {
  uint32_t at=strtoul(argv[3],nullptr,16); size_t count=strtoul(argv[4],nullptr,0); for(size_t i=0;i<count;i++) { auto v=read<uint32_t>(guest+at+i*4); v=be32(&v); if(command=="floats"){float f;memcpy(&f,&v,4);printf("%08x %08x %g\n",at+uint32_t(i*4),v,f);}else printf("%08x %08x\n",at+uint32_t(i*4),v); } return 0;
 }
 if(command=="dump"&&argc>=6) {
  uint32_t at=strtoul(argv[3],nullptr,16); size_t size=strtoull(argv[4],nullptr,16); if(size>512*1024*1024)return 4; std::vector<char> data(size); size_t readable=0;
  for(size_t i=0;i<size;i+=4096){size_t n=std::min<size_t>(4096,size-i);if(bytes(guest+at+i,data.data()+i,n))readable+=n;}
  std::ofstream(argv[5],std::ios::binary).write(data.data(),data.size()); printf("saved %zu bytes (%zu readable)\n",size,readable); return 0;
 }
 if(command=="scan") {
  std::string needle=argc>=4?argv[3]:"GFx"; uintptr_t pos=guest,limit=guest+0x100000000ull; std::vector<char> data; size_t matches=0;
  while(pos<limit){ MEMORY_BASIC_INFORMATION m{}; if(!VirtualQueryEx(process,(void*)pos,&m,sizeof(m)))break; auto begin=std::max(pos,(uintptr_t)m.BaseAddress),end=std::min(limit,(uintptr_t)m.BaseAddress+m.RegionSize); pos=end;
   if(m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))continue;
   for(uintptr_t chunk=begin;chunk<end;chunk+=1024*1024) {size_t n=std::min<uintptr_t>(1024*1024,end-chunk); data.resize(n);if(!bytes(chunk,data.data(),n))continue;
    for(size_t i=0;i+needle.size()<=n;i++) if(!memcmp(data.data()+i,needle.data(),needle.size())){ size_t a=i,z=i;while(a&&i-a<32&&data[a-1]>=32&&data[a-1]<127)a--;while(z<n&&z-i<160&&data[z]>=32&&data[z]<127)z++; printf("%08llx %.*s\n",(unsigned long long)(chunk+i-guest),int(z-a),data.data()+a); if(++matches>=3000)return 0; }
   }
  }
 }
 CloseHandle(process); return 0;
}
