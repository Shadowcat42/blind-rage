#pragma once
#include "qte_events.h"
#include "presentation_events.h"
#include "input_labels.h"
#include <sstream>
#include <cctype>

namespace a11y {
enum class InputAction {press,move,rotate,sequence};
struct InputDescription {std::string label;InputAction action=InputAction::press;};
using GestureLabel=std::function<InputDescription(std::string_view)>;
inline InputDescription controller_gesture(std::string_view tag){
 constexpr std::array<std::pair<std::string_view,std::string_view>,9> directions{{{"LUp","left stick up"},{"LDown","left stick down"},{"LLeft","left stick left"},{"LRight","left stick right"},{"RUp","right stick up"},{"RDown","right stick down"},{"RLeft","right stick left"},{"RRight","right stick right"},{"LRightDown","left stick diagonally down-right"}}};
 for(auto [name,text]:directions)if(name==tag)return {std::string(text),InputAction::move};
 constexpr std::array<std::pair<std::string_view,std::string_view>,6> rotations{{{"RRotateL","left stick clockwise"},{"LRotateL","left stick counterclockwise"},{"RRotateR","right stick clockwise"},{"LRotateR","right stick counterclockwise"},{"RRotateSet","sticks clockwise"},{"LRotateSet","sticks counterclockwise"}}};
 for(auto [name,text]:rotations)if(name==tag)return {std::string(text),InputAction::rotate};
 return {};
}
inline InputDescription describe_qte_icon(std::string tag,const ButtonLabel& buttons,const GestureLabel& gestures){
 for(auto prefix:{"Barrage_","Hold_","Finish_"})if(tag.starts_with(prefix)){tag.erase(0,std::strlen(prefix));break;}
 if(tag.size()==2&&tag[1]=='4'&&std::string_view("AXYB").find(tag[0])!=std::string_view::npos)tag.pop_back();
 if(tag=="BURST")tag="RT";
 if(auto label=buttons?buttons(tag):controller_button(tag,PromptDevice::xbox);!label.empty())return {label,InputAction::press};
 return gestures?gestures(tag):controller_gesture(tag);
}
inline std::string qte_instruction(const QteGroup& group,const std::vector<InputDescription>& inputs){
 if(inputs.empty()||group.mode>5)return {};
 bool same=std::all_of(inputs.begin(),inputs.end(),[&](const InputDescription& input){return input.action==inputs.front().action;});
 auto verb=[](InputAction action){return action==InputAction::move?"move":action==InputAction::rotate?"rotate":"press";};
 std::string list;for(size_t i=0;i<inputs.size();++i){if(inputs[i].label.empty())return {};if(i)list+=" and ";if(!same)list+=std::string(verb(inputs[i].action))+" ";list+=inputs[i].label;}
 std::string text;
 if(group.mode==2||group.mode==4){
  text=(group.mode==4?"Keep holding ":"Hold ")+list;
  if(group.mode==2&&group.hold_seconds>0){std::ostringstream duration;duration<<group.hold_seconds;text+=" for "+duration.str()+" seconds";}
  if(group.mode==2&&group.required==0)text+=", then release";
 }else if(group.mode==1){
  if(same&&inputs.front().action==InputAction::press)text="Mash "+list;
  else text=(same?std::string(verb(inputs.front().action))+" ":std::string{})+list+" repeatedly";
  if(group.required>1&&group.required<=999)text+=", "+std::to_string(group.required)+" times";
 }else{
  text=(same?std::string(verb(inputs.front().action))+" ":std::string{})+list;
  if(group.mode==3&&inputs.size()>1)text+=" together";
 }
 if(!text.empty())text[0]=char(std::toupper(static_cast<unsigned char>(text[0])));return text+".";
}

class QteReader {
 public:
  using NameLookup=std::function<std::string(CueName)>;
  struct Movie {uint32_t pointer{},index{};bool operator==(const Movie&) const=default;};
  explicit QteReader(ButtonLabel buttons={},GestureLabel gestures={}):buttons_(std::move(buttons)),gestures_(std::move(gestures)){}
  void reset(){icons_.clear();groups_.clear();visibility_.clear();}
  void apply(const PresentationEvent& event){
   Movie movie{event.movie,event.index};auto tag=event.tag.utf8();
   std::erase_if(icons_,[&](const Icon& icon){return icon.movie.pointer==movie.pointer&&icon.movie.index!=movie.index;});
   if(event.kind==PresentationKind::button_end){closed(movie);return;}
   if(event.kind==PresentationKind::button_create){
    std::erase_if(icons_,[&](const Icon& icon){return icon.movie==movie&&icon.tag==tag;});
    if(icons_.size()==128)icons_.erase(icons_.begin());icons_.push_back({movie,tag,event.type.utf8(),true,true,event.order,++icon_serial_,event.revision});return;
   }
   for(auto& icon:icons_)if(icon.movie==movie&&icon.tag==tag){
    if(event.kind==PresentationKind::button_visible){icon.visible=event.flag;icon.order=event.order;icon.revision=event.revision;}
    else if(event.kind==PresentationKind::button_active){icon.enabled=event.flag;icon.order=event.order;icon.revision=event.revision;}
    else if(event.kind==PresentationKind::button_success||event.kind==PresentationKind::button_failure||event.kind==PresentationKind::button_finish){icon.visible=false;icon.enabled=false;}
   }
  }
  void apply(const QteGroup& group,uint64_t order,const NameLookup& name){
   std::erase_if(groups_,[&](const Group& old){return old.data.source==group.source&&old.data.index!=group.index;});
   auto it=std::find_if(groups_.begin(),groups_.end(),[&](const Group& old){return old.data.source==group.source&&old.data.index==group.index;});
   if(it==groups_.end()){if(groups_.size()==32)groups_.erase(groups_.begin());groups_.push_back({});it=std::prev(groups_.end());}
   it->data=group;it->order=order;it->tags.clear();
   for(unsigned i=0;i<group.count&&i<group.commands.size();++i)it->tags.push_back(name(group.commands[i].tag));
  }
  std::vector<std::string> take(){
   std::vector<std::string> out;
   for(auto& group:groups_){auto cue=current(group);if(cue.first.empty()){group.last.clear();continue;}if(cue.first==group.last)continue;group.last=cue.first;if(std::find(out.begin(),out.end(),cue.second)==out.end())out.push_back(std::move(cue.second));}
   return out;
  }
  std::string repeat() const {const Group* latest=nullptr;std::string text;for(const auto& group:groups_){auto cue=current(group);if(!cue.first.empty()&&(!latest||group.order>latest->order)){latest=&group;text=std::move(cue.second);}}return text;}
  std::vector<Movie> movies() const {std::vector<Movie> out;for(const auto& icon:icons_)if(std::find(out.begin(),out.end(),icon.movie)==out.end())out.push_back(icon.movie);return out;}
  void closed(Movie movie){std::erase_if(icons_,[&](const Icon& icon){return icon.movie==movie;});visibility_.erase({movie.pointer,movie.index});}
  void visibility(Movie movie,bool visible){visibility_[{movie.pointer,movie.index}]=visible;}
 private:
  struct Icon {Movie movie;std::string tag,input;bool visible{},enabled{};uint64_t order{},serial{},revision{};};
  struct Group {QteGroup data;uint64_t order{};std::vector<std::string> tags;std::string last;};
  std::pair<std::string,std::string> current(const Group& group) const {
   if(!group.data.active)return {};
   std::vector<InputDescription> inputs;std::string key=std::to_string(group.data.generation)+":"+std::to_string(group.data.mode)+":"+std::to_string(group.data.required)+":"+std::to_string(group.data.hold_seconds)+":";
   for(size_t i=0;i<group.tags.size();++i){
    if(!group.data.commands[i].visible)continue;
    const Icon* selected=nullptr;
    for(const auto& icon:icons_){auto visible=visibility_.find({icon.movie.pointer,icon.movie.index});if(visible!=visibility_.end()&&!visible->second)continue;if(icon.tag==group.tags[i]&&icon.visible&&icon.enabled&&icon.order<=group.order&&icon.revision<=group.data.visual_revision&&(!selected||icon.serial>selected->serial))selected=&icon;}
    // A simultaneous/hold/mash group must never be narrated as a misleading
    // partial set merely because another icon's presentation has not arrived.
    if(!selected)return {};
    auto input=describe_qte_icon(selected->input,buttons_,gestures_);if(input.label.empty())return {};
    if(std::none_of(inputs.begin(),inputs.end(),[&](const InputDescription& old){return old.label==input.label&&old.action==input.action;}))inputs.push_back(std::move(input));
    key+=std::to_string(i)+"/"+std::to_string(selected->serial)+";";
   }
   auto text=qte_instruction(group.data,inputs);return text.empty()?std::pair<std::string,std::string>{}:std::pair{key,std::move(text)};
  }
  ButtonLabel buttons_;GestureLabel gestures_;uint64_t icon_serial_{};std::vector<Icon> icons_;std::vector<Group> groups_;std::map<std::pair<uint32_t,uint32_t>,bool> visibility_;
};
}
