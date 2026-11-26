// Small transient Win32/GDI menu: per-monitor DPI, no UI runtime or persistent renderer.
static UINT menuDpi=96;
static HFONT menuFont,menuTitleFont,menuSmallFont;
static int menuHover=-1;
static BOOL menuStartup=FALSE,menuHighContrast=FALSE;
static COLORREF menuBg,menuInk,menuMuted,menuBorder,menuHoverBg,menuAccent;
static const int rowTop[5]={70,108,146,184,232};
static int Px(int n) {return MulDiv(n,(int)menuDpi,96);}
static void MenuFonts(BOOL create) {
    if(menuFont) DeleteObject(menuFont);
    if(menuTitleFont) DeleteObject(menuTitleFont);
    if(menuSmallFont) DeleteObject(menuSmallFont);
    menuFont=menuTitleFont=menuSmallFont=NULL;
    if(!create) return;
    menuFont=CreateFontW(-Px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    menuTitleFont=CreateFontW(-Px(14),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    menuSmallFont=CreateFontW(-Px(11),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
}
static void MenuColors(void) {
    HIGHCONTRASTW hc={sizeof(hc),0,NULL};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0);menuHighContrast=(hc.dwFlags&HCF_HIGHCONTRASTON)!=0;
    menuBg=menuHighContrast?GetSysColor(COLOR_MENU):RGB(20,22,25);
    menuInk=menuHighContrast?GetSysColor(COLOR_MENUTEXT):RGB(239,241,243);
    menuMuted=menuHighContrast?GetSysColor(COLOR_MENUTEXT):RGB(148,155,165);
    menuBorder=menuHighContrast?GetSysColor(COLOR_MENUTEXT):RGB(49,53,59);
    menuHoverBg=menuHighContrast?GetSysColor(COLOR_HIGHLIGHT):RGB(40,44,50);
    menuAccent=menuHighContrast?GetSysColor(COLOR_MENUTEXT):hasReading?StatusColor(&lastReading):RGB(148,155,165);
}
static void Line(HDC dc,int x1,int y1,int x2,int y2) {MoveToEx(dc,Px(x1),Px(y1),NULL);LineTo(dc,Px(x2),Px(y2));}
static void Text(HDC dc,const wchar_t *s,int x,int y,int right,int height,HFONT font,COLORREF color,UINT flags) {
    RECT r={Px(x),Px(y),Px(right),Px(y+height)};HGDIOBJ previous=SelectObject(dc,font);SetTextColor(dc,color);DrawTextW(dc,s,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|flags);SelectObject(dc,previous);
}
static void PaintMenu(HDC dc) {
    RECT all={0,0,Px(248),Px(282)};SetBkMode(dc,TRANSPARENT);
    HBRUSH bg=CreateSolidBrush(menuBg),hover=CreateSolidBrush(menuHoverBg),status=CreateSolidBrush(menuAccent);
    HPEN border=CreatePen(PS_SOLID,Px(1),menuBorder),lines=CreatePen(PS_SOLID,Px(1),menuMuted);
    HGDIOBJ oldBrush=SelectObject(dc,bg),oldPen=SelectObject(dc,border);
    FillRect(dc,&all,bg);RoundRect(dc,0,0,Px(248),Px(282),Px(20),Px(20));
    Text(dc,hasReading && lastReading.model[0]?lastReading.model:L"迈从键盘",16,12,230,24,menuTitleFont,menuInk,DT_LEFT|DT_END_ELLIPSIS);
    SelectObject(dc,status);SelectObject(dc,GetStockObject(NULL_PEN));Ellipse(dc,Px(17),Px(45),Px(23),Px(51));
    wchar_t label[96];
    if(!hasReading) wcscpy(label,L"等待键盘输入 · 点击可手动读取");
    else if(!lastReading.ok) wcscpy(label,L"暂无读数 · 点击刷新重试");
    else if(lastReading.stale && lastReading.charging) wcscpy(label,L"上次状态：充电 · 按键后更新");
    else if(lastReading.stale) _snwprintf(label,96,L"上次读数 %d%% · 按键后更新",lastReading.percent);
    else if(lastReading.cached) _snwprintf(label,96,L"%d%% · 蓝牙系统缓存，状态未确认",lastReading.percent);
    else if(lastReading.charging) wcscpy(label,L"正在充电");
    else _snwprintf(label,96,L"%d%% · %ls",lastReading.percent,lastReading.connection[0]?lastReading.connection:L"电池供电");
    Text(dc,label,30,37,230,23,menuSmallFont,menuMuted,DT_LEFT);
    SelectObject(dc,border);Line(dc,12,64,236,64);Line(dc,12,226,236,226);
    const wchar_t *labels[5]={L"立即刷新",L"开机启动",L"颜色设置",L"使用说明",L"退出"};
    for(int i=0;i<5;i++) {
        int y=rowTop[i],cy=y+19;
        if(menuHover==i) {SelectObject(dc,hover);SelectObject(dc,GetStockObject(NULL_PEN));RoundRect(dc,Px(7),Px(y+2),Px(241),Px(y+36),Px(12),Px(12));}
        COLORREF fg=menuHighContrast&&menuHover==i?GetSysColor(COLOR_HIGHLIGHTTEXT):i==4?RGB(234,155,155):menuInk;
        if(menuHighContrast && menuHover!=i) fg=menuInk;
        Text(dc,labels[i],44,y,220,38,menuFont,fg,DT_LEFT);
        HPEN glyph=CreatePen(PS_SOLID,Px(1),fg);SelectObject(dc,glyph);SelectObject(dc,GetStockObject(NULL_BRUSH));
        if(i==0) {Arc(dc,Px(20),Px(cy-6),Px(33),Px(cy+7),Px(32),Px(cy-5),Px(33),Px(cy+3));Line(dc,32,cy-7,32,cy-2);Line(dc,28,cy-2,33,cy-2);}
        else if(i==1) {Arc(dc,Px(20),Px(cy-6),Px(33),Px(cy+7),Px(24),Px(cy-5),Px(29),Px(cy-5));Line(dc,26,cy-8,26,cy);}
        else if(i==2) {Ellipse(dc,Px(19),Px(cy-7),Px(34),Px(cy+7));Ellipse(dc,Px(23),Px(cy-4),Px(25),Px(cy-2));Ellipse(dc,Px(28),Px(cy-4),Px(30),Px(cy-2));Ellipse(dc,Px(22),Px(cy+1),Px(24),Px(cy+3));}
        else if(i==3) {RoundRect(dc,Px(20),Px(cy-7),Px(33),Px(cy+7),Px(3),Px(3));Line(dc,24,cy-3,29,cy-3);Line(dc,24,cy+1,29,cy+1);}
        else {Line(dc,21,cy-6,21,cy+6);Line(dc,21,cy-6,26,cy-6);Line(dc,21,cy+6,26,cy+6);Line(dc,25,cy,34,cy);Line(dc,31,cy-3,34,cy);Line(dc,31,cy+3,34,cy);}
        SelectObject(dc,lines);DeleteObject(glyph);
        if(i==0) Text(dc,L"F5",202,y,230,38,menuSmallFont,menuMuted,DT_RIGHT);
        if(i==1 && menuStartup) {HPEN check=CreatePen(PS_SOLID,Px(2),menuHighContrast?fg:RGB(40,222,98));SelectObject(dc,check);Line(dc,214,cy,218,cy+4);Line(dc,218,cy+4,225,cy-4);SelectObject(dc,lines);DeleteObject(check);}
    }
    SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(bg);DeleteObject(hover);DeleteObject(status);DeleteObject(border);DeleteObject(lines);
}
static int MenuHit(int x,int y) {
    if(x<Px(7) || x>=Px(241)) return -1;
    for(int i=0;i<5;i++) if(y>=Px(rowTop[i]+2) && y<Px(rowTop[i]+36)) return i;
    return -1;
}
static void MenuChoose(int index) {
    HWND old=menuWindow;int commands[5]={ID_REFRESH,ID_STARTUP,ID_COLORS,ID_HELP,ID_EXIT};
    if(old) DestroyWindow(old);
    if(index>=0 && index<5) PostMessageW(window,WM_COMMAND,commands[index],0);
}
static LRESULT CALLBACK MenuProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps),buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,Px(248),Px(282));HGDIOBJ old=SelectObject(buffer,bitmap);PaintMenu(buffer);BitBlt(dc,0,0,Px(248),Px(282),buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(h,&ps);return 0;}
    case WM_PRINTCLIENT:PaintMenu((HDC)wp);return 0;
    case WM_MOUSEMOVE: {TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);int next=MenuHit((short)LOWORD(lp),(short)HIWORD(lp));if(next!=menuHover) {menuHover=next;InvalidateRect(h,NULL,FALSE);}return 0;}
    case WM_MOUSELEAVE:menuHover=-1;InvalidateRect(h,NULL,FALSE);return 0;
    case WM_LBUTTONUP:MenuChoose(MenuHit((short)LOWORD(lp),(short)HIWORD(lp)));return 0;
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE) DestroyWindow(h);
        else if(wp==VK_DOWN || wp==VK_TAB) {menuHover=(menuHover+1)%5;InvalidateRect(h,NULL,FALSE);}
        else if(wp==VK_UP) {menuHover=menuHover<0?4:(menuHover+4)%5;InvalidateRect(h,NULL,FALSE);}
        else if(wp==VK_RETURN || wp==VK_SPACE) MenuChoose(menuHover);
        else if(wp==VK_F5) MenuChoose(0);
        return 0;
    case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE) DestroyWindow(h);return 0;
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DPICHANGED:DestroyWindow(h);return 0;
    case WM_DESTROY:menuWindow=NULL;MenuFonts(FALSE);return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
