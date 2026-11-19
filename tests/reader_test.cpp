#include "../src/menu_reader.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace a11y;
static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
 check(clean_text(" <font color='#fff'>Sound &amp; Music</font>\n  80 ")=="Sound & Music 80","text cleanup");
 Menu menu;menu.movie=1;menu.selector=2;menu.heading="Options";menu.cursor=0;menu.items={{"Sound volume, 80","Adjust volume.",true},{"Locked","",false}};
 check(describe(menu,true,true)=="Options. Sound volume, 80. 1 of 2. Adjust volume.","description includes value and help");menu.cursor=1;
 check(describe(menu,false,false)=="Locked. Unavailable. 2 of 2","disabled control");menu.cursor=-1;check(describe(menu,true,true).empty(),"invalid cursor");
 std::vector<unsigned char> data(0x10000);auto put=[&](uint32_t at,uint32_t v){for(int i=0;i<4;i++)data[at+i]=v>>(24-8*i);};
 Memory fake([&](uint32_t at,void* out,size_t n){if(uint64_t(at)+n>data.size())return false;memcpy(out,data.data()+at,n);return true;});
 Reader reader;put(0x100+0x1c,0x200);put(0x200,1);put(0x204,1);put(0x208,0xffffffff);put(0x210,0x300);put(0x300,0x400);memcpy(data.data()+0x400,"m_cursor",9);data[0x218]=3;put(0x220,0x40000000);put(0x228,0xfffffffe);
 auto members=reader.members(fake,0x100);check(members.size()==1&&members.at("m_cursor").number==2,"occupied -1 bucket and big-endian number");
 put(0x204,0x100000);check(reader.members(fake,0x100).empty(),"reject corrupt hash capacity");
 put(0x500+0xbc,0x600);put(0x608,0x700);put(0x70c,0x800);put(0x710,1);put(0x714,1);put(0x800,0x900);put(0x900,0xa00);put(0x904,3);put(0x908,3);
 data[0xa00]=0;data[0xa01]='O';data[0xa02]=0;data[0xa03]='K';
 check(reader.native_text(fake,0x500)=="OK","native UTF-16 text paragraph");put(0x904,4097);check(reader.native_text(fake,0x500).empty(),"reject corrupt native text length");
 put(0xb00+0x20,0xc00);put(0xb00+0xa0,1);put(0xc00+0xa0,3);check(!reader.visible(fake,0xb00),"degenerate ancestor excludes selector");put(0xc00+0xa0,1);check(reader.visible(fake,0xb00),"visible ancestor chain");put(0xc00+0xa0,0);check(!reader.visible(fake,0xb00),"explicitly invisible ancestor excludes selector");put(0xc00+0xa0,1);
 Menu status;status.message=true;status.cursor=0;status.items.push_back({"Press Start","",true});check(describe(status,true,true)=="Press Start","screen messages have no menu numbering");
 Menu confirmation;confirmation.modal=true;confirmation.heading="Restart the game now?";confirmation.cursor=1;confirmation.items={{"Yes","",true},{"No","",true}};
 check(describe(confirmation,true,true)=="Restart the game now? No","confirmation includes prompt and selected answer on entry");confirmation.cursor=0;
 check(describe(confirmation,false,true)=="Yes","confirmation navigation excludes prompt and other answers");
 confirmation.pending=true;check(describe(confirmation,true,true).empty(),"wait for confirmation selector rather than reading all answers");
 Menu collection;collection.collection=true;collection.heading="GAUGES";collection.acquired="1/9";collection.cursor=0;collection.items.push_back({"Avenger","Asura's default gauge.",true});
 check(describe(collection,true,true)=="GAUGES. Acquired: 1/9. Avenger: Asura's default gauge.","acquired total on collection entry");
 check(describe(collection,false,true)=="Avenger: Asura's default gauge.","collection movement includes effect but excludes heading and acquired total");
 collection.heading.clear();check(describe(collection,true,true)=="Acquired: 1/9. Avenger: Asura's default gauge.","empty collection heading adds no leading punctuation");
 collection.items[0]={"???","Complete the requirement.",false};check(describe(collection,false,true)=="Locked: Complete the requirement","locked item announces requirement without placeholder or double punctuation");
 if(argc>=2){
  std::string root=argv[1];auto load=[](const std::string& p){std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("Missing snapshot "+p);return std::vector<char>(std::istreambuf_iterator<char>(in),{});};
  auto low=load(root+"/heap-low.bin"),high=load(root+"/heap-high.bin"),image=load(root+"/guest-image.bin");
  std::ifstream globals(root+"/globals.bin",std::ios::binary);if(globals)globals.read(image.data()+0x12a9000,0x2000);
  uint64_t reads=0;Memory memory([&](uint32_t a,void* out,size_t n){reads++;for(auto [base,buffer]:std::array<std::pair<uint32_t,std::vector<char>*>,3>{{{0,&low},{0x40000000,&high},{0x82000000,&image}}}){if(a>=base&&uint64_t(a)-base+n<=buffer->size()){memcpy(out,buffer->data()+a-base,n);return true;}}return false;});
  Reader r;check(r.ready(memory),"snapshot Unreal globals");r.discover(memory);std::ofstream diagnostic(root+"/diagnostic.txt");r.diagnostic(memory,diagnostic);diagnostic.close();auto menus=r.menus(memory);check(!menus.empty(),"snapshot has a readable active menu");
  {
   auto movie=menus.back().movie;auto slot=memory.u32(Reader::objects_global)+memory.u32(movie+0x20)*4;
   if(uint64_t(slot)+4<=low.size()){
    char saved[4];memcpy(saved,low.data()+slot,4);memset(low.data()+slot,0,4);Reader late;late.discover(memory);memcpy(low.data()+slot,saved,4);
    late.discover(memory);auto published=late.menus(memory);check(std::any_of(published.begin(),published.end(),[&](const Menu& menu){return menu.movie==movie;}),"rediscover menu published in an existing object slot");
   }
  }
  if(argc==3&&std::string(argv[2])=="--bench"){
   reads=0;auto start=std::chrono::steady_clock::now();for(int i=0;i<40;i++){auto result=r.menus(memory);check(result==menus,"stable repeated snapshot result");}auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/40;
   std::cout<<"POLL "<<ms<<" ms, "<<reads/40<<" reads\n";
   reads=0;start=std::chrono::steady_clock::now();for(int i=0;i<10;i++)r.discover(memory);ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/10;
   std::cout<<"DISCOVER "<<ms<<" ms, "<<reads/10<<" reads\n";
  }else if(argc==3)check(menus.back().items[menus.back().cursor].label==argv[2],"snapshot selected item matches expected label");
  if(menus.back().modal&&!menus.back().message){
   check(!menus.back().heading.empty(),"confirmation retains the question as well as the selected button");
   std::vector<uint32_t> todo{menus.back().selector};std::set<uint32_t> seen;bool tested=false;
   while(!todo.empty()&&!tested){auto c=todo.back();todo.pop_back();if(!seen.insert(c).second)continue;
    auto fields=r.members(memory,memory.u32(c+0x1c8));auto items=Reader::get(fields,"m_items");
    if(items&&items->type==6&&memory.u32(items->pointer+0x3c)>0){
     auto at=items->pointer+0x3c;if(uint64_t(at)+4<=low.size()){
      char saved[4];memcpy(saved,low.data()+at,4);memset(low.data()+at,0,4);auto opening=r.menus(memory);memcpy(low.data()+at,saved,4);
      check(!opening.empty()&&opening.back().modal&&opening.back().pending,"opening choice dialog blocks background menu until selector is published");
      check(describe(opening.back(),true,true).empty(),"opening dialog cannot announce all choices");
      auto ready=r.menus(memory);check(ready==menus,"published confirmation restores selected answer");tested=true;
     }
    }auto kids=r.children(memory,c);todo.insert(todo.end(),kids.begin(),kids.end());
   }check(tested,"confirmation opening-state regression exercised");
  }
  if(!menus.back().message&&!menus.back().collection&&!menus.back().settings){
   auto target=menus.back();std::vector<uint32_t> todo{target.selector};std::set<uint32_t> seen;bool tested=false;
   while(!todo.empty()&&!tested){auto c=todo.back();todo.pop_back();if(!seen.insert(c).second)continue;auto object=memory.u32(c+0x1c8);auto fields=r.members(memory,object);
    auto items=Reader::get(fields,"m_items"),cursor=Reader::get(fields,"m_cursor");
    if(items&&cursor&&memory.u32(items->pointer+0x3c)==target.items.size()){
     auto table=memory.u32(object+0x1c),mask=memory.u32(table+4);for(uint32_t i=0;i<=mask&&mask<4096;i++){
      auto e=table+8+i*32;if(memory.u32(e)==0xfffffffe||memory.string(memory.u32(memory.u32(e+8)))!="m_cursor")continue;
      if(uint64_t(e)+32>low.size())continue;char saved[16];memcpy(saved,low.data()+e+16,16);low[e+16]=4;for(int j=0;j<4;j++)low[e+24+j]=uint32_t(target.cursor)>>(24-8*j);
      auto integerSelection=r.menus(memory);memcpy(low.data()+e+16,saved,16);
      check(integerSelection==menus,"initial integer cursor produces the same menu and focused choice as a floating-point cursor");tested=true;break;
     }
    }auto kids=r.children(memory,c);todo.insert(todo.end(),kids.begin(),kids.end());
   }check(tested,"integer-cursor regression exercised");
  }
  if(menus.back().modal&&!menus.back().message){
   auto object=memory.u32(menus.back().selector+0x1c8),table=memory.u32(object+0x1c),mask=memory.u32(table+4);bool exercised=false;
   for(uint32_t i=0;i<=mask&&mask<4096;i++){
    auto entry=table+8+i*32;if(memory.u32(entry)==0xfffffffe||memory.string(memory.u32(memory.u32(entry+8)))!="tl_opend"||uint64_t(entry)+32>low.size())continue;
    char saved=low[entry+20];low[entry+20]=0;auto opening=r.menus(memory);low[entry+20]=saved;
    check(!opening.empty()&&opening.back().pending,"dialog choices remain pending before the dialog opens");exercised=true;break;
   }
   check(exercised,"opening-dialog regression exercised");
  }
  for(const auto& m:menus){std::cout<<describe(m,true,true)<<"\n";for(const auto& i:m.items)std::cout<<"  "<<(i.enabled?"":"[disabled] ")<<i.label<<"\n";}
 }
 std::cout<<"Reader checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
