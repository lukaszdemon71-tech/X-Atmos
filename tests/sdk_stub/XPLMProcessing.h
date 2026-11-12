#pragma once
#include "XPLMDefs.h"
typedef void* XPLMFlightLoopID;
typedef float (*XPLMFlightLoop_f)(float, float, int, void*);
enum { xplm_FlightLoop_Phase_BeforeFlightModel=0, xplm_FlightLoop_Phase_AfterFlightModel=1 };
typedef struct { int structSize; int phase; XPLMFlightLoop_f callbackFunc; void* refcon; } XPLMCreateFlightLoop_t;
XPLM_API XPLMFlightLoopID XPLMCreateFlightLoop(XPLMCreateFlightLoop_t*);
XPLM_API void XPLMScheduleFlightLoop(XPLMFlightLoopID, float, int);
XPLM_API void XPLMDestroyFlightLoop(XPLMFlightLoopID);
XPLM_API float XPLMGetElapsedTime();
