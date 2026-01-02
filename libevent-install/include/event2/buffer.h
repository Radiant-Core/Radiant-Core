#ifndef EVENT2_BUFFER_H_INCLUDED_
#define EVENT2_BUFFER_H_INCLUDED_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum evbuffer_eol_style {
    EVBUFFER_EOL_ANY,
    EVBUFFER_EOL_CRLF,
    EVBUFFER_EOL_CRLF_STRICT,
    EVBUFFER_EOL_LF
};

struct evbuffer {
    int dummy;
};

struct evbuffer *evbuffer_new(void);
void evbuffer_free(struct evbuffer *buf);
int evbuffer_add(struct evbuffer *buf, const void *data, size_t datlen);
int evbuffer_remove(struct evbuffer *buf, void *data, size_t datlen);
size_t evbuffer_get_length(const struct evbuffer *buf);
unsigned char *evbuffer_pullup(struct evbuffer *buf, ssize_t size);
int evbuffer_drain(struct evbuffer *buf, size_t size);
char *evbuffer_readln(struct evbuffer *buffer, size_t *n_read_out, enum evbuffer_eol_style eol_style);

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_BUFFER_H_INCLUDED_ */
