#ifndef CONKER_NATIVE_UPDATE_H
#define CONKER_NATIVE_UPDATE_H
#include <stddef.h>
#include <stdint.h>
#include "update_config.h"
#define NATIVE_UPDATE_API "https://api.github.com/repos/" NATIVE_UPDATE_REPOSITORY "/releases/latest"
#define NATIVE_UPDATE_MAX_ZIP (32u*1024u*1024u)
struct NativeUpdateRelease {char tag[64],url[768],digest[65];size_t bytes;};
int native_update_newer(const char *remote,const char *installed);
/* CIA downloads are installed by FBI, never by replacing the running title. */
#define CONKER_CIA_TITLE_ID UINT64_C(0x000400000E0CBF00)
int native_update_validate_cia(const char *file,char *error,size_t capacity);
int native_update_release_for(const char *json,size_t length,const char *installed,int cia,struct NativeUpdateRelease *out);
int native_update_release(const char *json,size_t length,const char *installed,struct NativeUpdateRelease *out);
/* A nonzero return means the installed pair is usable, or no recovery was needed. */
int native_update_recover(const char *directory,char *error,size_t capacity);
/* Stage both files, check ZIP CRCs and headers, then replace with a rollback journal. */
int native_update_install(const char *zip,const char *directory,int (*progress)(size_t,size_t),char *error,size_t capacity);
#endif
