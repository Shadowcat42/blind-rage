#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace a11y {
// Installed Mark2 ABI, independently checked against its native implementations:
// r3 at 0, r4 at 0x20, r5 at 0x28, r6 at 0x30. Do not assume that every
// ReXGlue version has this layout. The launcher fingerprint gate is required.
inline uint32_t ui_argument(const void* context,unsigned reg) noexcept {
 uint32_t value{};
 if(reg<3||reg>7)return 0;
 std::memcpy(&value,static_cast<const uint8_t*>(context)+(reg==3?0:reg*8),4);
 return value;
}

inline bool narrated_member(std::string_view name) noexcept {
 // Shared semantic fields, independent of individual menus and their paths.
 constexpr std::string_view fields[]={"m_cursor","m_items","m_details","m_param","m_info",
  "m_input","m_instSelector","m_instTitle","m_getItemNum","m_text","_text","m_pageNow","m_pageItemMax","m_gameDiffHelp",
  "tl_opend","tl_type","Title","Text","Label","Name","Help","HelpText",
  "Locked","Get","bActive","Inst","InstName","Sel","_visible","text","htmlText"};
 for(auto field:fields)if(field==name)return true;
 return false;
}

// Small, allocation-free observations used on the game's thread. Read is supplied
// by the adapter so synthetic tests exercise the same decoding without a game.
struct UiValue {
 uint8_t type{};
 uint64_t payload{};
 bool valid{};
 bool operator==(const UiValue&) const=default;
};
template<class Read> uint32_t ui_word(Read&& read,uint32_t at) noexcept {
 uint8_t b[4]{};if(!at||!read(at,b,4))return 0;
 return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];
}
template<class Read> UiValue ui_value(Read&& read,uint32_t at) noexcept {
 uint8_t b[16]{};if(!at||!read(at,b,16))return {};
 UiValue result{b[0],0,true};
 // Ignore padding, flags, and unused union bytes; they are not value changes.
 if(b[0]==2)result.payload=b[4]!=0;
 else if(b[0]==3){for(unsigned i=8;i<16;i++)result.payload=result.payload<<8|b[i];}
 else if(b[0]>=4&&b[0]<=10){for(unsigned i=4;i<8;i++)result.payload=result.payload<<8|b[i];if(b[0]==4){result.payload=0;for(unsigned i=8;i<12;i++)result.payload=result.payload<<8|b[i];}}
 return result;
}
struct UiAssignment {
 uint32_t object{},key{};
 bool relevant{};
 UiValue before;
};
template<class Read> UiValue ui_member_value(Read&& read,uint32_t object,uint32_t key) noexcept {
 // object is the secondary AS interface, whose hash table is at +0xc.
 auto table=ui_word(read,object+0xc);
 auto mask=ui_word(read,table+4);
 if(!table||mask>65535||(mask&(mask+1)))return {};
 auto index=ui_word(read,key+0xc)&mask;
 for(unsigned probes=0;probes<=mask&&probes<65536;probes++){
  const auto entry=table+8+index*32;
  auto next=ui_word(read,entry);
  if(next==0xfffffffe)return {};
  if(ui_word(read,entry+8)==key)return ui_value(read,entry+16);
  if(next==0xffffffff||next>mask)return {};
  index=next;
 }
 return {};
}
template<class Read> UiAssignment ui_before_assignment(Read&& read,const void* context) noexcept {
 UiAssignment result;
 result.object=ui_argument(context,3);
 result.key=ui_word(read,ui_argument(context,5));
 auto text=ui_word(read,result.key);
 char name[64]{};
 if(!text||!read(text,name,sizeof(name))||!std::memchr(name,0,sizeof(name)))return result;
 result.relevant=narrated_member(name);
 if(result.relevant)result.before=ui_member_value(read,result.object,result.key);
 return result;
}
template<class Read> bool ui_after_assignment(Read&& read,const UiAssignment& assignment) noexcept {
 if(!assignment.relevant)return false;
 return assignment.before!=ui_member_value(read,assignment.object,assignment.key);
}
}