static void ShowMenu(void) {
    if(colorWindow) {SetForegroundWindow(colorWindow);return;}
    if(menuWindow) {DestroyWindow(menuWindow);return;}
    POINT p;MONITORINFO mi={0};mi.cbSize=sizeof(mi);GetCursorPos(&p);HMONITOR monitor=MonitorFromPoint(p,MONITOR_DEFAULTTONEAREST);GetMonitorInfoW(monitor,&mi);
    menuDpi=MonitorDpi(monitor);MenuFonts(TRUE);MenuColors();menuStartup=StartupEnabled();menuHover=-1;
    int width=Px(248),height=Px(282),x=p.x-width+Px(12),y=p.y-height-Px(8);
    if(x<mi.rcWork.left) x=mi.rcWork.left;
    if(x+width>mi.rcWork.right) x=mi.rcWork.right-width;
    if(y<mi.rcWork.top) y=p.y+Px(8);
    if(y+height>mi.rcWork.bottom) y=mi.rcWork.bottom-height;
    menuWindow=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"G75BatteryMenu",L"迈从键盘电量助手菜单",WS_POPUP,x,y,width,height,window,NULL,instance,NULL);
    if(!menuWindow) {MenuFonts(FALSE);return;}
    SetWindowRgn(menuWindow,CreateRoundRectRgn(0,0,width+1,height+1,Px(20),Px(20)),FALSE);
    SetForegroundWindow(menuWindow);ShowWindow(menuWindow,SW_SHOW);SetFocus(menuWindow);
}
static int PreviewMenu(UINT dpi) {
    if(dpi<96 || dpi>384) return 20;
    menuDpi=dpi;MenuFonts(TRUE);MenuColors();menuStartup=TRUE;menuHover=1;
    lastReading.ok=TRUE;lastReading.percent=93;wcscpy(lastReading.model,L"G75 V2");hasReading=TRUE;MenuColors();
    HDC screen=GetDC(NULL),dc=CreateCompatibleDC(screen);BITMAPINFO info={0};void *pixels;
    int width=Px(248),height=Px(282);info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,NULL,0);HGDIOBJ old=SelectObject(dc,bitmap);PaintMenu(dc);
    wchar_t path[MAX_PATH];_snwprintf(path,MAX_PATH,L"%lsmenu-%u.bmp",folder,dpi);HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE) {SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(NULL,screen);MenuFonts(FALSE);return 21;}
    BITMAPFILEHEADER head={0};head.bfType=0x4d42;head.bfOffBits=sizeof(head)+sizeof(BITMAPINFOHEADER);head.bfSize=head.bfOffBits+width*height*4;DWORD written;
    WriteFile(f,&head,sizeof(head),&written,NULL);WriteFile(f,&info.bmiHeader,sizeof(BITMAPINFOHEADER),&written,NULL);WriteFile(f,pixels,width*height*4,&written,NULL);CloseHandle(f);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(NULL,screen);MenuFonts(FALSE);return 0;
}
