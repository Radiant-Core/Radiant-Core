#ifndef EVENT2_THREAD_H_INCLUDED_
#define EVENT2_THREAD_H_INCLUDED_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int evthread_use_pthreads(void);
int evthread_use_windows_threads(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_THREAD_H_INCLUDED_ */
