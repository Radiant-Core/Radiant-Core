#ifndef EVENT2_EVENT_H_INCLUDED_
#define EVENT2_EVENT_H_INCLUDED_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EV_READ 0x01
#define EV_WRITE 0x02
#define EV_TIMEOUT 0x04

struct event_base {
    int dummy;
};

struct event {
    int dummy;
};

uint32_t event_get_version_number(void);
struct event_base *event_base_new(void);
void event_base_free(struct event_base *base);
struct event *event_new(struct event_base *base, uintptr_t fd, short events, void (*cb)(uintptr_t, short, void *), void *arg);
void event_free(struct event *event);
void event_active(struct event *event, int res, short ncalls);
int evtimer_add(struct event *ev, struct timeval *tv);
int event_base_dispatch(struct event_base *base);
int event_base_got_break(struct event_base *base);
int event_add(struct event *ev, struct timeval *tv);
int event_base_loopbreak(struct event_base *base);
int evutil_parse_sockaddr_port(const char *ip_as_string, struct sockaddr *out, int *outlen);

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_EVENT_H_INCLUDED_ */
