// Independent implementation of read-only M HUB protocol exchanges.
#include <cfgmgr32.h>
#include <devpropdef.h>
#include "ModelTable.h"
enum { PROTO_CZ=1,PROTO_QHW,PROTO_LEGACY,PROTO_WIRED };
typedef struct { wchar_t path[2048],name[64],mode[16];int protocol;USHORT input,output;BOOL shared; } Candidate;
static Candidate candidates[16];
static int candidateCount=0,preferred=-1;
static volatile LONG scanRequested=TRUE;
static ULONGLONG lastScan=0;
static BYTE sequenceId=0;
static const wchar_t *KnownFamily(const wchar_t *name) {
    static const wchar_t *families[]={L"GX87",L"KX75",L"G75",L"G87",L"G98",L"K99",L"X75",L"UT98",L"Z75",L"K87",L"JET75",L"Jet 75"};
    for(unsigned i=0;i<sizeof(families)/sizeof(families[0]);i++) if(wcsstr(name,families[i])) return families[i];
    return NULL;
}
static BOOL HasReport(PHIDP_PREPARSED_DATA pp,const HIDP_CAPS *caps,HIDP_REPORT_TYPE type,BYTE id) {
    USHORT n=type==HidP_Input?caps->NumberInputValueCaps:caps->NumberOutputValueCaps;
    if(n>128) return FALSE;
    HIDP_VALUE_CAPS values[128];
    if(n && HidP_GetValueCaps(type,values,&n,pp)==HIDP_STATUS_SUCCESS) for(USHORT i=0;i<n;i++) if(values[i].ReportID==id) return TRUE;
    n=type==HidP_Input?caps->NumberInputButtonCaps:caps->NumberOutputButtonCaps;
    if(n>128) return FALSE;
    HIDP_BUTTON_CAPS buttons[128];
    if(n && HidP_GetButtonCaps(type,buttons,&n,pp)==HIDP_STATUS_SUCCESS) for(USHORT i=0;i<n;i++) if(buttons[i].ReportID==id) return TRUE;
    return FALSE;
}
static BOOL ModernIdentity(USHORT vid,USHORT pid,const wchar_t *product,int protocol) {
    if(vid!=0x3837) return FALSE;
    if(protocol==PROTO_CZ) {
        const wchar_t *names[]={L"MCHOSE G75 V2 2.4G",L"MCHOSE G87 V2 2.4G",L"MCHOSE K87S 2.4G",L"MCHOSE K99 V3 2.4G"};
        if(pid==0x3033) {for(int i=0;i<4;i++) if(wcscmp(product,names[i])==0) return TRUE;return FALSE;}
        return (pid==0x2021 && wcsstr(product,L"G75")) || (pid==0x2015 && wcsstr(product,L"G87")) || (pid==0x200e && wcsstr(product,L"K87")) || (pid==0x2020 && wcsstr(product,L"K99"));
    }
    if(protocol==PROTO_QHW) {
        if(pid==0x1015) return wcscmp(product,L"MCHOSE G87 V3 2.4G")==0 || wcscmp(product,L"MCHOSE G98 V3 2.4G")==0 || wcscmp(product,L"MCHOSE K87 V3 2.4G")==0;
        return (pid==0x2029 && wcsstr(product,L"G87")) || (pid==0x2025 && wcsstr(product,L"G98")) || (pid==0x2023 && wcsstr(product,L"K87"));
    }
    return FALSE;
}
static BOOL Describe(HANDLE h,Candidate *c) {
    HIDD_ATTRIBUTES a={0};a.Size=sizeof(a);wchar_t product[128]={0};PHIDP_PREPARSED_DATA pp;HIDP_CAPS caps;
    if(!HidD_GetAttributes(h,&a) || (a.VendorID!=0x3837 && a.VendorID!=0x41e4 && a.VendorID!=0x3554) || !HidD_GetProductString(h,product,sizeof(product))) return FALSE;
    if(wcsstr(product,L"Upgrader") || wcsstr(product,L"Upgrade") || wcsstr(product,L"Bootloader") || wcsstr(product,L"CH585")) return FALSE;
    if(!HidD_GetPreparsedData(h,&pp)) return FALSE;
    BOOL accepted=FALSE;
    if(HidP_GetCaps(pp,&caps)==HIDP_STATUS_SUCCESS && caps.InputReportByteLength<=1024 && caps.OutputReportByteLength<=1024) {
        if(caps.UsagePage==1 && caps.Usage==0 && caps.InputReportByteLength==65 && caps.OutputReportByteLength==65 && HasReport(pp,&caps,HidP_Input,0) && HasReport(pp,&caps,HidP_Output,0) && ModernIdentity(a.VendorID,a.ProductID,product,PROTO_CZ)) c->protocol=PROTO_CZ;
        else if(caps.UsagePage==0xff80 && caps.Usage==1 && caps.InputReportByteLength>=11 && caps.OutputReportByteLength>=9 && HasReport(pp,&caps,HidP_Input,0x4d) && HasReport(pp,&caps,HidP_Output,0x4d) && ModernIdentity(a.VendorID,a.ProductID,product,PROTO_QHW)) c->protocol=PROTO_QHW;
        else if(caps.UsagePage==0xff02 && caps.Usage==2 && caps.InputReportByteLength==20 && caps.OutputReportByteLength==20 && HasReport(pp,&caps,HidP_Input,0x13) && HasReport(pp,&caps,HidP_Output,0x13) && ((a.VendorID==0x3554 && a.ProductID==0xfa09) || (a.VendorID==0x41e4 && a.ProductID==0x2001) || (a.VendorID==0x3837 && a.ProductID==0x201b))) c->protocol=PROTO_LEGACY;
        else if(a.VendorID!=0x3554 && KnownFamily(product) && caps.UsagePage>=0xff00 && (wcsstr(product,L"MCHOSE") || wcsstr(product,L"Gaming Keyboard") || wcsstr(product,L"K99") || wcsstr(product,L"GX87")) && caps.InputReportByteLength>=9 && caps.OutputReportByteLength>=9 && HasReport(pp,&caps,HidP_Input,6) && HasReport(pp,&caps,HidP_Output,6)) c->protocol=PROTO_WIRED;
        else c->protocol=0;
        if(c->protocol) {
            c->input=caps.InputReportByteLength;c->output=caps.OutputReportByteLength;c->shared=a.VendorID==0x3554;
            wcsncpy(c->name,product,63);c->name[63]=0;
            wcscpy(c->mode,c->protocol==PROTO_LEGACY || (a.ProductID==0x3033 || a.ProductID==0x1015)?L"2.4G":L"有线");accepted=TRUE;
        }
    }
    HidD_FreePreparsedData(pp);return accepted;
}
static void ScanCandidates(void) {
    GUID guid;HidD_GetHidGuid(&guid);HDEVINFO set=SetupDiGetClassDevsW(&guid,NULL,NULL,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    candidateCount=0;preferred=-1;scanRequested=FALSE;lastScan=GetTickCount64();
    if(set==INVALID_HANDLE_VALUE) return;
    for(DWORD i=0;candidateCount<16;i++) {
        SP_DEVICE_INTERFACE_DATA di={0};di.cbSize=sizeof(di);DWORD size=0;
        if(!SetupDiEnumDeviceInterfaces(set,NULL,&guid,i,&di)) break;
        SetupDiGetDeviceInterfaceDetailW(set,&di,NULL,0,&size,NULL);if(size<8 || size>16384) continue;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,size);if(!detail) break;
        detail->cbSize=sizeof(*detail);
        if(SetupDiGetDeviceInterfaceDetailW(set,&di,detail,size,NULL,NULL) && wcslen(detail->DevicePath)<2048 && (wcsstr(detail->DevicePath,L"vid_3837") || wcsstr(detail->DevicePath,L"vid_41e4") || wcsstr(detail->DevicePath,L"vid_3554"))) {
            HANDLE h=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
            if(h!=INVALID_HANDLE_VALUE) {
                Candidate c={0};if(Describe(h,&c)) {wcscpy(c.path,detail->DevicePath);candidates[candidateCount++]=c;}CloseHandle(h);
            }
        }
        HeapFree(GetProcessHeap(),0,detail);
    }
    SetupDiDestroyDeviceInfoList(set);
    // Prefer a newly discovered USB cable interface over a receiver's old snapshot.
    for(int i=0;i<candidateCount;i++) for(int j=i+1;j<candidateCount;j++) if(wcscmp(candidates[i].mode,L"2.4G")==0 && wcscmp(candidates[j].mode,L"有线")==0) {Candidate tmp=candidates[i];candidates[i]=candidates[j];candidates[j]=tmp;}
}
static void Drain(HANDLE h,OVERLAPPED *ov) {DWORD n;CancelIoEx(h,ov);GetOverlappedResult(h,ov,&n,TRUE);}
static BOOL Await(HANDLE h,OVERLAPPED *ov,DWORD ms,DWORD *n) {
    HANDLE events[2]={stopEvent,ov->hEvent};
    if(WaitForMultipleObjects(2,events,FALSE,ms)!=WAIT_OBJECT_0+1) {Drain(h,ov);return FALSE;}
    return GetOverlappedResult(h,ov,n,FALSE);
}
static BOOL SendOnly(HANDLE h,const BYTE *data,DWORD length) {
    OVERLAPPED ov={0};DWORD n=0;ov.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!ov.hEvent) return FALSE;
    BOOL ok=WriteFile(h,data,length,&n,&ov);if(!ok && GetLastError()==ERROR_IO_PENDING) ok=Await(h,&ov,1000,&n);
    CloseHandle(ov.hEvent);return ok && n==length;
}
typedef BOOL (*MatchReply)(const BYTE*,DWORD,void*);
static BOOL Exchange(HANDLE h,const Candidate *c,const BYTE *request,MatchReply match,void *context) {
    BYTE data[1024]={0};OVERLAPPED rd={0};DWORD n=0;BOOL pending=FALSE,ok=FALSE;
    rd.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!rd.hEvent) return FALSE;
    HidD_FlushQueue(h);
    if(!ReadFile(h,data,c->input,&n,&rd)) {if(GetLastError()!=ERROR_IO_PENDING) goto done;pending=TRUE;}
    if(!SendOnly(h,request,c->output)) goto done;
    ULONGLONG until=GetTickCount64()+1200;
    for(int i=0;i<16;i++) {
        if(pending) {ULONGLONG now=GetTickCount64();if(now>=until) goto done;BOOL received=Await(h,&rd,(DWORD)(until-now),&n);pending=FALSE;if(!received) goto done;}
        if(match(data,n,context)) {ok=TRUE;goto done;}
        if(GetTickCount64()>=until) break;
        ZeroMemory(data,sizeof(data));ResetEvent(rd.hEvent);
        if(!ReadFile(h,data,c->input,&n,&rd)) {if(GetLastError()!=ERROR_IO_PENDING) break;pending=TRUE;}
    }
