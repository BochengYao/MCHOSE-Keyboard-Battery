// No timer may authorize device I/O. Automatic reads require recent input from
// the same physical keyboard container. Raw Input headers only: no key contents.
#include <devpropdef.h>
#define REFRESH_INPUT 1
#define REFRESH_MANUAL 2
#define WM_RUNTIME_STATUS (WM_APP+4)
static volatile LONG refreshReasons=0,systemSuspended=FALSE;
static volatile LONG64 deviceWriteCalls=0,manualQueries=0,inputQueries=0,expiredInputQueries=0,timerTicks=0;
static SRWLOCK inputLock=SRWLOCK_INIT;
static ULONGLONG lastKeyboardInput=0,lastAutomaticRequest=0,lastReadingTick=0;
static GUID lastKeyboardContainer,queryContainer;
static BOOL queryExplicit=TRUE,queryHasContainer=FALSE,rawInputRegistered=FALSE;
typedef struct {HANDLE device;BOOL accepted;GUID container;} KeyboardSource;
static KeyboardSource keyboardSources[8];
static unsigned nextKeyboardSource=0;
static const DEVPROPKEY containerKey={{0x8c7ed206,0x3f8a,0x4827,{0xb3,0xab,0xae,0x9e,0x1f,0xae,0xfc,0x6c}},2};
static BOOL ContainerProperty(HDEVINFO set,SP_DEVINFO_DATA *info,GUID *out) {
    DEVPROPTYPE type=0;DWORD size=0;GUID zero={0};
    return SetupDiGetDevicePropertyW(set,info,&containerKey,&type,(BYTE*)out,sizeof(*out),&size,0) && type==DEVPROP_TYPE_GUID && size==sizeof(*out) && memcmp(out,&zero,sizeof(zero))!=0;
}
static BOOL ContainerForRawPath(const wchar_t *rawName,GUID *out) {
    wchar_t path[2048];if(wcslen(rawName)>=2048) return FALSE;wcscpy(path,rawName);
    size_t len=wcslen(path);if(len>=4 && _wcsicmp(path+len-4,L"\\KBD")==0) path[len-4]=0;
    HDEVINFO set=SetupDiCreateDeviceInfoList(NULL,NULL);if(set==INVALID_HANDLE_VALUE) return FALSE;
    SP_DEVICE_INTERFACE_DATA di={0};di.cbSize=sizeof(di);BOOL ok=FALSE;DWORD size=0;
    if(SetupDiOpenDeviceInterfaceW(set,path,0,&di)) {
        SetupDiGetDeviceInterfaceDetailW(set,&di,NULL,0,&size,NULL);
        if(size>=8 && size<=16384) {
            PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,size);
            if(detail) {detail->cbSize=sizeof(*detail);SP_DEVINFO_DATA info={0};info.cbSize=sizeof(info);if(SetupDiGetDeviceInterfaceDetailW(set,&di,detail,size,NULL,&info)) ok=ContainerProperty(set,&info,out);HeapFree(GetProcessHeap(),0,detail);}
        }
    }
    SetupDiDestroyDeviceInfoList(set);return ok;
}
static BOOL KeyboardContainer(HANDLE device,GUID *out) {
    if(!device) return FALSE;
    for(unsigned i=0;i<8;i++) if(keyboardSources[i].device==device) {if(keyboardSources[i].accepted) *out=keyboardSources[i].container;return keyboardSources[i].accepted;}
    KeyboardSource source={0};source.device=device;wchar_t path[2048]={0};UINT size=2048;
    UINT copied=GetRawInputDeviceInfoW(device,RIDI_DEVICENAME,path,&size);
    if(copied!=(UINT)-1 && copied>0 && copied<2048) {
        path[2047]=0;wchar_t upper[2048];wcscpy(upper,path);CharUpperBuffW(upper,(DWORD)wcslen(upper));
        if(wcsstr(upper,L"VID_3837") || wcsstr(upper,L"VID_41E4") || (wcsstr(upper,L"VID_3554") && wcsstr(upper,L"PID_FA09"))) source.accepted=ContainerForRawPath(path,&source.container);
    }
    keyboardSources[nextKeyboardSource++%8]=source;if(source.accepted) *out=source.container;return source.accepted;
}
static BOOL PolicyAllows(BOOL explicitRequest,BOOL suspended,BOOL hasContainer,ULONGLONG now,ULONGLONG lastInput) {
    if(suspended) return FALSE;
    return explicitRequest || (hasContainer && lastInput && now>=lastInput && now-lastInput<=2000);
}
static BOOL DeviceQueryAllowed(void) {
    AcquireSRWLockShared(&inputLock);ULONGLONG tick=lastKeyboardInput;BOOL same=queryHasContainer && memcmp(&lastKeyboardContainer,&queryContainer,sizeof(GUID))==0;ReleaseSRWLockShared(&inputLock);
    return PolicyAllows(queryExplicit,InterlockedCompareExchange(&systemSuspended,0,0)!=0,same,GetTickCount64(),tick);
}
static BOOL QueryMatchesContainer(BOOL known,const GUID *container) {
    return queryExplicit || (queryHasContainer && known && memcmp(container,&queryContainer,sizeof(GUID))==0);
}
static void RequestManualRefresh(void) {
    InterlockedOr(&refreshReasons,REFRESH_MANUAL);SetEvent(refreshEvent);lastAutomaticRequest=GetTickCount64();
}
static void ObserveKeyboardHeader(HRAWINPUT input) {
    RAWINPUTHEADER header={0};UINT size=sizeof(header);GUID container;
    if(GetRawInputData(input,RID_HEADER,&header,&size,sizeof(header))!=sizeof(header) || header.dwType!=RIM_TYPEKEYBOARD || !KeyboardContainer(header.hDevice,&container)) return;
    ULONGLONG now=GetTickCount64();AcquireSRWLockExclusive(&inputLock);lastKeyboardInput=now;lastKeyboardContainer=container;ReleaseSRWLockExclusive(&inputLock);
    if(!lastAutomaticRequest || now-lastAutomaticRequest>=30000) {lastAutomaticRequest=now;InterlockedOr(&refreshReasons,REFRESH_INPUT);SetEvent(refreshEvent);}
}
typedef struct {DWORD version;Reading reading;} SavedReading;
static void StoreLastReading(const Reading *r) {
    if(!r->ok || r->cached || r->stale) return;
    SavedReading saved={1,{0}};saved.reading=*r;HKEY key;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,paletteKey,0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL)==ERROR_SUCCESS) {RegSetValueExW(key,L"LastReading",0,REG_BINARY,(const BYTE*)&saved,sizeof(saved));RegCloseKey(key);}
}
static BOOL RestoreLastReading(Reading *r) {
    HKEY key;SavedReading saved={0};DWORD size=sizeof(saved),type=0;BOOL ok=FALSE;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,paletteKey,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS) return FALSE;
    if(RegQueryValueExW(key,L"LastReading",NULL,&type,(BYTE*)&saved,&size)==ERROR_SUCCESS && type==REG_BINARY && size==sizeof(saved) && saved.version==1 && saved.reading.ok==TRUE && saved.reading.percent>=0 && saved.reading.percent<=100 && (saved.reading.charging==TRUE || saved.reading.charging==FALSE) && saved.reading.protocol>=1 && saved.reading.protocol<=6) {
        *r=saved.reading;r->model[63]=0;r->connection[23]=0;r->stale=TRUE;ok=TRUE;
    }
    RegCloseKey(key);return ok;
}
static void WriteRuntimeStatus(void) {
    wchar_t path[MAX_PATH];_snwprintf(path,MAX_PATH,L"%lsruntime-status.txt",folder);path[MAX_PATH-1]=0;char text[1024];
    AcquireSRWLockShared(&inputLock);ULONGLONG tick=lastKeyboardInput;ReleaseSRWLockShared(&inputLock);ULONGLONG now=GetTickCount64();
    ULONGLONG age=tick && now>=tick?now-tick:0;
    int n=snprintf(text,sizeof(text),"version=1.4.0-preview.3\r\nraw_input_registered=%d\r\ndevice_write_calls=%lu\r\nmanual_queries=%lu\r\ninput_queries=%lu\r\nexpired_input_queries=%lu\r\ntimer_ticks=%lu\r\nlast_matching_input_age_ms=%lu\r\nbackground_query_enabled=0\r\n",rawInputRegistered,(unsigned long)InterlockedCompareExchange64(&deviceWriteCalls,0,0),(unsigned long)InterlockedCompareExchange64(&manualQueries,0,0),(unsigned long)InterlockedCompareExchange64(&inputQueries,0,0),(unsigned long)InterlockedCompareExchange64(&expiredInputQueries,0,0),(unsigned long)InterlockedCompareExchange64(&timerTicks,0,0),(unsigned long)(age>0xffffffff?0xffffffff:age));
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(file!=INVALID_HANDLE_VALUE) {DWORD written;if(n>0 && n<(int)sizeof(text)) WriteFile(file,text,(DWORD)n,&written,NULL);CloseHandle(file);}
}
static int RefreshPolicySelfTest(void) {
    if(PolicyAllows(FALSE,FALSE,FALSE,10000,0)) return 60;
    if(PolicyAllows(FALSE,FALSE,TRUE,10000,7000)) return 61;
    if(!PolicyAllows(FALSE,FALSE,TRUE,10000,9999)) return 62;
    if(PolicyAllows(TRUE,TRUE,TRUE,10000,9999)) return 63;
    if(!PolicyAllows(TRUE,FALSE,FALSE,10000,0)) return 64;
    if(PolicyAllows(FALSE,FALSE,TRUE,10000,10001)) return 65;
    GUID a={1,0,0,{0}},b={2,0,0,{0}};queryExplicit=FALSE;queryHasContainer=TRUE;queryContainer=a;
    if(!QueryMatchesContainer(TRUE,&a) || QueryMatchesContainer(TRUE,&b) || QueryMatchesContainer(FALSE,&a)) return 66;
    queryExplicit=TRUE;queryHasContainer=FALSE;return 0;
}
