#ifndef EVENT2_HTTP_H_INCLUDED_
#define EVENT2_HTTP_H_INCLUDED_

#include <stdint.h>
#include "event.h"
#include "buffer.h"
#include "keyvalq_struct.h"
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

struct evhttp;
struct evhttp_request;
struct evhttp_connection;
struct evkeyvalq;
struct evhttp_bound_socket;

typedef void (*event_callback_fn)(uintptr_t, short, void *);

enum evhttp_cmd_type {
    EVHTTP_REQ_GET,
    EVHTTP_REQ_POST,
    EVHTTP_REQ_HEAD,
    EVHTTP_REQ_PUT,
    EVHTTP_REQ_DELETE,
    EVHTTP_REQ_OPTIONS
};

struct evhttp *evhttp_new(struct event_base *base);
void evhttp_free(struct evhttp *http);
struct evhttp_bound_socket *evhttp_bind_socket_with_handle(struct evhttp *http, const char *address, unsigned short port);
int evhttp_bind_socket(struct evhttp *http, const char *address, unsigned short port);
struct evhttp_request *evhttp_request_new(void (*cb)(struct evhttp_request *, void *), void *arg);
void evhttp_request_free(struct evhttp_request *req);
void evhttp_send_reply(struct evhttp_request *req, int code, const char *reason, struct evbuffer *dbuf);
void evhttp_send_error(struct evhttp_request *req, int error, const char *reason);
int evhttp_add_header(struct evkeyvalq *headers, const char *key, const char *value);
const char *evhttp_find_header(const struct evkeyvalq *headers, const char *key);
struct evbuffer *evhttp_request_get_input_buffer(struct evhttp_request *req);
struct evbuffer *evhttp_request_get_output_buffer(struct evhttp_request *req);
struct evkeyvalq *evhttp_request_get_input_headers(struct evhttp_request *req);
struct evkeyvalq *evhttp_request_get_output_headers(struct evhttp_request *req);
struct evhttp_connection *evhttp_request_get_connection(struct evhttp_request *req);
struct bufferevent *evhttp_connection_get_bufferevent(struct evhttp_connection *conn);
void evhttp_connection_get_peer(struct evhttp_connection *conn, char **address, unsigned short *port);
const char *evhttp_request_get_uri(struct evhttp_request *req);
int evhttp_request_get_command(struct evhttp_request *req);
void evhttp_set_timeout(struct evhttp *http, int timeout_in_secs);
void evhttp_set_max_headers_size(struct evhttp *http, size_t max_size);
void evhttp_set_max_body_size(struct evhttp *http, size_t max_size);
void evhttp_set_gencb(struct evhttp *http, void (*cb)(struct evhttp_request *, void *), void *arg);
void evhttp_set_allowed_methods(struct evhttp *http, int methods);
void event_set_log_callback(void (*cb)(int, const char *));
struct event_base *event_base_new(void);
void event_base_free(struct event_base *base);
struct event *event_new(struct event_base *base, evutil_socket_t fd, short events, void (*cb)(evutil_socket_t, short, void *), void *arg);
void event_free(struct event *event);
void event_active(struct event *event, int res, short ncalls);
int evtimer_add(struct event *ev, struct timeval *tv);
int event_base_dispatch(struct event_base *base);
int event_base_got_break(struct event_base *base);
void evhttp_del_accept_socket(struct evhttp *http, struct evhttp_bound_socket *socket);
struct evhttp_connection *evhttp_connection_base_new(struct event_base *base, const char *dnsbase, const char *host, unsigned short port);
void evhttp_connection_free(struct evhttp_connection *conn);
int evhttp_request_get_response_code(struct evhttp_request *req);
void evhttp_connection_set_timeout(struct evhttp_connection *evcon, int timeout_in_secs);
char *evhttp_uriencode(const char *str, size_t len, int space_as_plus);
int evhttp_make_request(struct evhttp_connection *evcon, struct evhttp_request *req, enum evhttp_cmd_type type, const char *uri);

#define HTTP_INTERNAL 500
#define HTTP_BADMETHOD 405
#define HTTP_NOTFOUND 404
#define HTTP_SERVUNAVAIL 503

#define EVENT_LOG_WARN 1
#define EVENT_LOG_ERR 2

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_HTTP_H_INCLUDED_ */
