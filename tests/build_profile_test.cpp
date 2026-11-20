#include "../src/build_profile.h"
#include <sstream>
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void require(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
int main(){try{
 require(profile_named("disc")==&disc_profile&&profile_named("updated")==&updated_profile&&profile_named("title-update")==&updated_profile,"known profile names");
 require(profile_named("latest")==nullptr,"reject unverified profile names");
 for(auto profile:build_profiles){
  std::array<std::string,3> hashes;for(unsigned i=0;i<3;i++)hashes[i]=profile->fingerprints[i];
  require(profile_fingerprints(hashes)==profile,"select exact complete fingerprint set");
  hashes[1]="modified runtime";require(profile_fingerprints(hashes)==nullptr,"reject modified runtime despite matching executable");
 }
 std::array<std::string,3> mixed{updated_profile.fingerprints[0],disc_profile.fingerprints[1],updated_profile.fingerprints[2]};
 require(profile_fingerprints(mixed)==nullptr,"reject mixed disc and title-update binaries");
 std::istringstream updated("# installer\ngame=1\ntitle_update=1\n");require(installed_title_update(updated)==true,"title update selected from installer state");
 std::istringstream disc("game=1\n title_update = 0 # disc only\n");require(installed_title_update(disc)==false,"disc installer state and whitespace");
 std::istringstream missing("game=1\n");require(!installed_title_update(missing).has_value(),"missing update state is distinguishable from disc");
 std::istringstream invalid("title_update=unknown\n");require(!installed_title_update(invalid).has_value(),"reject ambiguous installer state");
 require(updated_profile.layout.sprite_vtable!=disc_profile.layout.sprite_vtable,"separate verified sprite layouts");
 std::cout<<"Build profile tests passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
