#include "../src/native_hooks.h"
#include <cstdio>
#include <cstdlib>

using Function=int(*)(int);
static Function original;
static int calls;
extern "C" __declspec(noinline) int hook_target(int value){return value*7+3;}
extern "C" __declspec(noinline) int hook_replacement(int value){++calls;return original(value)+11;}
extern "C" __declspec(noinline) int direct_caller(int value){return hook_target(value);}
static void require(bool pass,const char* message){if(!pass){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
 a11y::NativeHooks hooks;
 require(hooks.initialize(),"initialize");
 require(hooks.initialize(),"initialize twice");
 require(!hooks.add(nullptr,(void*)hook_replacement,(void**)&original),"reject invalid target");
 require(hooks.add((void*)hook_target,(void*)hook_replacement,(void**)&original),"create");
 require(!hooks.add((void*)hook_target,(void*)hook_replacement,(void**)&original),"reject duplicate target");
 require(hooks.enable(),"enable");
 require(direct_caller(2)==28&&calls==1,"intercept direct call and preserve original");
 Function volatile indirect=hook_target;
 require(indirect(3)==35&&calls==2,"intercept indirect call");
 require(hooks.disable(),"disable");
 require(indirect(3)==24&&calls==2,"restore behavior");
 require(hooks.enable(),"reenable");
 require(indirect(4)==42&&calls==3,"hook after reenable");
 require(hooks.clear(),"remove hooks");
 require(indirect(4)==31&&calls==3,"restore after removal");
 require(hooks.clear(),"clear twice");
 std::puts("Native hook tests passed.");
}
