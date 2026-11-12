#pragma once
#include "XPLMDefs.h"
#define XPLM_MSG_PLANE_CRASHED 101
#define XPLM_MSG_PLANE_LOADED 102
#define XPLM_MSG_AIRPORT_LOADED 103
#define XPLM_MSG_SCENERY_LOADED 104
XPLM_API XPLMPluginID XPLMGetMyID();
XPLM_API void XPLMGetPluginInfo(XPLMPluginID, char*, char*, char*, char*);

typedef int XPLMPluginID;
#define XPLM_NO_PLUGIN_ID (-1)
XPLMPluginID XPLMFindPluginBySignature(const char* inSignature);
