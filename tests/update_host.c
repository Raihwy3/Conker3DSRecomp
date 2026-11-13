#include "native_update.h"
#include "native_update_http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
int native_test_rename(const char *,const char *);
static int fail_at,renames,cancel_at,progress_calls;
static int cancelled(void *unused){(void)unused;return getenv("EOG_TEST_CANCEL")!=NULL;}
int native_test_rename(const char *a,const char *b){if(++renames==fail_at){errno=EIO;return -1;}return rename(a,b);}
static int progress(size_t done,size_t total){(void)done;(void)total;return !cancel_at || ++progress_calls<cancel_at;}
static char *read_file(const char *path,size_t *bytes){FILE *f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);if(size<0){fclose(f);return NULL;}char *p=calloc((size_t)size+1,1);if(!p){fclose(f);return NULL;}*bytes=fread(p,1,(size_t)size,f);fclose(f);return p;}
int main(int argc,char **argv){
 char error[256]={0};
 if(argc==4 && !strcmp(argv[1],"compare"))return native_update_newer(argv[2],argv[3])?0:1;
 if(argc==4 && !strcmp(argv[1],"release")){
  size_t n;char *json=read_file(argv[2],&n);if(!json)return 2;struct NativeUpdateRelease r={0};int ok=native_update_release(json,n,argv[3],&r);free(json);
  if(ok)printf("%s\n%s\n%s\n%zu\n",r.tag,r.url,r.digest,r.bytes);return ok?0:1;
 }
 if(argc>=4 && !strcmp(argv[1],"install")){
  if(argc>4)fail_at=atoi(argv[4]);if(argc>5)cancel_at=atoi(argv[5]);
  int ok=native_update_install(argv[2],argv[3],progress,error,sizeof error);if(!ok)fprintf(stderr,"%s\n",error);return ok?0:1;
 }
 if(argc==3 && !strcmp(argv[1],"recover")){int ok=native_update_recover(argv[2],error,sizeof error);if(!ok)fprintf(stderr,"%s\n",error);return ok?0:1;}
 if(argc==7 && !strcmp(argv[1],"fetch")){
  size_t ca_bytes;char *ca=read_file(argv[3],&ca_bytes);if(!ca)return 2;
  char data[4096];struct NativeUpdateTransfer t={0};t.memory=data;t.capacity=sizeof data;t.cancel=cancelled;
  if(strcmp(argv[4],"-")){t.file=argv[4];t.expected=(size_t)strtoul(argv[5],NULL,10);t.digest=argv[6];}
  int ok=native_update_http(argv[2],&t,ca,ca_bytes,error,sizeof error);free(ca);
  if(ok && !t.file)puts(data);if(!ok)fprintf(stderr,"%s\n",error);return ok?0:1;
 }
 return 2;
}
