#pragma once
// Compile-check stub. Not the X-Plane SDK. See README.md.
#include "XPLMDefs.h"
typedef int XPLMFontID;
enum { xplmFont_Basic = 0, xplmFont_Proportional = 18 };
XPLM_API void  XPLMSetGraphicsState(int, int, int, int, int, int, int);
XPLM_API void  XPLMDrawString(float*, int, int, char*, int*, XPLMFontID);
XPLM_API void  XPLMGetFontDimensions(XPLMFontID, int*, int*, int*);
XPLM_API float XPLMMeasureString(XPLMFontID, const char*, int);
