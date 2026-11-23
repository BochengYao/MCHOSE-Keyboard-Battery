#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <wchar.h>
#include <stdio.h>

#define WM_TRAY (WM_APP+1)
#define WM_READING (WM_APP+2)
#define ID_REFRESH 1
#define ID_STARTUP 2
#define ID_HELP 3
#define ID_EXIT 4
typedef struct { BOOL ok,charging,chargingKnown,cached; int percent,protocol; wchar_t model[64],connection[24],error[96]; BYTE raw[65]; } Reading;
static HWND window;
static HANDLE stopEvent,refreshEvent,worker;
static HINSTANCE instance;
static NOTIFYICONDATAW tray;
static HICON icon;
static int iconIndex=-1;
static wchar_t exePath[MAX_PATH],folder[MAX_PATH];
static const wchar_t *runKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static Reading lastReading;
static BOOL hasReading=FALSE,lowWarned=FALSE;
static ULONGLONG lastWarning=0;
static UINT taskbarCreated;
static HWND menuWindow;
static UINT iconDpi=0;
typedef BOOL (WINAPI *SetDpiContextFn)(HANDLE);
typedef HRESULT (WINAPI *GetMonitorDpiFn)(HMONITOR,int,UINT*,UINT*);
static UINT MonitorDpi(HMONITOR monitor) {
    UINT x=96,y=96;HMODULE lib=LoadLibraryW(L"shcore.dll");
    if(lib) {GetMonitorDpiFn fn=(GetMonitorDpiFn)(void*)GetProcAddress(lib,"GetDpiForMonitor");if(fn) fn(monitor,0,&x,&y);FreeLibrary(lib);}
    return x>=96 && x<=768?x:96;
}

