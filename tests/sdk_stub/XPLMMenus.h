#pragma once
#include "XPLMDefs.h"
#include "XPLMUtilities.h"
typedef void* XPLMMenuID;
typedef void (*XPLMMenuHandler_f)(void*, void*);
XPLM_API XPLMMenuID XPLMFindPluginsMenu();
XPLM_API int  XPLMAppendMenuItem(XPLMMenuID, const char*, void*, int);
XPLM_API void XPLMAppendMenuItemWithCommand(XPLMMenuID, const char*, XPLMCommandRef);
XPLM_API void XPLMAppendMenuSeparator(XPLMMenuID);
XPLM_API XPLMMenuID XPLMCreateMenu(const char*, XPLMMenuID, int, XPLMMenuHandler_f, void*);
XPLM_API void XPLMDestroyMenu(XPLMMenuID);
