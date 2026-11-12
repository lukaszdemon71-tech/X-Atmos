#pragma once
#include "XPLMDefs.h"
typedef void* XPLMDataRef;
typedef int XPLMDataTypeID;
enum { xplmType_Unknown=0, xplmType_Int=1, xplmType_Float=2, xplmType_Double=4,
       xplmType_FloatArray=8, xplmType_IntArray=16, xplmType_Data=32 };
XPLM_API XPLMDataRef XPLMFindDataRef(const char*);
XPLM_API XPLMDataTypeID XPLMGetDataRefTypes(XPLMDataRef);
XPLM_API float XPLMGetDataf(XPLMDataRef);
XPLM_API int   XPLMGetDatai(XPLMDataRef);
XPLM_API void  XPLMSetDataf(XPLMDataRef, float);
XPLM_API void  XPLMSetDatai(XPLMDataRef, int);
XPLM_API void  XPLMSetDatavf(XPLMDataRef, float*, int, int);
XPLM_API void  XPLMSetDatavi(XPLMDataRef, int*, int, int);
