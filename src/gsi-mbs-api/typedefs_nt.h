// $Id$
//-----------------------------------------------------------------------
//       The GSI Online Offline Object Oriented (Go4) Project
//         Experiment Data Processing at EE department, GSI
//-----------------------------------------------------------------------
// Copyright (C) 2000- GSI Helmholtzzentrum fuer Schwerionenforschung GmbH
//                     Planckstr. 1, 64291 Darmstadt, Germany
// Contact:            http://go4.gsi.de
//-----------------------------------------------------------------------
// This software can be used under the license agreements as stated
// in Go4License.txt file which is part of the distribution.
//-----------------------------------------------------------------------

/* This file called in typedefs.h defines data types for NT */

#ifndef TYPEDEF_NT_H
#define TYPEDEF_NT_H

#include <errno.h>

#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT         WSAESOCKTNOSUPPORT
#endif

#ifndef ECONNRESET
#define ECONNRESET              WSAECONNRESET
#endif

#ifndef ENOTSOCK
#define ENOTSOCK                WSAENOTSOCK
#endif

#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT            WSAEAFNOSUPPORT
#endif

#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT         WSAEPROTONOSUPPORT
#endif

#ifndef ENOBUFS
#define ENOBUFS                 WSAENOBUFS
#endif

#ifndef EISCONN
#define EISCONN                 WSAEISCONN
#endif

#ifndef ETIMEDOUT
#define ETIMEDOUT               WSAETIMEDOUT
#endif

#ifndef ENETUNREACH
#define ENETUNREACH             WSAENETUNREACH
#endif

#ifndef EADDRINUSE
#define EADDRINUSE              WSAEADDRINUSE
#endif

#ifndef ECONNREFUSED
#define ECONNREFUSED            WSAECONNREFUSED
#endif


// these constants not used, but keep them


#ifndef EWOULDBLOCK
#define EWOULDBLOCK             WSAEWOULDBLOCK
#endif

#ifndef EINPROGRESS
#define EINPROGRESS             WSAEINPROGRESS
#endif

#ifndef EALREADY
#define EALREADY                WSAEALREADY
#endif

#ifndef EDESTADDRREQ
#define EDESTADDRREQ            WSAEDESTADDRREQ
#endif

#ifndef EMSGSIZE
#define EMSGSIZE                WSAEMSGSIZE
#endif

#ifndef EPROTOTYPE
#define EPROTOTYPE              WSAEPROTOTYPE
#endif

#ifndef ENOPROTOOPT
#define ENOPROTOOPT             WSAENOPROTOOPT
#endif

#ifndef EOPNOTSUPP
#define EOPNOTSUPP              WSAEOPNOTSUPP
#endif

#ifndef EPFNOSUPPORT
#define EPFNOSUPPORT            WSAEPFNOSUPPORT
#endif

#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL           WSAEADDRNOTAVAIL
#endif

#ifndef ENETDOWN
#define ENETDOWN                WSAENETDOWN
#endif

#ifndef ENETRESET
#define ENETRESET               WSAENETRESET
#endif

#ifndef ECONNABORTED
#define ECONNABORTED            WSAECONNABORTED
#endif

#ifndef ENOTCONN
#define ENOTCONN                WSAENOTCONN
#endif

#ifndef ESHUTDOWN
#define ESHUTDOWN               WSAESHUTDOWN
#endif

#ifndef ETOOMANYREFS
#define ETOOMANYREFS            WSAETOOMANYREFS
#endif

#ifndef ELOOP
#define ELOOP                   WSAELOOP
#endif

#ifndef ENAMETOOLONG
#define ENAMETOOLONG            WSAENAMETOOLONG
#endif

#ifndef EHOSTDOWN
#define EHOSTDOWN               WSAEHOSTDOWN
#endif

#ifndef EHOSTUNREACH
#define EHOSTUNREACH            WSAEHOSTUNREACH
#endif

#ifndef ENOTEMPTY
#define ENOTEMPTY               WSAENOTEMPTY
#endif

#ifndef EPROCLIM
#define EPROCLIM                WSAEPROCLIM
#endif

#ifndef EUSERS
#define EUSERS                  WSAEUSERS
#endif

#ifndef EDQUOT
#define EDQUOT                  WSAEDQUOT
#endif

#ifndef ESTALE
#define ESTALE                  WSAESTALE
#endif

#ifndef EREMOTE
#define EREMOTE                 WSAEREMOTE
#endif

#endif
