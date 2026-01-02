#ifndef EVENT2_KEYVALQ_STRUCT_H_INCLUDED_
#define EVENT2_KEYVALQ_STRUCT_H_INCLUDED_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct evkeyvalq {
    struct evkeyval *tqh_first;
};

struct evkeyval {
    char *key;
    char *value;
    struct {
        struct evkeyval *tqe_next;
        struct evkeyval **tqe_prev;
    } next;
};

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_KEYVALQ_STRUCT_H_INCLUDED_ */
