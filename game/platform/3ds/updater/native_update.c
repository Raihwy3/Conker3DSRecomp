/* Adapted from Eyes of Gold Launcher. Copyright (c) 2026 Raihwy3. MIT. */
#include "native_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <minizip/unzip.h>
#define JSMN_STATIC
#define JSMN_STRICT
#include "jsmn.h"
#ifndef NATIVE_UPDATE_RENAME
#define NATIVE_UPDATE_RENAME rename
#endif
static const char *const names[]={"ConkerRecomp.3dsx","ConkerRecomp.smdh"};
static int version(const char *s,unsigned v[4]) {
    if(*s=='v')s++;
    for(int i=0;i<4;i++)v[i]=0;
    for(int i=0;i<3;i++) {
        if(*s<'0' || *s>'9')return 0;
        do {if(v[i]>6553)return 0;v[i]=v[i]*10+(unsigned)(*s++-'0');}while(*s>='0' && *s<='9');
        if(i<2 && *s++!='.')return 0;
    }
    if(!*s)return 1;
    if(s[0]!='-' || s[1]!='r')return 0;
    s+=2;if(*s<'0' || *s>'9')return 0;
    do {if(v[3]>6553)return 0;v[3]=v[3]*10+(unsigned)(*s++-'0');}while(*s>='0' && *s<='9');
    return !*s;
}
int native_update_newer(const char *remote,const char *installed) {
    unsigned a[4],b[4];if(!version(remote,a) || !version(installed,b))return 0;
    for(int i=0;i<4;i++)if(a[i]!=b[i])return a[i]>b[i];
    return 0;
}
struct Json {const char *text;jsmntok_t *tokens;int count;};
static int skip(struct Json *j,int i){int end=j->tokens[i].end;i++;while(i<j->count && j->tokens[i].start<end)i++;return i;}
static int equal(struct Json *j,int i,const char *s){return i>=0 && j->tokens[i].end-j->tokens[i].start==(int)strlen(s) && !memcmp(j->text+j->tokens[i].start,s,strlen(s));}
static int field(struct Json *j,int object,const char *key) {
    if(object<0 || j->tokens[object].type!=JSMN_OBJECT)return -1;
    int found=-1;
    for(int i=object+1;i<j->count && j->tokens[i].start<j->tokens[object].end;) {
        if(j->tokens[i].type!=JSMN_STRING || i+1>=j->count)return -1;
        if(equal(j,i,key)){if(found>=0)return -1;found=i+1;}
        i=skip(j,i+1);
    }
    return found;
}
static int string(struct Json *j,int i,char *out,size_t capacity) {
    if(i<0 || j->tokens[i].type!=JSMN_STRING)return 0;
    size_t n=0;int at=j->tokens[i].start,end=j->tokens[i].end;
    while(at<end) {
        unsigned char c=j->text[at++];
        if(c=='\\'){if(at>=end)return 0;c=j->text[at++];if(c!='/' && c!='\\' && c!='"')return 0;}
        if(c<32 || c>126 || n+1>=capacity)return 0;
        out[n++]=(char)c;
    }
    out[n]=0;return 1;
}
static int number(struct Json *j,int i,size_t *out) {
    if(i<0 || j->tokens[i].type!=JSMN_PRIMITIVE)return 0;
    size_t n=0;
    for(int at=j->tokens[i].start;at<j->tokens[i].end;at++) {
        int c=j->text[at]-'0';if(c<0 || c>9 || n>NATIVE_UPDATE_MAX_ZIP/10)return 0;n=n*10+(unsigned)c;
    }
    if(!n || n>NATIVE_UPDATE_MAX_ZIP)return 0;
    *out=n;return 1;
}
int native_update_release_for(const char *json,size_t length,const char *installed,int cia,struct NativeUpdateRelease *out) {
    if(!json || !out || length>65536)return 0;
    jsmntok_t *tokens=calloc(4096,sizeof *tokens);if(!tokens)return 0;
    jsmn_parser parser;jsmn_init(&parser);int n=jsmn_parse(&parser,json,length,tokens,4096);
    struct Json j={json,tokens,n};struct NativeUpdateRelease r={0};int ok=0;
    if(n<1 || tokens[0].type!=JSMN_OBJECT)goto end;
    for(size_t i=(size_t)tokens[0].end;i<length;i++)if(json[i]!=' ' && json[i]!='\r' && json[i]!='\n' && json[i]!='\t')goto end;
    if(!equal(&j,field(&j,0,"draft"),"false") || !equal(&j,field(&j,0,"prerelease"),"false"))goto end;
    if(!string(&j,field(&j,0,"tag_name"),r.tag,sizeof r.tag) || !native_update_newer(r.tag,installed))goto end;
    int assets=field(&j,0,"assets");if(assets<0 || tokens[assets].type!=JSMN_ARRAY)goto end;
    for(int i=assets+1;i<n && tokens[i].start<tokens[assets].end;i=skip(&j,i)) {
        char name[160],hash[80];
        if(!string(&j,field(&j,i,"name"),name,sizeof name))continue;
        size_t size=strlen(name);
        if(size<5 || strcmp(name+size-4,cia?".cia":".zip") ||
           strncmp(name,"conker-3ds-recomp",17))continue;
        // Asset names are path components, never paths or escaped URLs.
        int safe=1;for(size_t k=0;k<size;k++)if(!((name[k]>='a' && name[k]<='z') || (name[k]>='A' && name[k]<='Z') || (name[k]>='0' && name[k]<='9') || name[k]=='-' || name[k]=='_' || name[k]=='.'))safe=0;
        if(!safe)continue;
        if(!string(&j,field(&j,i,"browser_download_url"),r.url,sizeof r.url) ||
           !string(&j,field(&j,i,"digest"),hash,sizeof hash) || strncmp(hash,"sha256:",7) || strlen(hash)!=71 ||
           !number(&j,field(&j,i,"size"),&r.bytes))continue;
        int valid=1;for(int k=7;k<71;k++)if(!((hash[k]>='0' && hash[k]<='9') || (hash[k]>='a' && hash[k]<='f')))valid=0;
        if(!valid)continue;
        char prefix[256];snprintf(prefix,sizeof prefix,"https://github.com/%s/releases/download/%s/",NATIVE_UPDATE_REPOSITORY,r.tag);
        if(strncmp(r.url,prefix,strlen(prefix)) || strcmp(r.url+strlen(prefix),name))continue;
        memcpy(r.digest,hash+7,65);*out=r;ok=1;break;
    }
end:free(tokens);return ok;
}
int native_update_release(const char *json,size_t length,const char *installed,struct NativeUpdateRelease *out) {
    return native_update_release_for(json,length,installed,0,out);
}
static int fail(char *out,size_t capacity,const char *text){if(capacity)snprintf(out,capacity,"%s",text);return 0;}
static int path(char *out,size_t capacity,const char *dir,const char *name,const char *suffix){int n=snprintf(out,capacity,"%s/%s%s",dir,name,suffix);return n>=0 && (size_t)n<capacity;}
static int exists(const char *p){struct stat st;return stat(p,&st)==0;}
int native_update_recover(const char *directory,char *error,size_t capacity) {
    char marker[640];if(!path(marker,sizeof marker,directory,"launcher-update.pending",""))return fail(error,capacity,"Update folder path is too long.");
    FILE *f=fopen(marker,"rb");if(!f)return !exists(marker)?1:fail(error,capacity,"Cannot read update recovery record.");
    unsigned char record[6]={0};size_t count=fread(record,1,sizeof record,f);fclose(f);
    if(count!=5 || memcmp(record,"CBFU",4) || record[4]>3)return fail(error,capacity,"Update recovery record is invalid.");
    for(int i=0;i<2;i++) {
        char current[640],backup[640],staged[640];
        if(!path(current,sizeof current,directory,names[i],"") || !path(backup,sizeof backup,directory,names[i],".update-old") || !path(staged,sizeof staged,directory,names[i],".update-new"))return fail(error,capacity,"Update folder path is too long.");
        if(exists(backup)) {
            if(exists(current) && remove(current))return fail(error,capacity,"Cannot recover the previous launcher.");
            if(NATIVE_UPDATE_RENAME(backup,current))return fail(error,capacity,"Cannot restore the previous launcher.");
        }else if(!(record[4]&(1u<<i)) && exists(current) && remove(current))return fail(error,capacity,"Cannot remove an incomplete update.");
        remove(staged);
    }
    if(remove(marker))return fail(error,capacity,"Cannot finish update recovery.");
    return 1;
}
static unsigned little16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static int extract(unzFile zip,const char *directory,int index,int (*progress)(size_t,size_t),char *error,size_t capacity) {
    char member[160],staged[640];snprintf(member,sizeof member,"3ds/ConkerRecomp/%s",names[index]);
    unz_file_info64 info;
    if(unzLocateFile(zip,member,1)!=UNZ_OK || unzGetCurrentFileInfo64(zip,&info,NULL,0,NULL,0,NULL,0)!=UNZ_OK ||
       (info.flag&1) || (info.compression_method!=0 && info.compression_method!=8) ||
       info.uncompressed_size<32 || info.uncompressed_size>64u*1024u*1024u ||
       (index==1 && info.uncompressed_size!=0x36c0))return fail(error,capacity,"Release has no valid launcher pair.");
    if(!path(staged,sizeof staged,directory,names[index],".update-new"))return fail(error,capacity,"Update folder path is too long.");
    if(unzOpenCurrentFile(zip)!=UNZ_OK)return fail(error,capacity,"Cannot open launcher in the ZIP.");
    FILE *f=fopen(staged,"wb");if(!f){unzCloseCurrentFile(zip);return fail(error,capacity,"Cannot stage update on the SD card.");}
    unsigned char block[16384];size_t total=0;int valid=1;
    for(;;) {
        int got=unzReadCurrentFile(zip,block,sizeof block);
        if(got<0){valid=0;break;}if(!got)break;
        if(!total && (got<32 || memcmp(block,index?"SMDH":"3DSX",4) || (!index && (little16(block+4)<32 || little16(block+6)<8)))){valid=0;break;}
        if((size_t)got>(size_t)info.uncompressed_size-total || fwrite(block,1,(size_t)got,f)!=(size_t)got){valid=0;break;}
        total+=(size_t)got;
        if(progress && !progress(total,(size_t)info.uncompressed_size)){valid=0;break;}
    }
    if(fflush(f) || fsync(fileno(f)))valid=0;
    if(fclose(f))valid=0;
    if(unzCloseCurrentFile(zip)!=UNZ_OK || total!=info.uncompressed_size)valid=0;
    if(!valid){remove(staged);return fail(error,capacity,"Update was cancelled, damaged, or could not be saved.");}
    return 1;
}
int native_update_install(const char *archive,const char *directory,int (*progress)(size_t,size_t),char *error,size_t capacity) {
    if(!native_update_recover(directory,error,capacity))return 0;
    unzFile zip=unzOpen64(archive);if(!zip)return fail(error,capacity,"Downloaded file is not a readable ZIP.");
    unsigned count[2]={0};int state=unzGoToFirstFile(zip);
    while(state==UNZ_OK) {
        char name[256];unz_file_info64 info;
        if(unzGetCurrentFileInfo64(zip,&info,name,sizeof name,NULL,0,NULL,0)!=UNZ_OK){unzClose(zip);return fail(error,capacity,"Invalid release ZIP directory.");}
        if(info.size_filename>=sizeof name){state=unzGoToNextFile(zip);continue;}
        for(int i=0;i<2;i++){char wanted[160];snprintf(wanted,sizeof wanted,"3ds/ConkerRecomp/%s",names[i]);if(!strcmp(name,wanted))count[i]++;}
        state=unzGoToNextFile(zip);
    }
    int ok=state==UNZ_END_OF_LIST_OF_FILE && count[0]==1 && count[1]==1;
    if(ok) {for(int i=0;i<2;i++)if(!extract(zip,directory,i,progress,error,capacity)){ok=0;break;}}
    else fail(error,capacity,"Release ZIP has missing or duplicate launcher files.");
    unzClose(zip);
    if(!ok){for(int i=0;i<2;i++){char p[640];if(path(p,sizeof p,directory,names[i],".update-new"))remove(p);}return 0;}
    unsigned char record[5]={'C','B','F','U',0};char marker[640];
    if(!path(marker,sizeof marker,directory,"launcher-update.pending",""))return fail(error,capacity,"Update folder path is too long.");
    for(int i=0;i<2;i++){char p[640];path(p,sizeof p,directory,names[i],"");if(exists(p))record[4]|=1u<<i;path(p,sizeof p,directory,names[i],".update-old");if(exists(p) && remove(p))return fail(error,capacity,"Cannot clear previous update backup.");}
    FILE *f=fopen(marker,"wb");if(!f)return fail(error,capacity,"Cannot save update recovery record.");
    int saved=fwrite(record,1,5,f)==5;if(fflush(f) || fsync(fileno(f)))saved=0;if(fclose(f))saved=0;
    if(!saved){remove(marker);return fail(error,capacity,"Cannot save update recovery record.");}
    for(int i=0;i<2;i++) {
        char current[640],backup[640],staged[640];path(current,sizeof current,directory,names[i],"");path(backup,sizeof backup,directory,names[i],".update-old");path(staged,sizeof staged,directory,names[i],".update-new");
        if(((record[4]&(1u<<i)) && NATIVE_UPDATE_RENAME(current,backup)) || NATIVE_UPDATE_RENAME(staged,current)) {
            if(!native_update_recover(directory,error,capacity))return 0;
            return fail(error,capacity,"Update replacement failed; previous launcher restored.");
        }
    }
    if(remove(marker)){native_update_recover(directory,error,capacity);return fail(error,capacity,"Update commit failed; recovery attempted.");}
    for(int i=0;i<2;i++){char p[640];path(p,sizeof p,directory,names[i],".update-old");remove(p);}
    return 1;
}

