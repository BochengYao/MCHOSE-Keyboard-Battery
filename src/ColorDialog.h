// One dark Win32/GDI picker. No external dialog, UI framework, or background renderer.
#define COLOR_BUTTON_BASE 200
#define COLOR_RESET 210
#define COLOR_SAVE 211
#define COLOR_CANCEL 212
#define COLOR_HEX 300
#define COLOR_RED 301
#define COLOR_GREEN 302
#define COLOR_BLUE 303
static COLORREF draftColors[COLOR_COUNT];
static UINT colorDpi=96;
static HFONT colorFont,colorTitleFont;
static HBRUSH colorEditBrush;
static HBITMAP colorSV,colorHue;
static HICON colorPreviews[COLOR_COUNT];
static COLORREF previewColors[COLOR_COUNT];
static int selectedColor=0,colorDrag=0;
static float colorH=0,colorS=0,colorV=0;
static BOOL colorGradientDirty=TRUE,colorSyncing=FALSE;
static const wchar_t *colorError;
static const wchar_t *colorLabels[COLOR_COUNT]={L"正常电量",L"低电量",L"正在充电"};
static int CPx(int n) {return MulDiv(n,(int)colorDpi,96);}
static float Clamp01(float v) {return v<0?0:v>1?1:v;}
static COLORREF HSVColor(float h,float s,float v) {
    while(h>=360) h-=360;
    while(h<0) h+=360;
    s=Clamp01(s);v=Clamp01(v);
    float section=h/60.0f;int part=(int)section;float f=section-part;
    float p=v*(1-s),q=v*(1-s*f),t=v*(1-s*(1-f)),r=0,g=0,b=0;
    switch(part) {case 0:r=v;g=t;b=p;break;case 1:r=q;g=v;b=p;break;case 2:r=p;g=v;b=t;break;case 3:r=p;g=q;b=v;break;case 4:r=t;g=p;b=v;break;default:r=v;g=p;b=q;break;}
    return RGB((BYTE)(r*255+0.5f),(BYTE)(g*255+0.5f),(BYTE)(b*255+0.5f));
}
static void RGBToPicker(COLORREF color) {
    float r=GetRValue(color)/255.0f,g=GetGValue(color)/255.0f,b=GetBValue(color)/255.0f;
    float max=r>g?r:g;if(b>max) max=b;float min=r<g?r:g;if(b<min) min=b;float d=max-min;
    colorV=max;colorS=max>0?d/max:0;
    if(d>0) {if(max==r) colorH=60*((g-b)/d);else if(max==g) colorH=60*(2+(b-r)/d);else colorH=60*(4+(r-g)/d);if(colorH<0) colorH+=360;}
    colorGradientDirty=TRUE;
}
static void ColorText(HDC dc,const wchar_t *text,int x,int y,int right,int height,HFONT font,COLORREF color) {
    RECT r={CPx(x),CPx(y),CPx(right),CPx(y+height)};HGDIOBJ previous=SelectObject(dc,font);SetTextColor(dc,color);DrawTextW(dc,text,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(dc,previous);
}
static void SyncColorInputs(int except) {
    colorSyncing=TRUE;COLORREF color=draftColors[selectedColor];wchar_t text[16];
    if(except!=COLOR_HEX) {_snwprintf(text,16,L"#%02X%02X%02X",GetRValue(color),GetGValue(color),GetBValue(color));SetWindowTextW(GetDlgItem(colorWindow,COLOR_HEX),text);}
    unsigned values[3]={GetRValue(color),GetGValue(color),GetBValue(color)};
    for(int i=0;i<3;i++) if(except!=COLOR_RED+i) {_snwprintf(text,16,L"%u",values[i]);SetWindowTextW(GetDlgItem(colorWindow,COLOR_RED+i),text);}
    colorSyncing=FALSE;
}
static void RefreshColorView(int except) {
    colorError=NULL;SyncColorInputs(except);EnableWindow(GetDlgItem(colorWindow,COLOR_SAVE),TRUE);
    InvalidateRect(colorWindow,NULL,FALSE);
    for(int i=0;i<COLOR_COUNT;i++) InvalidateRect(GetDlgItem(colorWindow,COLOR_BUTTON_BASE+i),NULL,FALSE);
}
static BOOL ReadColorInputs(int id) {
    COLORREF color=0;wchar_t text[16];
    if(id==COLOR_HEX) {
        GetWindowTextW(GetDlgItem(colorWindow,id),text,16);const wchar_t *p=text;if(*p==L'#') p++;
        if(wcslen(p)!=6) return FALSE;
        unsigned value=0;
        for(int i=0;i<6;i++) {wchar_t ch=p[i];int n=ch>=L'0'&&ch<=L'9'?ch-L'0':ch>=L'A'&&ch<=L'F'?ch-L'A'+10:ch>=L'a'&&ch<=L'f'?ch-L'a'+10:-1;if(n<0) return FALSE;value=(value<<4)|(unsigned)n;}
        color=RGB((value>>16)&255,(value>>8)&255,value&255);
    } else {
        unsigned values[3];
        for(int i=0;i<3;i++) {
            GetWindowTextW(GetDlgItem(colorWindow,COLOR_RED+i),text,16);size_t len=wcslen(text);if(len<1 || len>3) return FALSE;
            unsigned value=0;for(size_t j=0;j<len;j++) {if(text[j]<L'0'||text[j]>L'9') return FALSE;value=value*10+(text[j]-L'0');}if(value>255) return FALSE;values[i]=value;
        }
        color=RGB(values[0],values[1],values[2]);
    }
    draftColors[selectedColor]=color;RGBToPicker(color);RefreshColorView(id);return TRUE;
}
static void ReleaseColorBitmaps(void) {if(colorSV) DeleteObject(colorSV);if(colorHue) DeleteObject(colorHue);colorSV=colorHue=NULL;}
static void MakeColorBitmaps(HDC dc) {
    if(!colorGradientDirty && colorSV && colorHue) return;
    ReleaseColorBitmaps();int width=CPx(354),height=CPx(216);BITMAPINFO bi={0};void *pixels;
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=width;bi.bmiHeader.biHeight=-height;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    colorSV=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&pixels,NULL,0);
    if(colorSV) {
        DWORD *p=pixels;COLORREF base=HSVColor(colorH,1,1);
        for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
            unsigned r=(unsigned)((255*(width-1)+(GetRValue(base)-255)*x)/(width-1))*(height-1-y)/(height-1);
            unsigned g=(unsigned)((255*(width-1)+(GetGValue(base)-255)*x)/(width-1))*(height-1-y)/(height-1);
            unsigned b=(unsigned)((255*(width-1)+(GetBValue(base)-255)*x)/(width-1))*(height-1-y)/(height-1);
            *p++=(r<<16)|(g<<8)|b;
        }
    }
    bi.bmiHeader.biWidth=CPx(18);colorHue=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&pixels,NULL,0);
    if(colorHue) {DWORD *p=pixels;for(int y=0;y<height;y++) {COLORREF c=HSVColor(y*360.0f/height,1,1);DWORD value=(GetRValue(c)<<16)|(GetGValue(c)<<8)|GetBValue(c);for(int x=0;x<CPx(18);x++) *p++=value;}}
    colorGradientDirty=FALSE;
}
static void PaintColors(HDC dc) {
    RECT all={0,0,CPx(560),CPx(460)};HBRUSH bg=CreateSolidBrush(RGB(20,22,25));HPEN border=CreatePen(PS_SOLID,CPx(1),RGB(49,53,59));
    HGDIOBJ oldBrush=SelectObject(dc,bg),oldPen=SelectObject(dc,border);FillRect(dc,&all,bg);RoundRect(dc,0,0,CPx(560),CPx(460),CPx(20),CPx(20));SetBkMode(dc,TRANSPARENT);
    ColorText(dc,L"托盘颜色",20,14,430,28,colorTitleFont,RGB(239,241,243));ColorText(dc,L"选择状态，再调整颜色",20,43,480,22,colorFont,RGB(154,163,175));
    HPEN cross=CreatePen(PS_SOLID,CPx(1),RGB(154,163,175));SelectObject(dc,cross);MoveToEx(dc,CPx(527),CPx(23),NULL);LineTo(dc,CPx(537),CPx(33));MoveToEx(dc,CPx(537),CPx(23),NULL);LineTo(dc,CPx(527),CPx(33));SelectObject(dc,border);DeleteObject(cross);
    MakeColorBitmaps(dc);HDC image=CreateCompatibleDC(dc);HGDIOBJ previous=NULL;
    if(colorSV) {previous=SelectObject(image,colorSV);BitBlt(dc,CPx(20),CPx(122),CPx(354),CPx(216),image,0,0,SRCCOPY);SelectObject(image,previous);}
    if(colorHue) {previous=SelectObject(image,colorHue);BitBlt(dc,CPx(388),CPx(122),CPx(18),CPx(216),image,0,0,SRCCOPY);SelectObject(image,previous);}DeleteDC(image);
    int sx=CPx(20)+(int)(colorS*(CPx(354)-1)),sy=CPx(122)+(int)((1-colorV)*(CPx(216)-1));
    HPEN marker=CreatePen(PS_SOLID,CPx(3),RGB(10,12,15));SelectObject(dc,marker);SelectObject(dc,GetStockObject(NULL_BRUSH));Ellipse(dc,sx-CPx(5),sy-CPx(5),sx+CPx(5),sy+CPx(5));SelectObject(dc,border);DeleteObject(marker);
    marker=CreatePen(PS_SOLID,CPx(1),RGB(255,255,255));SelectObject(dc,marker);Ellipse(dc,sx-CPx(5),sy-CPx(5),sx+CPx(5),sy+CPx(5));
    int hy=CPx(122)+(int)(colorH/360*(CPx(216)-1));MoveToEx(dc,CPx(385),hy-1,NULL);LineTo(dc,CPx(409),hy-1);MoveToEx(dc,CPx(385),hy+1,NULL);LineTo(dc,CPx(409),hy+1);SelectObject(dc,border);DeleteObject(marker);
    HBRUSH swatch=CreateSolidBrush(draftColors[selectedColor]);SelectObject(dc,swatch);RoundRect(dc,CPx(426),CPx(122),CPx(540),CPx(192),CPx(10),CPx(10));SelectObject(dc,oldBrush);DeleteObject(swatch);
    ColorText(dc,L"HEX",426,197,540,20,colorFont,RGB(154,163,175));ColorText(dc,L"R",426,260,458,20,colorFont,RGB(154,163,175));ColorText(dc,L"G",468,260,500,20,colorFont,RGB(154,163,175));ColorText(dc,L"B",510,260,542,20,colorFont,RGB(154,163,175));
    ColorText(dc,colorError?colorError:L"拖动色板与色相条，或输入 HEX / RGB",20,355,540,26,colorFont,colorError?RGB(255,130,130):RGB(154,163,175));
    SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(bg);DeleteObject(border);
}
static void DrawColorButton(const DRAWITEMSTRUCT *item) {
    HDC dc=item->hDC;BOOL focused=(item->itemState&ODS_FOCUS)!=0,pressed=(item->itemState&ODS_SELECTED)!=0,disabled=(item->itemState&ODS_DISABLED)!=0;
    BOOL tab=item->CtlID>=COLOR_BUTTON_BASE && item->CtlID<COLOR_BUTTON_BASE+COLOR_COUNT;int index=(int)item->CtlID-COLOR_BUTTON_BASE;
    COLORREF surface=tab?(index==selectedColor?RGB(44,49,57):RGB(29,33,39)):item->CtlID==COLOR_SAVE?RGB(44,128,87):RGB(40,44,50);if(pressed) surface=RGB(58,65,74);if(disabled) surface=RGB(33,37,42);
    HBRUSH canvas=CreateSolidBrush(RGB(20,22,25));FillRect(dc,&item->rcItem,canvas);DeleteObject(canvas);HBRUSH brush=CreateSolidBrush(surface);HPEN pen=CreatePen(PS_SOLID,CPx(focused?2:1),focused?RGB(150,190,235):tab&&index==selectedColor?RGB(103,112,126):RGB(60,66,75));
    HGDIOBJ oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);RoundRect(dc,item->rcItem.left,item->rcItem.top,item->rcItem.right,item->rcItem.bottom,CPx(10),CPx(10));
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,disabled?RGB(103,112,126):RGB(239,241,243));HGDIOBJ previous=SelectObject(dc,colorFont);RECT text=item->rcItem;
    if(tab) {
        if(!colorPreviews[index] || previewColors[index]!=draftColors[index]) {if(colorPreviews[index]) DestroyIcon(colorPreviews[index]);int icons[3]={89,15,101};HICON base=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(1000+icons[index]),IMAGE_ICON,CPx(22),CPx(22),0);colorPreviews[index]=base?TintIcon(base,draftColors[index]):NULL;if(base) DestroyIcon(base);previewColors[index]=draftColors[index];}
        if(colorPreviews[index]) DrawIconEx(dc,CPx(12),CPx(7),colorPreviews[index],CPx(22),CPx(22),0,NULL,DI_NORMAL);
        text.left+=CPx(43);DrawTextW(dc,colorLabels[index],-1,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    } else {wchar_t label[48];GetWindowTextW(item->hwndItem,label,48);DrawTextW(dc,label,-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    SelectObject(dc,previous);SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(pen);DeleteObject(brush);
}
static void LayoutColorControls(void) {
    for(int i=0;i<3;i++) SetWindowPos(GetDlgItem(colorWindow,COLOR_BUTTON_BASE+i),NULL,CPx(20+i*178),CPx(74),CPx(164),CPx(36),SWP_NOZORDER);
    SetWindowPos(GetDlgItem(colorWindow,COLOR_HEX),NULL,CPx(426),CPx(220),CPx(114),CPx(30),SWP_NOZORDER);
    for(int i=0;i<3;i++) SetWindowPos(GetDlgItem(colorWindow,COLOR_RED+i),NULL,CPx(426+i*42),CPx(283),CPx(32),CPx(30),SWP_NOZORDER);
    SetWindowPos(GetDlgItem(colorWindow,COLOR_RESET),NULL,CPx(20),CPx(403),CPx(118),CPx(36),SWP_NOZORDER);SetWindowPos(GetDlgItem(colorWindow,COLOR_CANCEL),NULL,CPx(332),CPx(403),CPx(96),CPx(36),SWP_NOZORDER);SetWindowPos(GetDlgItem(colorWindow,COLOR_SAVE),NULL,CPx(444),CPx(403),CPx(96),CPx(36),SWP_NOZORDER);
    if(colorFont) DeleteObject(colorFont);
    if(colorTitleFont) DeleteObject(colorTitleFont);
    colorFont=CreateFontW(-CPx(12),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");colorTitleFont=CreateFontW(-CPx(18),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    for(int i=0;i<4;i++) SendMessageW(GetDlgItem(colorWindow,COLOR_HEX+i),WM_SETFONT,(WPARAM)colorFont,TRUE);
    for(int i=0;i<3;i++) {if(colorPreviews[i]) DestroyIcon(colorPreviews[i]);colorPreviews[i]=NULL;}
    colorGradientDirty=TRUE;SetWindowRgn(colorWindow,CreateRoundRectRgn(0,0,CPx(560)+1,CPx(460)+1,CPx(20),CPx(20)),FALSE);InvalidateRect(colorWindow,NULL,TRUE);
}
static void DragColor(int x,int y) {
    if(colorDrag==1) {colorS=Clamp01((x-CPx(20))/(float)(CPx(354)-1));colorV=1-Clamp01((y-CPx(122))/(float)(CPx(216)-1));}
    else if(colorDrag==2) {colorH=Clamp01((y-CPx(122))/(float)(CPx(216)-1))*359.999f;colorGradientDirty=TRUE;}
    draftColors[selectedColor]=HSVColor(colorH,colorS,colorV);RefreshColorView(0);
}
static LRESULT CALLBACK ColorProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_COMMAND: {
        int id=LOWORD(wp);
        if(id>=COLOR_HEX && id<=COLOR_BLUE && HIWORD(wp)==EN_CHANGE && !colorSyncing) {if(!ReadColorInputs(id)) {colorError=L"HEX 需为 6 位，RGB 范围为 0–255";EnableWindow(GetDlgItem(h,COLOR_SAVE),FALSE);InvalidateRect(h,NULL,FALSE);}return 0;}
        if(id>=COLOR_BUTTON_BASE && id<COLOR_BUTTON_BASE+3) {selectedColor=id-COLOR_BUTTON_BASE;RGBToPicker(draftColors[selectedColor]);RefreshColorView(0);}
        else if(id==COLOR_RESET) {CopyMemory(draftColors,defaultColors,sizeof(draftColors));RGBToPicker(draftColors[selectedColor]);RefreshColorView(0);}
        else if(id==COLOR_SAVE || id==IDOK) {if(colorError) return 0;if(!SaveColorSettings(draftColors)) {colorError=L"无法保存颜色设置，请检查当前用户权限";InvalidateRect(h,NULL,FALSE);return 0;}CopyMemory(trayColors,draftColors,sizeof(trayColors));iconDpi=0;if(hasReading) UpdateTray(&lastReading,TRUE);DestroyWindow(h);}
        else if(id==COLOR_CANCEL || id==IDCANCEL) DestroyWindow(h);
        return 0;
    }
    case WM_CTLCOLOREDIT:SetTextColor((HDC)wp,RGB(239,241,243));SetBkColor((HDC)wp,RGB(35,39,46));return (LRESULT)colorEditBrush;
    case WM_DRAWITEM:DrawColorButton((const DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps),buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,CPx(560),CPx(460));HGDIOBJ old=SelectObject(buffer,bitmap);PaintColors(buffer);BitBlt(dc,0,0,CPx(560),CPx(460),buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(h,&ps);return 0;}
    case WM_PRINTCLIENT:PaintColors((HDC)wp);return 0;
    case WM_NCHITTEST: {POINT p={(short)LOWORD(lp),(short)HIWORD(lp)};ScreenToClient(h,&p);if(p.y<CPx(65) && !(p.x>=CPx(518)&&p.y<=CPx(42))) return HTCAPTION;return HTCLIENT;}
    case WM_LBUTTONDOWN: {int x=(short)LOWORD(lp),y=(short)HIWORD(lp);if(x>=CPx(518)&&y<=CPx(42)) {DestroyWindow(h);return 0;}if(x>=CPx(20)&&x<CPx(374)&&y>=CPx(122)&&y<CPx(338)) colorDrag=1;else if(x>=CPx(382)&&x<CPx(412)&&y>=CPx(122)&&y<CPx(338)) colorDrag=2;if(colorDrag) {SetCapture(h);DragColor(x,y);}return 0;}
    case WM_MOUSEMOVE:if(colorDrag) DragColor((short)LOWORD(lp),(short)HIWORD(lp));return 0;
    case WM_LBUTTONUP:if(colorDrag) {DragColor((short)LOWORD(lp),(short)HIWORD(lp));colorDrag=0;ReleaseCapture();}return 0;
    case WM_CAPTURECHANGED:colorDrag=0;return 0;
    case WM_DPICHANGED: {colorDpi=LOWORD(wp);RECT *r=(RECT*)lp;SetWindowPos(h,NULL,r->left,r->top,CPx(560),CPx(460),SWP_NOZORDER|SWP_NOACTIVATE);LayoutColorControls();return 0;}
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:if(colorDrag) ReleaseCapture();colorDrag=0;ReleaseColorBitmaps();for(int i=0;i<3;i++) {if(colorPreviews[i]) DestroyIcon(colorPreviews[i]);colorPreviews[i]=NULL;}if(colorFont) DeleteObject(colorFont);if(colorTitleFont) DeleteObject(colorTitleFont);colorFont=colorTitleFont=NULL;if(colorEditBrush) DeleteObject(colorEditBrush);colorEditBrush=NULL;colorWindow=NULL;return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
static void ShowColorSettings(void) {
    if(colorWindow) {SetForegroundWindow(colorWindow);return;}
    CopyMemory(draftColors,trayColors,sizeof(draftColors));selectedColor=0;colorError=NULL;RGBToPicker(draftColors[0]);POINT cursor;GetCursorPos(&cursor);HMONITOR monitor=MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST);colorDpi=MonitorDpi(monitor);MONITORINFO mi={0};mi.cbSize=sizeof(mi);GetMonitorInfoW(monitor,&mi);
    WNDCLASSW cls={0};cls.lpfnWndProc=ColorProc;cls.hInstance=instance;cls.hCursor=LoadCursorW(NULL,IDC_ARROW);cls.lpszClassName=L"MchoseBatteryColors";cls.style=CS_DROPSHADOW;RegisterClassW(&cls);
    int width=CPx(560),height=CPx(460);colorWindow=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"颜色设置",WS_POPUP|WS_SYSMENU,mi.rcWork.left+(mi.rcWork.right-mi.rcWork.left-width)/2,mi.rcWork.top+(mi.rcWork.bottom-mi.rcWork.top-height)/2,width,height,window,NULL,instance,NULL);if(!colorWindow) return;
    colorEditBrush=CreateSolidBrush(RGB(35,39,46));
    for(int i=0;i<3;i++) CreateWindowExW(0,L"BUTTON",colorLabels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,colorWindow,(HMENU)(INT_PTR)(COLOR_BUTTON_BASE+i),instance,NULL);
    for(int i=0;i<4;i++) {HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_CENTER|ES_AUTOHSCROLL|(i?ES_NUMBER:0),0,0,0,0,colorWindow,(HMENU)(INT_PTR)(COLOR_HEX+i),instance,NULL);SendMessageW(edit,EM_LIMITTEXT,i?3:7,0);}
    CreateWindowExW(0,L"BUTTON",L"恢复默认",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,colorWindow,(HMENU)(INT_PTR)COLOR_RESET,instance,NULL);CreateWindowExW(0,L"BUTTON",L"取消",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,colorWindow,(HMENU)(INT_PTR)COLOR_CANCEL,instance,NULL);CreateWindowExW(0,L"BUTTON",L"保存",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,colorWindow,(HMENU)(INT_PTR)COLOR_SAVE,instance,NULL);
    LayoutColorControls();SyncColorInputs(0);ShowWindow(colorWindow,SW_SHOW);SetForegroundWindow(colorWindow);SetFocus(GetDlgItem(colorWindow,COLOR_BUTTON_BASE));
}
