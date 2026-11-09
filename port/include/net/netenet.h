#ifndef _IN_NETENET_H
#define _IN_NETENET_H

#define ENET_NO_PRAGMA_LINK 1
#include "external/enet.h"
#undef bool
#undef near
#undef far
#undef s_host   /* winsock's in_addr shorthand; net_core.c has a variable of that name */

#endif
