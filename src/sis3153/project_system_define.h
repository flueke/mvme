// Note (flueke): Modified to use the Qt platform defines.
#include <QtGlobal>

#if defined Q_OS_LINUX
    #define LINUX
#elif defined Q_OS_WIN
    #define WIN
    #define WINDOWS
#elif defined Q_OS_OSX
    #define LINUX
    #define MAC_OSX
#endif

/*
#define LINUX    // set if MAC_OSX, also  else WIN
//#define MAC_OSX



// don't change below

#ifdef MAC_OSX
	#define LINUX
#endif

#ifndef LINUX
	#define WIN
    #define WINDOWS
#endif
*/

