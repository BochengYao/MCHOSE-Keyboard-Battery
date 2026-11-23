// Tray palette uses small per-user settings and GDI tinting only on icon changes.
enum { COLOR_NORMAL=0,COLOR_LOW,COLOR_CHARGING,COLOR_COUNT };
static const COLORREF defaultColors[COLOR_COUNT]={RGB(40,222,98),RGB(255,80,80),RGB(70,174,255)};
static COLORREF trayColors[COLOR_COUNT]={RGB(40,222,98),RGB(255,80,80),RGB(70,174,255)};
static const wchar_t *paletteKey=L"Software\\BochengYao\\MCHOSEKeyboardBattery";
static HWND colorWindow;
static void UpdateTray(const Reading *r,BOOL force);
static void ShowColorSettings(void);
typedef struct {DWORD version;COLORREF colors[COLOR_COUNT];} SavedPalette;
static BOOL ValidPalette(const SavedPalette *p) {
    if(p->version!=1) return FALSE;
    for(int i=0;i<COLOR_COUNT;i++) if(p->colors[i]>0xffffff) return FALSE;
    return TRUE;
}
static void LoadColorSettings(void) {
    HKEY key;SavedPalette data={0};DWORD size=sizeof(data),type=0;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,paletteKey,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS) return;
    if(RegQueryValueExW(key,L"TrayColors",NULL,&type,(BYTE*)&data,&size)==ERROR_SUCCESS && type==REG_BINARY && size==sizeof(data) && ValidPalette(&data)) CopyMemory(trayColors,data.colors,sizeof(trayColors));
    RegCloseKey(key);
}
static BOOL SaveColorSettings(const COLORREF *colors) {
    HKEY key;SavedPalette data={1,{0}};CopyMemory(data.colors,colors,sizeof(data.colors));
    if(!ValidPalette(&data) || RegCreateKeyExW(HKEY_CURRENT_USER,paletteKey,0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL)!=ERROR_SUCCESS) return FALSE;
    LONG result=RegSetValueExW(key,L"TrayColors",0,REG_BINARY,(const BYTE*)&data,sizeof(data));RegCloseKey(key);return result==ERROR_SUCCESS;
}
static COLORREF StatusColor(const Reading *r) {return !r->ok?RGB(192,192,192):trayColors[r->charging?COLOR_CHARGING:r->percent<=20?COLOR_LOW:COLOR_NORMAL];}
static int IconColorRole(int index) {return index==101?COLOR_CHARGING:index<=20?COLOR_LOW:COLOR_NORMAL;}
static HICON TintIcon(HICON source,COLORREF color) {
    ICONINFO info={0};BITMAP bitmap={0};HICON tinted=NULL;HDC dc=NULL;HBITMAP dib=NULL;void *pixels=NULL;
    if(!GetIconInfo(source,&info)) return NULL;
    if(!info.hbmColor || !GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap) || bitmap.bmWidth<=0 || bitmap.bmHeight<=0 || bitmap.bmWidth>256 || bitmap.bmHeight>256) goto done;
    BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=bitmap.bmWidth;bi.bmiHeader.biHeight=-bitmap.bmHeight;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    dc=GetDC(NULL);dib=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&pixels,NULL,0);if(!dib || !pixels) goto done;
    if(GetDIBits(dc,info.hbmColor,0,(UINT)bitmap.bmHeight,pixels,&bi,DIB_RGB_COLORS)!=bitmap.bmHeight) goto done;
    BYTE *p=pixels;
    for(LONG i=0;i<bitmap.bmWidth*bitmap.bmHeight;i++,p+=4) {
        unsigned alpha=p[3];
        // HICON color planes use premultiplied BGRA. Preserve the glyph's alpha.
        p[0]=(BYTE)((GetBValue(color)*alpha+127)/255);p[1]=(BYTE)((GetGValue(color)*alpha+127)/255);p[2]=(BYTE)((GetRValue(color)*alpha+127)/255);
    }
    ICONINFO output=info;output.hbmColor=dib;tinted=CreateIconIndirect(&output);
done:
    if(dib) DeleteObject(dib);
    if(dc) ReleaseDC(NULL,dc);
    if(info.hbmColor) DeleteObject(info.hbmColor);
    if(info.hbmMask) DeleteObject(info.hbmMask);
    return tinted;
}
static HICON LoadTrayIcon(int index,int size) {
    HICON source=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(1000+index),IMAGE_ICON,size,size,LR_DEFAULTCOLOR);
    if(!source || index==102) return source;
    int role=IconColorRole(index);
    if(trayColors[role]==defaultColors[role]) return source;
    HICON tinted=TintIcon(source,trayColors[role]);
    if(tinted) {DestroyIcon(source);return tinted;}
    return source;
}
static int ColorSelfTest(void) {
    SavedPalette p={1,{RGB(12,34,56),RGB(255,0,255),RGB(0,0,0)}};
    if(!ValidPalette(&p)) return 50;
    p.version=2;if(ValidPalette(&p)) return 51;
    p.version=1;p.colors[0]=0x1000000;if(ValidPalette(&p)) return 52;
    for(int size=16;size<=48;size+=8) {
        HICON source=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(1089),IMAGE_ICON,size,size,0);if(!source) return 53;
        HICON tinted=TintIcon(source,RGB(13,77,219));DestroyIcon(source);if(!tinted) return 54;
        ICONINFO info={0};GetIconInfo(tinted,&info);BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=size;bi.bmiHeader.biHeight=-size;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
        BYTE data[48*48*4];HDC dc=GetDC(NULL);int count=GetDIBits(dc,info.hbmColor,0,(UINT)size,data,&bi,DIB_RGB_COLORS);ReleaseDC(NULL,dc);BOOL colored=FALSE,transparent=FALSE;
        if(count==size) for(int i=0;i<size*size;i++) {BYTE *v=data+i*4;if(v[3]>0) {if(v[0]!=(219*v[3]+127)/255 || v[1]!=(77*v[3]+127)/255 || v[2]!=(13*v[3]+127)/255) {count=0;break;}colored=TRUE;}else transparent=TRUE;}
        DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);DestroyIcon(tinted);if(!count || !colored || !transparent) return 55;
    }
    return 0;
}
