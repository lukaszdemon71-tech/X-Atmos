#pragma once
#include "XPLMDefs.h"
typedef void* XPLMCommandRef;
typedef int XPLMCommandPhase;
enum { xplm_CommandBegin=0, xplm_CommandContinue=1, xplm_CommandEnd=2 };
typedef int (*XPLMCommandCallback_f)(XPLMCommandRef, XPLMCommandPhase, void*);
XPLM_API void XPLMDebugString(const char*);
XPLM_API void XPLMGetSystemPath(char*);
XPLM_API void* XPLMFindSymbol(const char*);
XPLM_API void XPLMEnableFeature(const char*, int);
XPLM_API XPLMCommandRef XPLMCreateCommand(const char*, const char*);
XPLM_API XPLMCommandRef XPLMFindCommand(const char*);
XPLM_API void XPLMRegisterCommandHandler(XPLMCommandRef, XPLMCommandCallback_f, int, void*);
XPLM_API void XPLMUnregisterCommandHandler(XPLMCommandRef, XPLMCommandCallback_f, int, void*);
