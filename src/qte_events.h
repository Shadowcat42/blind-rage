#pragma once
#include "menu_reader.h"
#include <array>

namespace a11y {
struct CueName {uint32_t index{},number{};bool operator==(const CueName&) const=default;};
struct QteCommand {CueName tag;uint8_t command{};bool visible{};bool operator==(const QteCommand&) const=default;};
struct QteGroup {
 uint32_t source{},index{},judge{},judge_index{};uint64_t generation{},visual_revision{};
 CueName identifier;uint8_t mode{},count{};int32_t required{};float hold_seconds{};bool active{};
 std::array<QteCommand,32> commands{};
 bool operator==(const QteGroup&) const=default;
};
enum class QteSchema {divergence,animation_cross,animation_mash,animation_hold};
inline uint32_t cue_word(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
inline CueName cue_name(const uint8_t* p){return {cue_word(p),cue_word(p+4)};}

// This runs at a selected script-return boundary, on the game thread. Copy
// bounded arrays together: the worker must never chase live judge/list pointers.
inline bool capture_qte_group(const Memory& m,uint32_t source,uint32_t pc,QteGroup& out){
 out={};if(!source)return false;
 out.source=source;out.index=m.u32(source+0x20);
 std::array<uint8_t,0x68> actor{};
 if(!source||!out.index||!m.copy(source+0xc4,actor.data(),actor.size()))return false;
 auto flags=cue_word(actor.data());out.identifier=cue_name(actor.data()+0x20);
 // Base.CheckActivate uses these same enable/start/end/area conditions before
 // judging. Disabled/ended requests still publish an inactive snapshot.
 if(!pc||!out.identifier.index||(flags&0x18800000)!=0x18800000||(flags&0x06000000))return true;
 out.mode=actor[0x50];out.required=int32_t(cue_word(actor.data()+0x54));out.hold_seconds=std::bit_cast<float>(cue_word(actor.data()+0x58));
 if(out.mode>5||!std::isfinite(out.hold_seconds)||out.hold_seconds<0)return false;
 auto list=cue_word(actor.data()+0x2c),count=cue_word(actor.data()+0x30),capacity=cue_word(actor.data()+0x34);
 if(!count||count>out.commands.size()||count>capacity||capacity>4096||!list)return false;
 out.judge=m.u32(pc+0x760);out.judge_index=m.u32(out.judge+0x20);
 std::array<uint8_t,12> array{};
 if(!out.judge||!out.judge_index||!m.copy(out.judge+0x1c4,array.data(),array.size()))return false;
 auto rows=cue_word(array.data()),length=cue_word(array.data()+4),allocated=cue_word(array.data()+8);
 if(length>64||length>allocated||allocated>4096||(!rows&&length))return false;
 std::array<uint8_t,64*0x5c> judges{};
 if(length&&!m.copy(rows,judges.data(),length*0x5c))return false;
 const uint8_t* judge=nullptr;
 for(uint32_t i=0;i<length;++i){auto* row=judges.data()+i*0x5c;if(cue_name(row+0x50)==out.identifier){judge=row;break;}}
 if(!judge||!(cue_word(judge+0x48)&0x80000000)||(cue_word(judge+0x58)&0x80000000)||judge[0x4c])return true;
 out.required=int32_t(cue_word(judge+0x10));out.hold_seconds=std::bit_cast<float>(cue_word(judge+0x14));
 if(!std::isfinite(out.hold_seconds)||out.hold_seconds<0)return false;
 auto commands=cue_word(judge),n=cue_word(judge+4),cap=cue_word(judge+8);
 auto input=cue_word(judge+0x20),input_n=cue_word(judge+0x24),input_cap=cue_word(judge+0x28);
 if(n!=count||n>cap||cap>4096||input_n!=n||input_n>input_cap||input_cap>4096||!commands||!input||judge[0xc]!=out.mode)return false;
 std::array<uint8_t,32> buttons{};std::array<uint8_t,32*12> info{};std::array<uint8_t,32*0x30> requests{};
 if(!m.copy(commands,buttons.data(),n)||!m.copy(input,info.data(),n*12)||!m.copy(list,requests.data(),n*0x30))return false;
 auto ordered=int32_t(cue_word(judge+0x34));if(out.mode==0&&(ordered<0||uint32_t(ordered)>=n))return false;
 out.count=uint8_t(count);out.active=true;
 for(uint32_t i=0;i<n;++i){
  auto* item=requests.data()+i*0x30;auto judged=cue_word(info.data()+i*12),shown=cue_word(item+0x2c);
  if(item[0]!=buttons[i]||item[0]>42)return false;
  auto& command=out.commands[i];command.command=item[0];command.tag=cue_name(item+0x24);
  // Request() only initializes InputInfo[0].bShow for several modes even
  // though RequestButtonIcon displays the whole simultaneous/mash/hold set.
  // RequestList.bShow and the presentation lifecycle establish visibility;
  // judge success and JudgeIdx establish completion/current ordered input.
  command.visible=command.tag.index&&!(judged&0x80000000)&&(shown&0x80000000)&&!(shown&0x40000000)&&!(cue_word(item+0x20)&0x80000000)&&(out.mode!=0||uint32_t(ordered)==i);
 }
 return true;
}
inline bool capture_animation_qte(const Memory& m,uint32_t source,QteSchema schema,QteGroup& out){
 out={};out.source=source;out.index=m.u32(source+0x20);if(!source||!out.index||schema==QteSchema::divergence)return false;
 uint32_t flagOffset=schema==QteSchema::animation_cross?0x110:schema==QteSchema::animation_mash?0x11c:0xf0;
 uint8_t raw[4]{};if(!m.copy(source+flagOffset,raw,4))return false;auto flags=cue_word(raw);
 if(!(flags&0x20000000)||(flags&0x50000000))return true; // enable, success, output
 out.mode=schema==QteSchema::animation_hold?4:1;
 std::array<uint8_t,32*0x30> items{};uint32_t count=1;
 if(schema==QteSchema::animation_hold){
  std::array<uint8_t,12> array{};if(!m.copy(source+0xc4,array.data(),array.size()))return false;
  auto pointer=cue_word(array.data());count=cue_word(array.data()+4);auto capacity=cue_word(array.data()+8);
  if(!count||count>out.commands.size()||count>capacity||capacity>4096||!pointer||!m.copy(pointer,items.data(),count*0x30))return false;
 }else if(!m.copy(source+0xc4,items.data(),0x30))return false;
 out.active=true;out.count=uint8_t(count);
 for(uint32_t i=0;i<count;++i){auto* item=items.data()+i*0x30;auto& command=out.commands[i];command.command=item[0];command.tag=cue_name(item+0x24);if(command.command>42)return false;
  command.visible=command.tag.index&&!(cue_word(item+0x20)&0x80000000)&&!(cue_word(item+0x2c)&0x40000000)&&(schema!=QteSchema::animation_hold||(cue_word(item+0x2c)&0x80000000));
 }
 return true;
}
}
