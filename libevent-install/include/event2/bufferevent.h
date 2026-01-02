#ifndef EVENT2_BUFFEREVENT_H_INCLUDED_
#define EVENT2_BUFFEREVENT_H_INCLUDED_

#include <stdint.h>
#include "event.h"
#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct bufferevent {
    struct evbuffer *input;
    struct evbuffer *output;
    int dummy;
};

struct bufferevent *bufferevent_new(int fd, void (*readcb)(struct bufferevent *, void *), void (*writecb)(struct bufferevent *, void *), void (*errorcb)(struct bufferevent *, short, void *), void *arg);
void bufferevent_free(struct bufferevent *bev);
int bufferevent_enable(struct bufferevent *bufev, short event);
int bufferevent_disable(struct bufferevent *bufev, short event);
int bufferevent_write(struct bufferevent *bufev, const void *data, size_t size);
struct evbuffer *bufferevent_get_input(struct bufferevent *bufev);
struct evbuffer *bufferevent_get_output(struct bufferevent *bufev);
struct bufferevent *bufferevent_socket_new(struct event_base *base, int fd, int options);
void bufferevent_setcb(struct bufferevent *bufev, void (*readcb)(struct bufferevent *, void *), void (*writecb)(struct bufferevent *, void *), void (*errorcb)(struct bufferevent *, short, void *), void *cbarg);
int bufferevent_socket_connect(struct bufferevent *bev, const struct sockaddr *address, int addrlen);

#define BEV_EVENT_READING 0x01
#define BEV_EVENT_WRITING 0x02
#define BEV_EVENT_EOF 0x10
#define BEV_EVENT_ERROR 0x20
#define BEV_EVENT_TIMEOUT 0x40
#define BEV_EVENT_CONNECTED 0x80
#define BEV_OPT_CLOSE_ON_FREE 0x01

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_BUFFEREVENT_H_INCLUDED_ */
