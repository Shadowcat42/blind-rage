#include "../src/synchronic_reader.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
static PresentationEvent event(PresentationKind kind,uint64_t order,bool flag=true){PresentationEvent e;e.kind=kind;e.order=order;e.movie=100;e.index=10;e.flag=flag;e.tag.length=6;const char* tag="Track0";for(unsigned i=0;i<6;++i)e.tag.bytes[i*2+1]=uint8_t(tag[i]);return e;}
int main(){try{
 SynchronicReader reader([](std::string_view button){return controller_button(button,PromptDevice::playstation);});TimingState state;state.source=1;state.index=2;state.generation=3;state.active=true;state.name={4,0};auto name=[](CueName){return std::string("Track");};
 reader.apply(state,1,name);check(reader.take().empty(),"a track without a displayed prompt is quiet");reader.apply(event(PresentationKind::sync_start,2));check(reader.take().empty(),"retained track data cannot authorize a new prompt lifecycle");reader.apply(state,3,name);auto lines=reader.take();check(lines.size()==1&&lines[0]=="Timing: triangle.","prepare before the accepted window");
 state.window=true;reader.apply(state,4,name);lines=reader.take();check(lines.size()==1&&lines[0]=="Triangle now.","announce the physical timing button at the actual window");check(reader.take().empty(),"unchanged timing state is quiet");
 state.active=false;state.window=false;reader.apply(state,5,name);check(reader.take().empty()&&reader.repeat().empty(),"judge completion invalidates an instruction without speech stop");
 state.active=state.window=true;state.mash=true;state.generation++;reader.apply(state,6,name);lines=reader.take();check(lines.size()==1&&lines[0]=="Mash triangle now.","barrage and normal timing have distinct instructions");reader.apply(event(PresentationKind::sync_visible,7,false));check(reader.repeat().empty(),"hidden prompt does not speak");reader.apply(event(PresentationKind::sync_end,8));check(reader.movies().empty(),"end removes its exact prompt lifecycle");
 reader.apply(event(PresentationKind::sync_start,9));reader.apply(state,10,name);reader.apply(event(PresentationKind::sync_end,11));check(reader.take().empty(),"dismissal in a queued batch suppresses an unsent cue");
 std::vector<uint8_t> data(0x8000);auto word=[&](uint32_t at,uint32_t value){for(unsigned i=0;i<4;++i)data[at+i]=uint8_t(value>>(24-i*8));};Memory memory([&](uint32_t at,void* out,size_t n){if(uint64_t(at)+n>data.size())return false;std::memcpy(out,data.data()+at,n);return true;});
 word(0x1020,2);word(0x102c,4);word(0x108c,0x2000);word(0x1090,2);word(0x1094,2);word(0x2000+20,0x40000000);word(0x2000+24+20,0x40000000);TimingState copied;
 check(capture_timing(memory,0x1000,0,true,copied)&&copied.active&&copied.window,"first active unjudged track can accept native window");check(capture_timing(memory,0x1000,1,true,copied)&&!copied.active&&!copied.window,"future track waits for predecessor judgment");word(0x2000+20,0xc0000000);check(capture_timing(memory,0x1000,1,true,copied)&&copied.active,"predecessor judgment enables the next track");word(0x2000+24+20,0xc0000000);check(capture_timing(memory,0x1000,1,true,copied)&&!copied.active,"judged animation cannot remain actionable");
 check(!capture_timing(memory,0x1000,2,true,copied)&&!capture_timing(memory,0x1000,-1,true,copied),"track bounds enforced");
 std::cout<<"Synchronic lifecycle and native-window tests passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
