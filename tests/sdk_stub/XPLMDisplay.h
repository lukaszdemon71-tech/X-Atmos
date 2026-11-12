#pragma once
#include "XPLMDefs.h"
typedef void* XPLMWindowID;
enum { xplm_WindowDecorationNone=0, xplm_WindowDecorationRoundRectangle=1,
       xplm_WindowDecorationSelfDecorated=2, xplm_WindowDecorationSelfDecoratedResizable=3 };
enum { xplm_WindowLayerFlightOverlay=0, xplm_WindowLayerFloatingWindows=1,
       xplm_WindowLayerModal=2, xplm_WindowLayerGrowlNotifications=3 };
enum { xplm_WindowContentTypeOpenGL=0, xplm_WindowContentTypeBrowser=1 };
typedef const char* (*XPLMBrowserCallback_f)(XPLMWindowID, const char*, void*);
typedef void (*XPLMBrowserLoadFinished_f)(XPLMWindowID, const char*, void*);
typedef void (*XPLMBrowserLoadError_f)(XPLMWindowID, const char*, const char*, void*);
typedef void (*XPLMDrawWindow_f)(XPLMWindowID, void*);
typedef int XPLMMouseStatus;
typedef int XPLMCursorStatus;
typedef int XPLMKeyFlags;
enum { xplm_MouseDown = 1, xplm_MouseDrag = 2, xplm_MouseUp = 3 };
enum { xplm_CursorDefault = 0 };
typedef int (*XPLMHandleMouseClick_f)(XPLMWindowID, int, int, XPLMMouseStatus, void*);
typedef int (*XPLMHandleMouseWheel_f)(XPLMWindowID, int, int, int, int, void*);
typedef XPLMCursorStatus (*XPLMHandleCursor_f)(XPLMWindowID, int, int, void*);
typedef void (*XPLMHandleKey_f)(XPLMWindowID, char, XPLMKeyFlags, char, void*, int);
typedef struct {
    int structSize; int left, top, right, bottom; int visible;
    XPLMDrawWindow_f drawWindowFunc;
    XPLMHandleMouseClick_f handleMouseClickFunc;
    XPLMHandleKey_f handleKeyFunc;
    XPLMHandleCursor_f handleCursorFunc;
    XPLMHandleMouseWheel_f handleMouseWheelFunc;
    void* refcon;
    int decorateAsFloatingWindow; int layer;
    XPLMHandleMouseClick_f handleRightClickFunc;
    int contentType;
    XPLMBrowserLoadFinished_f browserLoadFinishedFunc;
    XPLMBrowserLoadError_f browserLoadErrorFunc;
} XPLMCreateWindow_t;
XPLM_API XPLMWindowID XPLMCreateWindowEx(XPLMCreateWindow_t*);
XPLM_API void XPLMDestroyWindow(XPLMWindowID);
XPLM_API void XPLMSetWindowTitle(XPLMWindowID, const char*);
XPLM_API void XPLMSetWindowIsVisible(XPLMWindowID, int);
XPLM_API int  XPLMGetWindowIsVisible(XPLMWindowID);
XPLM_API void XPLMGetWindowGeometry(XPLMWindowID, int*, int*, int*, int*);
XPLM_API void XPLMSetWindowGeometry(XPLMWindowID, int, int, int, int);
XPLM_API void XPLMSetWindowResizingLimits(XPLMWindowID, int, int, int, int);
XPLM_API void XPLMGetScreenBoundsGlobal(int*, int*, int*, int*);
