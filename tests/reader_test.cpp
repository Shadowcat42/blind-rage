#include "../src/menu_reader.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <limits>
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
 put(0x4020,0x5000);put(0x41c8,0x6000);put(0x51c8,0x7000);put(0x601c,0x8000);put(0x701c,0x8100);
 put(0x8000,1);put(0x8004,3);put(0x8100,2);put(0x8104,3);
 for(uint32_t table:{0x8000u,0x8100u})for(unsigned i=0;i<4;i++)put(table+8+i*32,0xfffffffe);
 put(0x9000,0x9100);memcpy(data.data()+0x9100,"m_input",8);put(0x9010,0x9120);memcpy(data.data()+0x9120,"m_gameDiffHelp",15);put(0x9020,0x9140);memcpy(data.data()+0x9140,"tl_opend",9);
 auto boolSlot=[&](uint32_t entry,uint32_t key,bool value){put(entry,0xffffffff);put(entry+8,key);data[entry+16]=2;data[entry+20]=value;};
 boolSlot(0x8008,0x9000,true);boolSlot(0x8108,0x9000,false);boolSlot(0x8128,0x9010,true);
 check(!reader.selector_input_active(fake,0x4000),"New Game handoff suppresses retained child selector");
 data[0x813c]=0;check(reader.selector_input_active(fake,0x4000),"ordinary delegated input remains available without handoff phase");data[0x813c]=1;
 boolSlot(0x8028,0x9020,false);put(0x8000,2);check(reader.selector_input_active(fake,0x4000),"dialog choices retain their own pending lifecycle during handoff");
 Menu status;status.message=true;status.cursor=0;status.items.push_back({"Press Start","",true});check(describe(status,true,true)=="Press Start","screen messages have no menu numbering");
 Menu confirmation;confirmation.modal=true;confirmation.heading="Restart the game now?";confirmation.cursor=1;confirmation.items={{"Yes","",true},{"No","",true}};
 check(describe(confirmation,true,true)=="Restart the game now? No","confirmation includes prompt and selected answer on entry");confirmation.cursor=0;
 check(describe(confirmation,false,true)=="Yes","confirmation navigation excludes prompt and other answers");
 auto dialogContext=menu_context(confirmation);confirmation.cursor=1;check(menu_context(confirmation)==dialogContext,"answer changes retain dialog context");
 auto nextQuestion=confirmation;nextQuestion.heading="Start on Normal?";check(menu_context(nextQuestion)!=dialogContext,"new question on a reused panel gets a new context even with the same selected answer");
 confirmation.pending=true;check(describe(confirmation,true,true).empty(),"wait for confirmation selector rather than reading all answers");
 Menu collection;collection.collection=true;collection.heading="GAUGES";collection.acquired="1/9";collection.cursor=0;collection.items.push_back({"Avenger","Asura's default gauge.",true});
 check(describe(collection,true,true)=="GAUGES. Acquired: 1/9. Avenger: Asura's default gauge.","acquired total on collection entry");
 check(describe(collection,false,true)=="Avenger: Asura's default gauge.","collection movement includes effect but excludes heading and acquired total");
 collection.heading.clear();check(describe(collection,true,true)=="Acquired: 1/9. Avenger: Asura's default gauge.","empty collection heading adds no leading punctuation");
 collection.items[0]={"???","Complete the requirement.",false};check(describe(collection,false,true)=="Locked: Complete the requirement","locked item announces requirement without placeholder or double punctuation");
 Value gridCursor;gridCursor.type=3;gridCursor.number=2;Value gridPage=gridCursor;gridPage.number=1;Value pageSize=gridCursor;pageSize.type=4;pageSize.number=10;
 check(collection_grid_index(&gridCursor,&gridPage,&pageSize,45)==12,"paged collection uses page offset and local cursor");
 gridCursor.type=4;check(collection_grid_index(&gridCursor,&gridPage,&pageSize,45)==12,"integer collection cursor");
 gridPage.number=4;gridCursor.number=4;check(collection_grid_index(&gridCursor,&gridPage,&pageSize,45)==44,"partial last collection page");
 gridCursor.number=5;check(collection_grid_index(&gridCursor,&gridPage,&pageSize,45)==-1,"reject empty cell on partial collection page");
 gridCursor.number=std::numeric_limits<double>::quiet_NaN();check(collection_grid_index(&gridCursor,&gridPage,&pageSize,45)==-1,"reject invalid collection cursor");
 gridCursor.number=2;pageSize.number=0;check(collection_grid_index(&gridCursor,&gridPage,&pageSize,45)==-1,"reject zero page size");
 Menu lockedGrid=collection;lockedGrid.items[0]={"???","",false};lockedGrid.collection_count=45;lockedGrid.collection_position=0;
 auto firstLocked=describe(lockedGrid,false,true);lockedGrid.collection_position=1;
 check(firstLocked=="Locked. 1 of 45"&&describe(lockedGrid,false,true)=="Locked. 2 of 45","identical locked grid entries remain distinguishable on movement");
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
  if(menus.back().collection){
   auto object=memory.u32(menus.back().selector+0x1c8),table=memory.u32(object+0x1c),mask=memory.u32(table+4);
   for(uint32_t i=0;i<=mask&&mask<4096;i++){
    auto entry=table+8+i*32;if(memory.u32(entry)==0xfffffffe||memory.string(memory.u32(memory.u32(entry+8)))!="m_input"||uint64_t(entry)+32>low.size())continue;
    char saved=low[entry+20];low[entry+20]=0;auto background=r.menus(memory);low[entry+20]=saved;
    check(std::none_of(background.begin(),background.end(),[](const Menu& menu){return menu.collection;}),"inactive retained collection panel cannot override its parent menu");
    check(r.menus(memory)==menus,"reactivated collection panel restores its selection");break;
   }
  }
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
  if(menus.back().modal&&!menus.back().message){
   bool handoff=false;std::vector<uint32_t> nodes;auto movie=menus.back().movie;
   for(auto offset:{0x17cu,0x180u}){auto gfx=memory.u32(movie+offset);if(memory.u32(gfx+0x40)==0x48)nodes.push_back(memory.u32(memory.u32(gfx+0x44)+4));}
   std::set<uint32_t> seen;
   while(!nodes.empty()&&seen.size()<3000){auto c=nodes.back();nodes.pop_back();if(!seen.insert(c).second)continue;auto fields=r.members(memory,memory.u32(c+0x1c8));auto help=Reader::get(fields,"m_gameDiffHelp"),input=Reader::get(fields,"m_input");if(help&&help->type==2&&help->boolean&&input&&input->type==2&&!input->boolean)handoff=true;auto kids=r.children(memory,c);nodes.insert(nodes.end(),kids.begin(),kids.end());}
   if(handoff){auto flags=menus.back().selector+0xa0;check(uint64_t(flags)+4<=low.size(),"handoff dialog flags are captured");char saved=low[flags+3];low[flags+3]=char(uint8_t(saved)&~1u);auto gap=r.menus(memory);low[flags+3]=saved;
    check(gap.empty(),"difficulty-to-confirmation gap cannot announce the dormant title menu or fallback labels");check(r.menus(memory)==menus,"dialog restored after handoff test");
   }
  }
  for(const auto& m:menus){std::cout<<describe(m,true,true)<<"\n";for(const auto& i:m.items)std::cout<<"  "<<(i.enabled?"":"[disabled] ")<<i.label<<"\n";}
 }
 std::cout<<"Reader checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