static BOOL Parse(const BYTE *b,DWORD count,Reading *r) {
    if(count!=65 || b[0]!=0 || b[1]!=0xaa || b[2]!=5 || b[3]!=0 || b[5]!=1 || b[6]!=32 || b[7]!=0 || b[8]!=0) return FALSE;
    if(b[4]!=((b[5]+b[6]+b[7]+b[8]+b[9])&255) || (b[9]&127)>100) return FALSE;
    ZeroMemory(r,sizeof(*r));r->ok=TRUE;r->percent=b[9]&127;r->charging=(b[9]&128)!=0;CopyMemory(r->raw,b,65);return TRUE;
}
#include "KeyboardAdapters.h"
static void Tip(const Reading *r,wchar_t *out,size_t n) {
    const wchar_t *name=r->model[0]?r->model:L"迈从键盘";
    if(!r->ok) _snwprintf(out,n,L"迈从键盘 · %ls",r->error);
    else if(r->cached) _snwprintf(out,n,L"%ls · %d%% · 蓝牙系统缓存（连接和充电状态未确认）",name,r->percent);
    else if(r->charging) _snwprintf(out,n,L"%ls · 正在充电（不显示充电百分比）",name);
    else _snwprintf(out,n,L"%ls · %d%% · %ls",name,r->percent,r->connection[0]?r->connection:L"电池供电");
    out[n-1]=0;
}
static int DisplayIcon(const Reading *r) {return !r->ok?102:r->charging?101:r->percent;}
static void WriteDiagnostic(const Reading *r,const wchar_t *name) {
    wchar_t path[MAX_PATH],tip[128],text[768],detail[320];char utf8[3072];SYSTEMTIME t;GetLocalTime(&t);Tip(r,tip,128);
    _snwprintf(path,MAX_PATH,L"%ls%ls",folder,name);path[MAX_PATH-1]=0;
    if(r->ok) _snwprintf(detail,320,L"原始上报=%d%% 充电标志=%d 充电状态已知=%d 缓存=%d 协议=%d\r\n原始应答=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",r->percent,r->charging,r->chargingKnown,r->cached,r->protocol,r->raw[1],r->raw[2],r->raw[3],r->raw[4],r->raw[5],r->raw[6],r->raw[7],r->raw[8],r->raw[9]);
    else wcscpy(detail,L"未取得有效电量，无百分比读数");
    detail[319]=0;
    _snwprintf(text,768,L"迈从键盘电量助手 1.4 · 多协议预览版\r\n%04u-%02u-%02u %02u:%02u:%02u\r\n托盘提示=%ls\r\n%ls\r\n仅在状态变化时写入此文件\r\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,tip,detail);text[767]=0;
    int len=WideCharToMultiByte(CP_UTF8,0,text,-1,utf8,sizeof(utf8),NULL,NULL);
    if(len<=0) return;
    HANDLE f=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f!=INVALID_HANDLE_VALUE) {DWORD n;WriteFile(f,"\xef\xbb\xbf",3,&n,NULL);WriteFile(f,utf8,len-1,&n,NULL);CloseHandle(f);}
}
static DWORD WINAPI Worker(void *unused) {
    HANDLE events[2]={stopEvent,refreshEvent};(void)unused;
    while(WaitForMultipleObjects(2,events,FALSE,INFINITE)==WAIT_OBJECT_0+1) {
        Reading *r=(Reading*)HeapAlloc(GetProcessHeap(),0,sizeof(Reading));if(!r) continue;
        *r=Query();
        if(WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0 || !PostMessageW(window,WM_READING,0,(LPARAM)r)) HeapFree(GetProcessHeap(),0,r);
    }
    return 0;
}
static BOOL StartupEnabled(void) {
    HKEY key;wchar_t value[MAX_PATH+4],expected[MAX_PATH+4];DWORD size=sizeof(value),type=0;BOOL enabled=FALSE;
    _snwprintf(expected,MAX_PATH+4,L"\"%ls\"",exePath);
    if(RegOpenKeyExW(HKEY_CURRENT_USER,runKey,0,KEY_QUERY_VALUE,&key)==ERROR_SUCCESS) {
        if(RegQueryValueExW(key,L"G75Battery",NULL,&type,(BYTE*)value,&size)==ERROR_SUCCESS && type==REG_SZ && size<=sizeof(value)) {value[MAX_PATH+3]=0;enabled=wcscmp(value,expected)==0;}
        RegCloseKey(key);
    }
    return enabled;
}
static void ToggleStartup(void) {
    BOOL enabled=StartupEnabled();HKEY key;LONG result;
    result=RegCreateKeyExW(HKEY_CURRENT_USER,runKey,0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL);
    if(result==ERROR_SUCCESS) {
        wchar_t value[MAX_PATH+4];_snwprintf(value,MAX_PATH+4,L"\"%ls\"",exePath);
        result=enabled?RegDeleteValueW(key,L"G75Battery"):RegSetValueExW(key,L"G75Battery",0,REG_SZ,(BYTE*)value,(DWORD)((wcslen(value)+1)*sizeof(wchar_t)));
        RegCloseKey(key);
    }
    if(result!=ERROR_SUCCESS) MessageBoxW(window,L"无法修改开机启动设置。",L"G75 电量助手",MB_OK|MB_ICONERROR);
}
static void UpdateTray(const Reading *r,BOOL force) {
    int next=DisplayIcon(r);
    BOOL changed=!hasReading || r->ok!=lastReading.ok || (r->ok && (wcscmp(r->model,lastReading.model)!=0 || wcscmp(r->connection,lastReading.connection)!=0 || r->cached!=lastReading.cached || r->charging!=lastReading.charging || (!r->charging && r->percent!=lastReading.percent))) || (!r->ok && wcscmp(r->error,lastReading.error)!=0);
    NOTIFYICONIDENTIFIER identity={0};RECT trayRect;identity.cbSize=sizeof(identity);identity.hWnd=window;identity.uID=1;
    HMONITOR monitor=MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY);
    if(SUCCEEDED(Shell_NotifyIconGetRect(&identity,&trayRect))) monitor=MonitorFromRect(&trayRect,MONITOR_DEFAULTTOPRIMARY);
    UINT dpi=MonitorDpi(monitor);
    if(!changed && !force && iconDpi==dpi) return;
    HICON previous=NULL;
    if(next!=iconIndex || !icon || iconDpi!=dpi) {
        int size=MulDiv(16,(int)dpi,96);
        HICON fresh=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(1000+next),IMAGE_ICON,size,size,LR_DEFAULTCOLOR);
        if(!fresh) return;
        previous=icon;icon=fresh;iconIndex=next;iconDpi=dpi;
    }
    tray.uFlags=NIF_ICON|NIF_TIP|NIF_SHOWTIP;tray.hIcon=icon;Tip(r,tray.szTip,128);Shell_NotifyIconW(NIM_MODIFY,&tray);
    if(previous) DestroyIcon(previous);
    if(r->ok && !r->cached && !r->charging && r->percent<=20 && (!lowWarned || GetTickCount64()-lastWarning>=3600000)) {
        tray.uFlags=NIF_INFO;wcscpy(tray.szInfoTitle,L"键盘电量偏低");_snwprintf(tray.szInfo,256,L"%ls 剩余 %d%%，可以充电了。",r->model,r->percent);tray.dwInfoFlags=NIIF_WARNING;Shell_NotifyIconW(NIM_MODIFY,&tray);lowWarned=TRUE;lastWarning=GetTickCount64();
    }
    if(r->ok && (r->charging || r->percent>=25)) lowWarned=FALSE;
    if(changed) WriteDiagnostic(r,L"最近读取.txt");
    lastReading=*r;hasReading=TRUE;
}
static void AddTray(void) {
    tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;tray.hIcon=icon;Shell_NotifyIconW(NIM_ADD,&tray);tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray);
}
#include "TrayMenu.h"
static LRESULT CALLBACK WindowProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(taskbarCreated && msg==taskbarCreated) {AddTray();if(hasReading) UpdateTray(&lastReading,TRUE);return 0;}
    switch(msg) {
    case WM_TIMER:SetEvent(refreshEvent);return 0;
    case WM_READING: {Reading *r=(Reading*)lp;UpdateTray(r,FALSE);HeapFree(GetProcessHeap(),0,r);return 0;}
    case WM_DEVICECHANGE:case WM_POWERBROADCAST: InterlockedExchange(&scanRequested,TRUE);SetEvent(refreshEvent);return DefWindowProcW(h,msg,wp,lp);
    case WM_DISPLAYCHANGE:case WM_SETTINGCHANGE:if(menuWindow) DestroyWindow(menuWindow);if(hasReading) UpdateTray(&lastReading,TRUE);return 0;
    case WM_DPICHANGED:if(menuWindow) DestroyWindow(menuWindow);if(hasReading) UpdateTray(&lastReading,TRUE);return 0;
    case WM_TRAY:
        if(LOWORD(lp)==WM_CONTEXTMENU) ShowMenu();
        else if(LOWORD(lp)==NIN_SELECT || LOWORD(lp)==NIN_KEYSELECT) {InterlockedExchange(&scanRequested,TRUE);SetEvent(refreshEvent);}
        return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case ID_REFRESH:InterlockedExchange(&scanRequested,TRUE);SetEvent(refreshEvent);break;
        case ID_STARTUP:ToggleStartup();break;
        case ID_HELP: {wchar_t path[MAX_PATH];_snwprintf(path,MAX_PATH,L"%ls使用说明.txt",folder);ShellExecuteW(h,L"open",path,NULL,NULL,SW_SHOWNORMAL);break;}
        case ID_EXIT:DestroyWindow(h);break;
        case 5:ShowMenu();break;
        }
        return 0;
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:if(menuWindow) DestroyWindow(menuWindow);KillTimer(h,1);SetEvent(stopEvent);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
static int SelfTest(void) {
    BYTE b[65]={0};Reading r;wchar_t tip[128];
    b[1]=0xaa;b[2]=5;b[4]=126;b[5]=1;b[6]=32;b[9]=93;
    if(!Parse(b,65,&r) || r.percent!=93 || r.charging || DisplayIcon(&r)!=93) return 1;
    b[9]=0;b[4]=33;if(!Parse(b,65,&r) || r.percent!=0 || DisplayIcon(&r)!=0) return 2;
    b[9]=0xe4;b[4]=5;if(!Parse(b,65,&r) || !r.charging || DisplayIcon(&r)!=101) return 3;
    Tip(&r,tip,128);if(wcsstr(tip,L"100%")) return 4;
    b[4]++;if(Parse(b,65,&r)) return 5;
    b[9]=101;b[4]=134;if(Parse(b,65,&r) || Parse(b,64,&r)) return 6;
    b[9]=20;b[4]=53;if(!Parse(b,65,&r) || DisplayIcon(&r)!=20) return 7;
    b[9]=93;b[4]=126;if(!Parse(b,65,&r) || DisplayIcon(&r)!=93) return 8;
    for(int i=0;i<=102;i++) {HICON h=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(1000+i),IMAGE_ICON,16,16,0);if(!h) return 9;DestroyIcon(h);}
    return 0;
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE previous,LPWSTR command,int show) {
    (void)previous;(void)show;instance=inst;GetModuleFileNameW(NULL,exePath,MAX_PATH);wcscpy(folder,exePath);wchar_t *slash=wcsrchr(folder,L'\\');if(slash) slash[1]=0;
    if(wcscmp(command,L"--self-test")==0) {int code=SelfTest();return code?code:AdapterSelfTest();}
    if(wcsncmp(command,L"--preview-menu ",15)==0) return PreviewMenu((UINT)_wtoi(command+15));
    stopEvent=CreateEventW(NULL,TRUE,FALSE,NULL);refreshEvent=CreateEventW(NULL,FALSE,FALSE,NULL);if(!stopEvent || !refreshEvent) return 10;
    if(wcscmp(command,L"--probe")==0 || wcscmp(command,L"--bluetooth-probe")==0) {Reading r=wcscmp(command,L"--bluetooth-probe")==0?QueryBluetooth():Query();if(!r.ok && !r.error[0]) wcscpy(r.error,L"Windows 未提供可读取的迈从蓝牙电量");WriteDiagnostic(&r,L"probe-result.txt");CloseHandle(stopEvent);CloseHandle(refreshEvent);return r.ok?0:11;}
    if(wcscmp(command,L"--inventory")==0) {WriteInventory();CloseHandle(stopEvent);CloseHandle(refreshEvent);return 0;}
    HANDLE mutex=CreateMutexW(NULL,TRUE,L"Local\\G75Battery-3837-3033");if(!mutex || GetLastError()==ERROR_ALREADY_EXISTS) {if(mutex) CloseHandle(mutex);CloseHandle(stopEvent);CloseHandle(refreshEvent);return 0;}
    SetDpiContextFn setDpi=(SetDpiContextFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext");
    if(setDpi) setDpi((HANDLE)(INT_PTR)-4);else SetProcessDPIAware();
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW cls={0};cls.lpfnWndProc=WindowProc;cls.hInstance=inst;cls.lpszClassName=L"G75BatteryNative";RegisterClassW(&cls);
    window=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"",WS_POPUP,0,0,0,0,NULL,NULL,inst,NULL);
    if(!window) {CloseHandle(mutex);CloseHandle(stopEvent);CloseHandle(refreshEvent);return 12;}
    WNDCLASSW menuClass={0};menuClass.lpfnWndProc=MenuProc;menuClass.hInstance=inst;menuClass.hCursor=LoadCursorW(NULL,IDC_ARROW);menuClass.lpszClassName=L"G75BatteryMenu";menuClass.style=CS_DROPSHADOW;RegisterClassW(&menuClass);
    ZeroMemory(&tray,sizeof(tray));tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uCallbackMessage=WM_TRAY;
    icon=(HICON)LoadImageW(inst,MAKEINTRESOURCEW(1102),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),0);iconIndex=102;wcscpy(tray.szTip,L"迈从键盘 · 正在读取");AddTray();
    worker=CreateThread(NULL,128*1024,Worker,NULL,STACK_SIZE_PARAM_IS_A_RESERVATION,NULL);
    if(!worker) {DestroyWindow(window);} else {SetTimer(window,1,30000,NULL);SetEvent(refreshEvent);}
    MSG message;while(GetMessageW(&message,NULL,0,0)>0) {TranslateMessage(&message);DispatchMessageW(&message);}
    SetEvent(stopEvent);if(worker) {WaitForSingleObject(worker,INFINITE);CloseHandle(worker);}
    while(PeekMessageW(&message,NULL,WM_READING,WM_READING,PM_REMOVE)) HeapFree(GetProcessHeap(),0,(void*)message.lParam);
    Shell_NotifyIconW(NIM_DELETE,&tray);if(icon) DestroyIcon(icon);CloseHandle(stopEvent);CloseHandle(refreshEvent);CloseHandle(mutex);return 0;
}
