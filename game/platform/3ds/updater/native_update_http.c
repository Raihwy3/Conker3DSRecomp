/* Adapted from Eyes of Gold Launcher. Copyright (c) 2026 Raihwy3. MIT. */
#include "native_update_http.h"
#include "native_update.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#ifdef NATIVE_UPDATE_HOST
#include <openssl/evp.h>
#else
#include <mbedtls/sha256.h>
#endif
struct Writer {struct NativeUpdateTransfer *t;FILE *file;};
static size_t write_data(char *data,size_t size,size_t count,void *arg) {
    struct Writer *w=arg;struct NativeUpdateTransfer *t=w->t;
    if(size && count>SIZE_MAX/size)return 0;
    size_t bytes=size*count,limit=w->file?t->expected:t->capacity-1;
    if((t->cancel && t->cancel(t->user)) || t->length>limit || bytes>limit-t->length)return 0;
    if(w->file){if(fwrite(data,1,bytes,w->file)!=bytes)return 0;}
    else memcpy(t->memory+t->length,data,bytes);
    t->length+=bytes;
    if(t->progress)t->progress(t->user,t->length,w->file?t->expected:0);
    return bytes;
}
static int progress(void *arg,curl_off_t total,curl_off_t now,curl_off_t upload_total,curl_off_t upload_now) {
    (void)total;(void)now;(void)upload_total;(void)upload_now;
    struct NativeUpdateTransfer *t=arg;return t->cancel && t->cancel(t->user);
}
static int file_hash(const char *name,const char *expected) {
    unsigned char result[32]={0},buffer[8192];int ok=1;size_t bytes;FILE *f=fopen(name,"rb");if(!f)return 0;
#ifdef NATIVE_UPDATE_HOST
    EVP_MD_CTX *hash=EVP_MD_CTX_new();if(!hash){fclose(f);return 0;}
    ok=EVP_DigestInit_ex(hash,EVP_sha256(),NULL)==1;
    while(ok && (bytes=fread(buffer,1,sizeof buffer,f)))ok=EVP_DigestUpdate(hash,buffer,bytes)==1;
    unsigned length=0;if(ok)ok=EVP_DigestFinal_ex(hash,result,&length)==1 && length==32;EVP_MD_CTX_free(hash);
#else
    mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);ok=mbedtls_sha256_starts_ret(&hash,0)==0;
    while(ok && (bytes=fread(buffer,1,sizeof buffer,f)))ok=mbedtls_sha256_update_ret(&hash,buffer,bytes)==0;
    if(ok)ok=mbedtls_sha256_finish_ret(&hash,result)==0;
    mbedtls_sha256_free(&hash);
#endif
    if(ferror(f))ok=0;
    fclose(f);char hex[65];
    for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",result[i]);
    return ok && expected && !strcmp(hex,expected);
}
int native_update_http(const char *url,struct NativeUpdateTransfer *t,const void *ca,size_t ca_size,char *error,size_t capacity) {
    int ok=0;char detail[CURL_ERROR_SIZE]={0};struct Writer writer={t,NULL};CURL *curl=NULL;
    if(!url || strncmp(url,"https://",8) || (!t->file && (!t->memory || t->capacity<2)) ||
       (t->file && (!t->digest || !t->expected || t->expected>NATIVE_UPDATE_MAX_ZIP)))goto end;
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)goto end;
    curl=curl_easy_init();if(!curl)goto cleanup;
    if(t->file){writer.file=fopen(t->file,"wb");if(!writer.file)goto cleanup;}
    t->length=0;
    struct curl_blob trust={(void *)ca,ca_size,CURL_BLOB_NOCOPY};
#define SET(option,value) do {if(curl_easy_setopt(curl,option,value)!=CURLE_OK)goto cleanup;} while(0)
    SET(CURLOPT_URL,url);SET(CURLOPT_USERAGENT,"Conker-3DS-Recomp/update-check");
    SET(CURLOPT_FOLLOWLOCATION,1L);SET(CURLOPT_MAXREDIRS,4L);
    SET(CURLOPT_PROTOCOLS_STR,"https");SET(CURLOPT_REDIR_PROTOCOLS_STR,"https");
    SET(CURLOPT_SSL_VERIFYPEER,1L);SET(CURLOPT_SSL_VERIFYHOST,2L);SET(CURLOPT_CAINFO_BLOB,&trust);
    SET(CURLOPT_CONNECTTIMEOUT_MS,3000L);SET(CURLOPT_TIMEOUT_MS,t->file?120000L:4000L);
    SET(CURLOPT_LOW_SPEED_LIMIT,1024L);SET(CURLOPT_LOW_SPEED_TIME,8L);
    SET(CURLOPT_NOSIGNAL,1L);SET(CURLOPT_FAILONERROR,1L);SET(CURLOPT_ERRORBUFFER,detail);
    SET(CURLOPT_WRITEFUNCTION,write_data);SET(CURLOPT_WRITEDATA,&writer);
    SET(CURLOPT_NOPROGRESS,0L);SET(CURLOPT_XFERINFOFUNCTION,progress);SET(CURLOPT_XFERINFODATA,t);
    CURLcode result=curl_easy_perform(curl);long status=0;
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    if(writer.file){if(fflush(writer.file) || fsync(fileno(writer.file)))result=CURLE_WRITE_ERROR;if(fclose(writer.file))result=CURLE_WRITE_ERROR;writer.file=NULL;}
    if(result==CURLE_OK && status==200 && !(t->cancel && t->cancel(t->user))) {
        if(t->file)ok=t->length==t->expected && file_hash(t->file,t->digest);
        else {t->memory[t->length]=0;ok=1;}
    }
cleanup:
    if(writer.file)fclose(writer.file);
    if(curl)curl_easy_cleanup(curl);
    curl_global_cleanup();
end:
    if(!ok){if(t->file)remove(t->file);if(capacity)snprintf(error,capacity,"%s",detail[0]?detail:"Download failed or did not match the release checksum.");}
    return ok;
#undef SET
}
