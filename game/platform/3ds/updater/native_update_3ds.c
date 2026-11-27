/* Copyright (c) 2026 Raihwy3. MIT. Runs before game/audio/render workers. */
#include <3ds.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "native_update.h"
#include "conker_launcher.h"
#include "native_update_http.h"
#include "native_update_ca.h"
static PrintConsole *top_screen,*bottom_screen;
static int cia_mode;
static int texpack_mode;
static struct {
    int download,cancel,done,ok;
    unsigned received,total;
    char metadata[65537],error[160],file[640];
    struct NativeUpdateRelease release;
} job;
static int cancelled(void *p){(void)p;return __atomic_load_n(&job.cancel,__ATOMIC_RELAXED);}
static void received(void *p,size_t n,size_t total){(void)p;__atomic_store_n(&job.received,(unsigned)n,__ATOMIC_RELAXED);__atomic_store_n(&job.total,(unsigned)total,__ATOMIC_RELAXED);}
static void worker(void *p){
    (void)p;void *buffer=memalign(4096,256*1024);
    if(buffer){
        if(R_SUCCEEDED(socInit(buffer,256*1024))){
            struct NativeUpdateTransfer t={0};t.cancel=cancelled;t.progress=received;
            const char *url=texpack_mode?NATIVE_TEXPACK_API:NATIVE_UPDATE_API;
            if(job.download){url=job.release.url;t.file=job.file;t.expected=job.release.bytes;t.digest=job.release.digest;}
            else {t.memory=job.metadata;t.capacity=sizeof job.metadata;}
            job.ok=native_update_http(url,&t,native_update_ca,sizeof native_update_ca-1,job.error,sizeof job.error);
            if(job.ok && !job.download){
                job.ok=texpack_mode?native_update_texpack_release(job.metadata,t.length,&job.release)
                                    :native_update_release_for(job.metadata,t.length,NATIVE_RELEASE_TAG,cia_mode,&job.release);
            }
            socExit();
        }
        free(buffer);
    }
    __atomic_store_n(&job.done,1,__ATOMIC_RELEASE);
}
static void draw(const char *stage,const char *message,const char *actions,unsigned n,unsigned total){
    struct ConkerLauncherView view={0};
    view.stage=stage;view.message=message;view.actions=actions;view.done=n;view.total=total;
    char detail[96];
    if(job.release.tag[0]){snprintf(detail,sizeof detail,"AVAILABLE  %s",job.release.tag);view.detail=detail;}
    if(conker_launcher_draw(&view))return;
    consoleSelect(top_screen);consoleClear();
    printf("\x1b[3;4HConker: 3DS Recomp\x1b[7;4H%s\x1b[11;4H%s",stage,message);
    if(total)printf("\x1b[17;4H%u%%",(unsigned)((unsigned long long)n*100/total));
    consoleSelect(bottom_screen);consoleClear();
    printf("\x1b[3;3HLAUNCHER UPDATE\x1b[7;3HInstalled: %s",NATIVE_RELEASE_TAG);
    if(job.release.tag[0])printf("\x1b[11;3HAvailable: %s",job.release.tag);
    printf("\x1b[21;3H%s",actions);
    gfxFlushBuffers();gfxSwapBuffers();
}
static int buttons(void){
    hidScanInput();u32 k=hidKeysDown();
    if(k&KEY_TOUCH){touchPosition p;hidTouchRead(&p);if(p.py>=150 && p.py<=190)k|=p.px<160?KEY_A:KEY_B;}
    return k&KEY_B?-1:k&KEY_A?1:0;
}
static int prompt(const char *stage,const char *message,const char *actions){
    draw(stage,message,actions,0,0);
    // Opening presses must be released before choosing another action.
    while(aptMainLoop()){hidScanInput();if(!(hidKeysHeld()&(KEY_A|KEY_B|KEY_TOUCH)))break;gspWaitForVBlank();}
    while(aptMainLoop()){int k=buttons();if(k)return k;gspWaitForVBlank();}return 0;
}
static int transfer(int download){
    job.download=download;job.cancel=job.done=job.ok=0;job.received=job.total=0;job.error[0]=0;
    Thread thread=threadCreate(worker,NULL,128*1024,0x3f,0,false);if(!thread)return 0;
    u64 started=osGetTime(),paint=0;int skip=0;
    while(!__atomic_load_n(&job.done,__ATOMIC_ACQUIRE)){
        if(!aptMainLoop() || buttons()<0 || (!download && osGetTime()-started>4500)){skip=1;__atomic_store_n(&job.cancel,1,__ATOMIC_RELAXED);}
        if(osGetTime()>=paint){
            const char *st=texpack_mode?(download?"DOWNLOADING HD TEXTURES":"CHECKING HD TEXTURES"):download?"DOWNLOADING UPDATE":"CHECKING FOR UPDATES";
            const char *msg=skip?"Cancelling...":texpack_mode?(download?"Saving texpack.bin...":"Checking latest texture pack..."):download?"Saving the new launcher...":"Checking GitHub releases...";
            draw(st,msg,download?"B: Cancel":"B: Skip",__atomic_load_n(&job.received,__ATOMIC_RELAXED),__atomic_load_n(&job.total,__ATOMIC_RELAXED));
            paint=osGetTime()+200;
        }
        gspWaitForVBlank();
    }
    // Never reuse the transfer buffers or shut down SOC while its worker is alive.
    threadJoin(thread,U64_MAX);threadFree(thread);return !skip && job.ok;
}
static int stage_progress(size_t n,size_t total){
    static u64 paint;
    if(!aptMainLoop() || buttons()<0)return 0;
    if(osGetTime()>=paint){draw("INSTALLING UPDATE","Checking the launcher files...","B: Cancel",(unsigned)n,(unsigned)total);paint=osGetTime()+100;}
    return 1;
}
// Only the installed title has our title ID; any .3dsx launcher (hbmenu or others) runs under another ID.
static int running_as_cia(void){
    u64 id=0;
    if(R_SUCCEEDED(aptInit())){APT_GetProgramID(&id);aptExit();}
    return id==CONKER_CIA_TITLE_ID;
}
int conker_update_startup(PrintConsole *top,PrintConsole *bottom){
    const char *directory="sdmc:/3ds/ConkerRecomp";
    top_screen=top;bottom_screen=bottom;cia_mode=running_as_cia();char error[160]={0},path[640];
    if(!native_update_recover(directory,error,sizeof error))return prompt("UPDATE RECOVERY",error,"A: Continue  B: Exit")>0;
    // A repository must be explicitly configured by the maintainer.
    if(!NATIVE_UPDATE_REPOSITORY[0])return 1;
    snprintf(path,sizeof path,"%s/no-update-check.flag",directory);if(!access(path,F_OK))return 1;
    u32 wifi=0;if(R_FAILED(acInit()))return 1;Result rc=ACU_GetWifiStatus(&wifi);acExit();if(R_FAILED(rc)||!wifi)return 1;
    if(!transfer(0))return aptMainLoop();
    int choice=prompt("NEW RELEASE AVAILABLE",cia_mode?"Download the CIA update? Install it with FBI after closing the game.":"Install this launcher update?\n\n   ROMs, saves and settings are kept.",cia_mode?"A: Download  B: Not now":"A: Install  B: Not now");
    if(choice<=0)return choice<0;
    snprintf(job.file,sizeof job.file,"%s/launcher-update.%s.part",directory,cia_mode?"cia":"zip");
    if(!transfer(1))return aptMainLoop() && prompt("UPDATE NOT INSTALLED","The current launcher is unchanged.","A: Play  B: Exit")>0;
    if(cia_mode){
        if(!native_update_validate_cia(job.file,error,sizeof error)){
            remove(job.file);return prompt("UPDATE NOT SAVED",error,"A: Play  B: Exit")>0;
        }
        snprintf(path,sizeof path,"%s/launcher-update.cia",directory);
        // Replace any older download only after staging and verification succeeded; 3DS rename fails if destination exists.
        remove(path);
        if(rename(job.file,path)){remove(job.file);return prompt("UPDATE NOT SAVED","Unable to save the CIA on the SD card.","A: Play  B: Exit")>0;}
        return prompt("CIA UPDATE READY","Close the game. In FBI, install /3ds/ConkerRecomp/launcher-update.cia. Saves and settings stay in place.","A: Exit  B: Play")<0;
    }
    snprintf(path,sizeof path,"%s/launcher-update.zip",directory);remove(path);
    if(rename(job.file,path) || !native_update_install(path,directory,stage_progress,error,sizeof error))return prompt("UPDATE NOT INSTALLED",error[0]?error:"Unable to save update.","A: Play  B: Exit")>0;
    return prompt("UPDATE INSTALLED","Relaunch to use the new version.","A: Exit  B: Play")<0;
}
int conker_download_texpack(PrintConsole *top,PrintConsole *bottom){
    const char *directory="sdmc:/3ds/ConkerRecomp";
    top_screen=top;bottom_screen=bottom;
    u32 wifi=0;if(R_FAILED(acInit()))return 0;Result rc=ACU_GetWifiStatus(&wifi);acExit();
    if(R_FAILED(rc)||!wifi){prompt("NO WI-FI","Connect to Wi-Fi to download HD textures.","A: Back  B: Back");return 0;}
    memset(&job.release,0,sizeof job.release);
    texpack_mode=1;
    int found=transfer(0);
    if(!found){texpack_mode=0;prompt("NO TEXTURE PACK",job.error[0]?job.error:"Could not find latest HD texture pack release.","A: Back  B: Back");return 0;}
    char msg[128];
    unsigned mb=(unsigned)((job.release.bytes+(512u*1024u))/(1024u*1024u));
    snprintf(msg,sizeof msg,"Download 2x HD texture pack (~%u MB) to your SD card?",mb);
    if(prompt("HD TEXTURES",msg,"A: Download  B: Cancel")<=0){texpack_mode=0;return 0;}
    snprintf(job.file,sizeof job.file,"%s/texpack.bin.part",directory);
    int ok=transfer(1);
    texpack_mode=0;
    if(!ok){remove(job.file);prompt("DOWNLOAD FAILED",job.error[0]?job.error:"Could not download HD textures.","A: Back  B: Back");return 0;}
    char dest[640];snprintf(dest,sizeof dest,"%s/texpack.bin",directory);remove(dest);
    if(rename(job.file,dest)){remove(job.file);prompt("SAVE FAILED","Unable to save texpack.bin to SD card.","A: Back  B: Back");return 0;}
    prompt("HD TEXTURES READY","Downloaded texpack.bin! Toggle HD textures in Display settings.","A: Continue  B: Continue");
    return 1;
}
