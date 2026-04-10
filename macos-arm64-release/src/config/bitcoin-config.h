// Copyright (c) 2017-2021 The Bitcoin developers

#pragma once

#define PACKAGE_NAME "Radiant Core"

#define COPYRIGHT_YEAR "2026"
#define COPYRIGHT_HOLDERS "The %s developers"
#define COPYRIGHT_HOLDERS_SUBSTITUTION "Radiant;Core"
#define COPYRIGHT_HOLDERS_FINAL ""

/* #undef HAVE_ENDIAN_H */
#define HAVE_SYS_ENDIAN_H 1

#define HAVE_DECL_HTOLE16 1
#define HAVE_DECL_HTOBE16 1
#define HAVE_DECL_BE16TOH 1
#define HAVE_DECL_LE16TOH 1
#define HAVE_DECL_HTOBE32 1
#define HAVE_DECL_HTOLE32 1
#define HAVE_DECL_BE32TOH 1
#define HAVE_DECL_LE32TOH 1
#define HAVE_DECL_HTOBE64 1
#define HAVE_DECL_HTOLE64 1
#define HAVE_DECL_BE64TOH 1
#define HAVE_DECL_LE64TOH 1

/* #undef HAVE_BYTESWAP_H */

/* #undef HAVE_DECL_BSWAP_16 */
/* #undef HAVE_DECL_BSWAP_32 */
/* #undef HAVE_DECL_BSWAP_64 */

#define HAVE_SYS_SELECT_H 1
/* #undef HAVE_SYS_PRCTL_H */

#define HAVE_DECL___BUILTIN_CLZ 1
#define HAVE_DECL___BUILTIN_CLZL 1
#define HAVE_DECL___BUILTIN_CLZLL 1
#define HAVE_DECL___BUILTIN_POPCOUNT 1

/* #undef HAVE_MALLOPT_ARENA_MAX */
/* #undef HAVE_MALLOC_INFO */

#define HAVE_DECL_STRNLEN 1
#define HAVE_DECL_DAEMON 1
#define HAVE_DECL_GETIFADDRS 1
#define HAVE_DECL_FREEIFADDRS 1
/* #undef HAVE_GETENTROPY */
#define HAVE_GETENTROPY_RAND 1
/* #undef HAVE_SYS_GETRANDOM */
/* #undef HAVE_SYSCTL_ARND */

/* #undef CHAR_EQUALS_INT8 */
#define HAVE_LARGE_FILE_SUPPORT 1

#define HAVE_FUNC_ATTRIBUTE_VISIBILITY 1
/* #undef HAVE_FUNC_ATTRIBUTE_DLLEXPORT */

#define FDELT_TYPE long int

/* #undef ENABLE_WALLET */
/* #undef ENABLE_ZMQ */

/* Define if QR support should be compiled in */
/* #undef USE_QRCODE */

/* UPnP support not compiled if undefined */
/* #undef ENABLE_UPNP */
#ifdef ENABLE_UPNP
/* Value (0 or 1) determines the UPnP default state at startup. */
#define USE_UPNP 0
#endif

/* Define if QtDBus support should be enabled */
/* #undef USE_DBUS */