static uint32_t cia_u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t cia_u64(const unsigned char *p){return cia_u32(p)|(uint64_t)cia_u32(p+4)<<32;}
int native_update_validate_cia(const char *file,char *error,size_t capacity) {
    FILE *f=fopen(file,"rb");if(!f)return fail(error,capacity,"Cannot read the downloaded CIA.");
    unsigned char h[32],ncch[0x200];int valid=0;
    if(fseek(f,0,SEEK_END))goto end;
    long length=ftell(f);rewind(f);
    if(length<0 || (unsigned long)length>NATIVE_UPDATE_MAX_ZIP || fread(h,1,sizeof h,f)!=sizeof h)goto end;
    uint32_t header=cia_u32(h),cert=cia_u32(h+8),ticket=cia_u32(h+12),tmd=cia_u32(h+16);
    if(header!=0x2020 || h[4] || h[5] || h[6] || h[7] || !cert || !ticket || !tmd)goto end;
    uint64_t at=((uint64_t)header+63)&~UINT64_C(63);
    at+=((uint64_t)cert+63)&~UINT64_C(63);at+=((uint64_t)ticket+63)&~UINT64_C(63);at+=((uint64_t)tmd+63)&~UINT64_C(63);
    uint64_t content=cia_u64(h+24);
    if(at>(uint64_t)length || content<sizeof ncch || content>(uint64_t)length-at)goto end;
    if(fseek(f,(long)at,SEEK_SET) || fread(ncch,1,sizeof ncch,f)!=sizeof ncch)goto end;
    valid=!memcmp(ncch+0x100,"NCCH",4) && cia_u64(ncch+0x118)==CONKER_CIA_TITLE_ID;
end:fclose(f);
    return valid?1:fail(error,capacity,"Downloaded CIA is invalid or belongs to another title.");
}
