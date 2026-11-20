#pragma once
#include <array>
#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
namespace a11y {
struct ReaderLayout {
 uint32_t names_global=0x832a9c58,objects_global=0x832a9c98;
 uint32_t sprite_vtable=0x82028718,text_vtable=0x8202bcb0;
 uint32_t title_process_offset=0x1f1;
};
struct BuildProfile {
 const char* id;
 const wchar_t* directory;
 std::array<const char*,3> fingerprints;
 std::array<uintptr_t,10> hook_rvas;
 ReaderLayout layout;
 const char* patch_fingerprint;
 std::array<uintptr_t,2> presentation_rvas;
 std::array<uintptr_t,2> prompt_state_rvas;
 std::array<uintptr_t,2> gameplay_rvas;
};
inline constexpr BuildProfile disc_profile{
 "disc",L"bin_disc",
 {"35e6586c44f2972a9f0b8a289a671bc1699a83db83ab49fd55dee594bbe52bc9","f24da9895acea3799e5af26caafb751fa2463cc5267df549caa70289be19f321","795aa1c041ac9c1d5a35b08deee9a6742936fcd6752aaba7a02478b925e594f2"},
 {0x029b5050,0x045f72b0,0x01a647d0,0x0212c6c0,0x03b841c0,0x02b146b0,0x0040ce60,0x008071c0,0x0164bd00,0x01ce3ce0},
 {},nullptr,{0x030822c0,0x0406ec00},{0x04e83cb1,0x04e83cc8},{0x0466e5a0,0x023e9ae0}
};
inline constexpr BuildProfile updated_profile{
 "updated",L"bin",
 {"e73431cfb9e9c4c08d26b93162d0a1a6b41cf982cf1103074401c358bfec9d04","c72cfdfef9ef660b74bcc03b013ad73595b7c97f2ec38ad63862cef290d5d46c","9ee946458d427fed8cb00b0ba50a124e8b79b8280ca33afbe1d1fd913233c942"},
 {0x02438e80,0x03c91080,0x031cfaa0,0x02073600,0x025454c0,0x021d4b90,0x03ba5f10,0x01c13090,0x041a81b0,0x0137b5b0},
 {0x832a9c58,0x832a9c98,0x82028710,0x8202bcb0,0x1f1},
 "918cdee90389f27dee0bfe1bdfd758bc52cc4148666dcae1e35a1c0297f2005c",{0x0348bb10,0x03078010},{0x04e83cb1,0x04e83cc8},{0x03aa6770,0x00df51f0}
};
inline constexpr std::array<const BuildProfile*,2> build_profiles{&updated_profile,&disc_profile};
inline const BuildProfile* profile_named(std::string_view name){
 if(name=="disc")return &disc_profile;
 if(name=="updated"||name=="title-update")return &updated_profile;
 return nullptr;
}
inline const BuildProfile* profile_fingerprints(const std::array<std::string,3>& hashes){
 for(auto profile:build_profiles){bool match=true;for(unsigned i=0;i<3;i++)if(hashes[i]!=profile->fingerprints[i])match=false;if(match)return profile;}
 return nullptr;
}
inline std::optional<bool> installed_title_update(std::istream& input){
 std::optional<bool> result;std::string line;
 auto trim=[](std::string value){auto begin=value.find_first_not_of(" \t\r\n");if(begin==std::string::npos)return std::string{};return value.substr(begin,value.find_last_not_of(" \t\r\n")-begin+1);};
 while(std::getline(input,line)){
  if(auto comment=line.find('#');comment!=std::string::npos)line.resize(comment);
  auto equals=line.find('=');if(equals==std::string::npos||trim(line.substr(0,equals))!="title_update")continue;
  auto value=trim(line.substr(equals+1));if(value!="0"&&value!="1")return {};result=value=="1";
 }
 return result;
}
}
