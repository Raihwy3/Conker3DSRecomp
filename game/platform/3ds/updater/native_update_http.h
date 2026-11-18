#ifndef CONKER_NATIVE_UPDATE_HTTP_H
#define CONKER_NATIVE_UPDATE_HTTP_H
#include <stddef.h>
struct NativeUpdateTransfer {
    char *memory;size_t capacity,length;
    const char *file,*digest;size_t expected;
    int (*cancel)(void *);void (*progress)(void *,size_t,size_t);void *user;
};
int native_update_http(const char *url,struct NativeUpdateTransfer *transfer,const void *ca,size_t ca_size,char *error,size_t error_size);
#endif
