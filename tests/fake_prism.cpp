#define PRISM_BUILDING
#include <prism.h>
#include <cstring>
struct PrismContext {};
struct PrismBackend {PrismBackendId id;};
extern "C" {
PrismContext* PRISM_CALL prism_init(PrismConfig*){return new PrismContext;}
void PRISM_CALL prism_shutdown(PrismContext* context){delete context;}
PrismBackend* PRISM_CALL prism_registry_create(PrismContext*,PrismBackendId id){return new PrismBackend{id};}
PrismBackend* PRISM_CALL prism_registry_create_best(PrismContext*){return new PrismBackend{PRISM_BACKEND_INVALID};}
void PRISM_CALL prism_backend_free(PrismBackend* backend){delete backend;}
const char* PRISM_CALL prism_backend_name(PrismBackend* backend){return backend->id==PRISM_BACKEND_NVDA?"NVDA test backend":"Fallback test backend";}
PrismError PRISM_CALL prism_backend_speak(PrismBackend* backend,const char* text,bool){
 if(!std::strcmp(text,"fail"))return PRISM_ERROR_SPEAK_FAILURE;
 if(!std::strcmp(text,"fallback")&&backend->id==PRISM_BACKEND_NVDA)return PRISM_ERROR_BACKEND_NOT_AVAILABLE;
 return PRISM_OK;
}
PrismError PRISM_CALL prism_backend_stop(PrismBackend*){return PRISM_OK;}
const char* PRISM_CALL prism_error_string(PrismError){return "synthetic speech failure";}
}
