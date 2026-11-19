#include "../src/ui_changes.h"
#include <cstdio>
#include <cstdlib>
static void require(bool pass,const char* message){if(!pass){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
 std::array<uint8_t,8192> memory{};
 auto word=[&](uint32_t at,uint32_t value){for(int i=3;i>=0;i--){memory[at+i]=uint8_t(value);value>>=8;}};
 auto read=[&](uint32_t at,void* out,size_t n){if(at>memory.size()||n>memory.size()-at)return false;std::memcpy(out,memory.data()+at,n);return true;};
 std::array<uint64_t,40> context{};context[0]=0x100;context[5]=0x400;context[6]=0x500;
 word(0x10c,0x1000);word(0x1000,1);word(0x1004,0);word(0x1008,0xffffffff);word(0x1010,0x200);
 word(0x400,0x200);word(0x200,0x300);std::memcpy(memory.data()+0x300,"m_cursor",9);
 memory[0x1018]=4;word(0x1020,0);
 auto assignment=a11y::ui_before_assignment(read,context.data());
 require(assignment.relevant&&assignment.before.valid,"decode relevant member");
 require(!a11y::ui_after_assignment(read,assignment),"no notification for unchanged value");
 memory[0x1019]=0x80;word(0x101c,0xdeadbeef);
 require(!a11y::ui_after_assignment(read,assignment),"ignore padding and inactive union bytes");
 word(0x1020,1);
 require(a11y::ui_after_assignment(read,assignment),"notify changed integer cursor");
 std::memcpy(memory.data()+0x300,"m_repeat",9);
 assignment=a11y::ui_before_assignment(read,context.data());
 require(!assignment.relevant,"ignore animation repeat counter");
 require(!a11y::ui_after_assignment(read,assignment),"irrelevant mutation stays quiet");
 require(a11y::narrated_member("tl_opend")&&a11y::narrated_member("m_param"),"dialog and collection fields");
 memory[0x1018]=3;word(0x1020,0x3ff00000);word(0x1024,0);
 auto value=a11y::ui_value(read,0x1018);
 require(value.type==3&&value.payload==0x3ff0000000000000ull,"double payload");
 word(0x1008,0);word(0x1010,0x600);
 require(!a11y::ui_member_value(read,0x100,0x200).valid,"bound cyclic hash chain");
 std::puts("UI change decoding tests passed.");
}
