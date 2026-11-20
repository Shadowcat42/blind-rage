#include "../src/presentation_hooks.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
extern "C" __declspec(noinline) void object_target(void* c,uint8_t* b){static_cast<uint64_t*>(c)[0]=0x777;b[0x7101]='Z';}
extern "C" __declspec(noinline) void movie_target(void* c,uint8_t*){static_cast<uint64_t*>(c)[0]=0x888;}
int main(){try{
 // Reserve guest address space, committing only fixture pages. No game files,
 // game execution, external process access, or real speech are involved.
 auto* base=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x100000000ull,MEM_RESERVE,PAGE_NOACCESS));check(base,"reserve guest fixture address space");
 check(VirtualAlloc(base+0x1000,0x10000,MEM_COMMIT,PAGE_READWRITE),"commit fixture low pages");
 check(VirtualAlloc(base+0x832a9000,0x2000,MEM_COMMIT,PAGE_READWRITE),"commit fixture globals");
 auto word=[&](uint32_t at,uint32_t value){for(unsigned i=0;i<4;i++)base[at+i]=uint8_t(value>>(24-8*i));};
 word(disc_profile.layout.names_global,0x4000);word(disc_profile.layout.names_global+4,4);
 const char* names[]={"None","Function","CallStartText","BCGFxMovieText"};
 for(unsigned i=0;i<4;i++){word(0x4000+i*4,0x5000+i*0x100);std::memcpy(base+0x5008+i*0x100,names[i],std::strlen(names[i])+1);}
 word(0x1028,0x1100);word(0x102c,2);word(0x112c,3);
 word(0x2010,0x1000);word(0x2014,0x3000);word(0x201c,0x6000);word(0x30c8,0x80000000);word(0x3020,5);
 auto string=[&](uint32_t header,uint32_t at,const char* text){auto n=uint32_t(std::strlen(text));word(header,at);word(header+4,n+1);word(header+8,n+1);for(unsigned i=0;i<n;i++)base[at+i*2+1]=uint8_t(text[i]);};
 string(0x6000,0x7000,"test");string(0x600c,0x7100,"Hello");string(0x6018,0x7200,"subtitle");
 auto& source=PresentationHooks::instance();check(source.install_targets({(void*)object_target,(void*)movie_target}),"install presentation detours");source.capture(true);
 // Opaque calls prevent the optimizer from assuming a patched function still
 // has its original write-only signature and omitting caller state it reads.
 PresentationHooks::Function volatile call_object=object_target,call_movie=movie_target;
 std::array<uint64_t,40> context{};context[4]=0x2000;call_object(context.data(),base);
 check(context[0]==0x777&&base[0x7101]=='Z',"original executes and retains its return-register behavior");
 check(WaitForSingleObject(source.event(),0)==WAIT_OBJECT_0,"presentation event wakes worker");bool overflow=false;auto batch=source.take(overflow);
 check(!overflow&&batch.size()==1&&batch[0].text.utf8()=="Hello"&&batch[0].kind==PresentationKind::text_start,"copy caller locals before original mutates them");
 call_movie(context.data(),base);check(context[0]==0x888&&source.take(overflow).size()==1,"movie-player boundary shares capture behavior");
 word(0x102c,1);call_object(context.data(),base);check(source.take(overflow).empty(),"metadata identity changes invalidate cached classification");
 word(0x102c,2);source.capture(false);call_object(context.data(),base);check(source.take(overflow).empty()&&context[0]==0x777,"disabled capture preserves original behavior");
 source.capture(true);check(source.take(overflow).empty(),"reinitialization starts with an empty queue");source.capture(false);VirtualFree(base,0,MEM_RELEASE);
 std::cout<<"Presentation hook integration tests passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
