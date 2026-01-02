#ifndef EVENT2_UTIL_H_INCLUDED_
#define EVENT2_UTIL_H_INCLUDED_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#define evutil_socket_t uintptr_t
#else
#define evutil_socket_t int
#endif

int evutil_make_socket_nonblocking(evutil_socket_t sock);
int evutil_make_listen_socket_reuseable(evutil_socket_t sock);
int evutil_make_socket_closeonexec(evutil_socket_t sock);

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_UTIL_H_INCLUDED_ */