done:if(pending) Drain(h,&rd);CloseHandle(rd.hEvent);return ok;
}
static BOOL MatchCZ(const BYTE *data,DWORD n,void *ctx) {return Parse(data,n,(Reading*)ctx);}
static const wchar_t *PasswordName(const BYTE *bytes) {
    for(unsigned i=0;i<sizeof(legacyModels)/sizeof(legacyModels[0]);i++) if(memcmp(bytes,legacyModels[i].bytes,6)==0) return legacyModels[i].name;
    return NULL;
}
typedef struct {Reading *r;BYTE command,sequence;const wchar_t *identified;} ReplyContext;
static BOOL LegacyValid(const BYTE *b,DWORD n,BYTE command) {
    if(n!=20 || b[0]!=0x13 || b[1]!=command || b[2]!=1 || b[3]!=0) return FALSE;
    unsigned sum=0;for(int i=0;i<19;i++) sum+=b[i];return (sum&255)==b[19];
}
static BOOL MatchLegacySession(const BYTE *b,DWORD n,void *ctx) {
    ReplyContext *context=ctx;if(!LegacyValid(b,n,5) || b[4]<6 || b[4]>14) return FALSE;
    context->identified=PasswordName(b+5);return context->identified!=NULL;
}
static BOOL MatchLegacyBattery(const BYTE *b,DWORD n,void *ctx) {
    if(!LegacyValid(b,n,0x4a) || b[4]!=2 || b[5]>100) return FALSE;
    Reading *r=ctx;ZeroMemory(r,sizeof(*r));r->ok=TRUE;r->percent=b[5];r->charging=(b[6]&0x10)!=0;CopyMemory(r->raw,b,n);return TRUE;
}
static BOOL MatchWired(const BYTE *b,DWORD n,void *ctx) {
    if(n<9 || b[0]!=6 || b[1]!=0x87 || b[5]!=2 || b[6]!=0 || b[7]>100) return FALSE;
    Reading *r=ctx;ZeroMemory(r,sizeof(*r));r->ok=TRUE;r->percent=b[7];r->charging=(b[8]&15)!=0;CopyMemory(r->raw,b,n<65?n:65);return TRUE;
}
static BOOL MatchQHW(const BYTE *b,DWORD n,void *ctx) {
    ReplyContext *q=ctx;
    if(n<11 || b[0]!=0x4d || b[1]!=1 || b[2]!=3 || b[3]!=2 || b[4]!=4 || b[5]!=9 || b[6]!=0 || b[7]!=q->sequence || b[8]>2 || b[9]>100) return FALSE;
    // Official QhwCmdQueueItem excludes version (and report ID) from XOR.
    BYTE check=0;for(int i=2;i<10;i++) check^=b[i];if(check!=b[10]) return FALSE;
    Reading *r=q->r;ZeroMemory(r,sizeof(*r));r->ok=TRUE;r->percent=b[9];r->charging=b[8]==1;CopyMemory(r->raw,b,n<65?n:65);return TRUE;
}
static Reading QueryCandidate(const Candidate *c) {
    Reading r={0};BYTE request[1024]={0};
    HANDLE h=CreateFileW(c->path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
    if(h==INVALID_HANDLE_VALUE) return r;
    Candidate verify={0};if(!Describe(h,&verify) || verify.protocol!=c->protocol || verify.input!=c->input || verify.output!=c->output || wcscmp(verify.name,c->name)!=0) goto done;
    if(c->protocol==PROTO_CZ) {request[1]=0x55;request[2]=5;request[4]=33;request[5]=1;request[6]=32;Exchange(h,c,request,MatchCZ,&r);}
    else if(c->protocol==PROTO_QHW) {
        request[0]=0x4d;request[1]=0xaa;request[2]=0x55;request[3]=0xaa;request[4]=0x55;request[5]=0xaa;request[6]=0x55;
        if(!SendOnly(h,request,c->output)) goto done;
        ZeroMemory(request,sizeof(request));ReplyContext q={0};q.r=&r;q.sequence=++sequenceId;
        request[0]=0x4d;request[1]=1;request[2]=3;request[4]=4;request[5]=9;request[7]=q.sequence;request[8]=3^4^9^q.sequence;
        Exchange(h,c,request,MatchQHW,&q);
    } else if(c->protocol==PROTO_LEGACY) {
        ReplyContext q={0};request[0]=0x13;request[1]=5;request[2]=1;request[19]=0x19;
        if(!Exchange(h,c,request,MatchLegacySession,&q)) goto done;
        request[1]=0x4a;request[19]=0x5e;
        if(Exchange(h,c,request,MatchLegacyBattery,&r) && q.identified) wcsncpy(r.model,q.identified,63);
    } else if(c->protocol==PROTO_WIRED) {request[0]=6;request[1]=0x87;request[4]=1;request[6]=2;Exchange(h,c,request,MatchWired,&r);}
    if(r.ok) {
        if(!r.model[0]) {const wchar_t *name=c->name;if(wcsncmp(name,L"MCHOSE ",7)==0) name+=7;wcsncpy(r.model,name,63);r.model[63]=0;}
        wcscpy(r.connection,c->mode);r.chargingKnown=TRUE;r.protocol=c->protocol;
    }
done:CloseHandle(h);return r;
}
#include "BluetoothCache.h"
// Windows' Bluetooth battery cache: read only. The key is not a documented device API.
// It is deliberately identified as cached data with unconfirmed connection/charging state.
static Reading QueryBluetooth(void) {
    const DEVPROPKEY batteryKey={{0x104ea319,0x6ee2,0x4701,{0xbd,0x47,0x8d,0xdb,0xf4,0x25,0xbb,0xe5}},2};
    Reading r={0};HDEVINFO set=SetupDiGetClassDevsW(NULL,NULL,NULL,DIGCF_ALLCLASSES|DIGCF_PRESENT);if(set==INVALID_HANDLE_VALUE) return r;
    for(DWORD i=0;i<2048;i++) {
        SP_DEVINFO_DATA di={0};di.cbSize=sizeof(di);wchar_t id[512]={0},name[128]={0};DWORD type=0;
        if(!SetupDiEnumDeviceInfo(set,i,&di)) break;
        if(!SetupDiGetDeviceInstanceIdW(set,&di,id,512,NULL) || (wcsncmp(id,L"BTHLE\\",6)!=0 && wcsncmp(id,L"BTHENUM\\",8)!=0)) continue;
        if(!SetupDiGetDeviceRegistryPropertyW(set,&di,SPDRP_FRIENDLYNAME,&type,(BYTE*)name,sizeof(name),NULL)) SetupDiGetDeviceRegistryPropertyW(set,&di,SPDRP_DEVICEDESC,&type,(BYTE*)name,sizeof(name),NULL);
        if(wcsncmp(name,L"MCHOSE ",7)!=0 || !KnownFamily(name)) continue;
        ULONG status=0,problem=0;if(CM_Get_DevNode_Status(&status,&problem,di.DevInst,0)!=CR_SUCCESS || problem || (status&0x02000000)) continue;
        BYTE value=0;DEVPROPTYPE ptype=0;DWORD size=0;
        if(SetupDiGetDevicePropertyW(set,&di,&batteryKey,&ptype,&value,1,&size,0) && ptype==DEVPROP_TYPE_BYTE && size==1 && value<=100) {
            r.ok=TRUE;r.percent=value;r.cached=TRUE;r.chargingKnown=FALSE;r.protocol=5;wcsncpy(r.model,name+7,63);wcscpy(r.connection,L"蓝牙系统缓存");break;
        }
    }
    SetupDiDestroyDeviceInfoList(set);if(!r.ok) r=QueryGattCache();return r;
}
static Reading Query(void) {
    if(scanRequested || GetTickCount64()-lastScan>300000) ScanCandidates();
    if(preferred>=0 && preferred<candidateCount) {Reading r=QueryCandidate(&candidates[preferred]);if(r.ok) return r;}
    for(int i=0;i<candidateCount;i++) {
        if(i==preferred || WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0) continue;
        Reading r=QueryCandidate(&candidates[i]);if(r.ok) {preferred=i;return r;}
    }
    preferred=-1;
    Reading r=QueryBluetooth();if(r.ok) return r;
    wcscpy(r.error,candidateCount?L"未取得电量，请唤醒键盘或关闭 Hub":L"未发现可读取电量的迈从键盘");return r;
}
static void WriteInventory(void) {
    ScanCandidates();wchar_t path[MAX_PATH];_snwprintf(path,MAX_PATH,L"%lsdevice-inventory.txt",folder);path[MAX_PATH-1]=0;
    HANDLE f=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(f==INVALID_HANDLE_VALUE) return;
    DWORD written;const char *heading="Recognized MCHOSE keyboard HID interfaces (no commands sent; no serials or device paths exported)\r\n";WriteFile(f,heading,(DWORD)strlen(heading),&written,NULL);
    for(int i=0;i<candidateCount;i++) {
        wchar_t row[256];char utf8[1024];Candidate *c=&candidates[i];_snwprintf(row,256,L"%ls | transport=%ls | protocol=%d | input=%u output=%u\r\n",c->name,c->mode,c->protocol,c->input,c->output);row[255]=0;
        int n=WideCharToMultiByte(CP_UTF8,0,row,-1,utf8,sizeof(utf8),NULL,NULL);if(n>0) WriteFile(f,utf8,n-1,&written,NULL);
    }
    CloseHandle(f);
}
static int AdapterSelfTest(void) {
    if(!ModernIdentity(0x3837,0x3033,L"MCHOSE G75 V2 2.4G",PROTO_CZ) || !ModernIdentity(0x3837,0x3033,L"MCHOSE K99 V3 2.4G",PROTO_CZ) || ModernIdentity(0x3837,0x3033,L"MCHOSE Mouse",PROTO_CZ) || ModernIdentity(0x3554,0xfa09,L"AULA",PROTO_CZ)) return 30;
    if(!ModernIdentity(0x3837,0x2021,L"MCHOSE G75 V2 (8K)",PROTO_CZ) || !ModernIdentity(0x3837,0x1015,L"MCHOSE G98 V3 2.4G",PROTO_QHW) || ModernIdentity(0x3837,0x1017,L"MCHOSE G98 V3 Upgrader",PROTO_QHW)) return 31;
    BYTE frame[65]={0};Reading r;ReplyContext q={0};q.r=&r;q.sequence=7;
    frame[0]=0x4d;frame[1]=1;frame[2]=3;frame[3]=2;frame[4]=4;frame[5]=9;frame[7]=7;frame[8]=1;frame[9]=80;for(int i=2;i<10;i++) frame[10]^=frame[i];
    if(!MatchQHW(frame,32,&q) || r.percent!=80 || !r.charging) return 32;
    q.sequence=8;if(MatchQHW(frame,32,&q)) return 33;q.sequence=7;frame[10]++;if(MatchQHW(frame,32,&q)) return 34;
    ZeroMemory(frame,sizeof(frame));frame[0]=0x13;frame[1]=5;frame[2]=1;frame[4]=10;CopyMemory(frame+5,legacyModels[7].bytes,6);for(int i=0;i<19;i++) frame[19]+=frame[i];
    if(!MatchLegacySession(frame,20,&q) || !q.identified || wcscmp(q.identified,L"G75 Pro")!=0) return 35;
    frame[10]=0xff;frame[19]=0;for(int i=0;i<19;i++) frame[19]+=frame[i];if(MatchLegacySession(frame,20,&q)) return 36;
    ZeroMemory(frame,sizeof(frame));frame[0]=0x13;frame[1]=0x4a;frame[2]=1;frame[4]=2;frame[5]=36;frame[6]=1;for(int i=0;i<19;i++) frame[19]+=frame[i];
    if(!MatchLegacyBattery(frame,20,&r) || r.percent!=36 || r.charging) return 37;
    frame[1]=0x0a;if(MatchLegacyBattery(frame,20,&r)) return 38;
    ZeroMemory(frame,sizeof(frame));frame[0]=6;frame[1]=0x87;frame[5]=2;frame[7]=42;frame[8]=0x10;
    if(!MatchWired(frame,65,&r) || r.percent!=42 || r.charging) return 39;
    frame[5]=0;if(MatchWired(frame,65,&r)) return 40;
    if(!KnownFamily(L"MCHOSE G75 5.0") || KnownFamily(L"MCHOSE A7 Mouse")) return 41;
    if(sizeof(LEUuid)!=20 || sizeof(LEService)!=24 || sizeof(LECharacteristic)!=36) return 42;
    LEUuid uuid={0};uuid.shortUuid=TRUE;uuid.value.shortValue=0x180f;if(!UuidIs(&uuid,0x180f) || UuidIs(&uuid,0x1812)) return 43;
    return 0;
}
