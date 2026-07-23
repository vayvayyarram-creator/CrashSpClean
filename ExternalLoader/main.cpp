/*
 * ExternalLoader/main.cpp  —  Moon Private Standalone Launcher
 *
 * Kullanici bu EXE'yi acar → key giris (ilk kez) veya kayitli key →
 * Cloudflare Workers auth (HTTPS, /auth) → SpCrashReport.dll indir + yükle.
 * GoodbyeDPI veya baska bir baglantiya gerek yok.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

/* ── Sunucu (Cloudflare Workers, HTTPS, port 443) ──────────── */
static const wchar_t SRV_HOST[] = L"moon-auth-service.moonsal.workers.dev";
#define SRV_PORT  443

/* ── Key registry yolu (kamufle CLSID) ─────────────────────── */
#define REG_PATH "SOFTWARE\\Classes\\CLSID\\{A8F2D341-C912-4B7E-9D23-5E81F3A2B0C4}"

/* ── FudInitParams — DLL DllMain'e gecir (WsClient.hpp ile esles) ─ */
typedef struct {
    char          cheatKey[72];
    char          wsHost[128];
    unsigned short wsPort;
} FudInitParams;

/* ════════════════════════════════════════════════════════════════
   Temel yardimcilar
   ════════════════════════════════════════════════════════════════ */

/* JSON: "field":"value" → value */
static int _jstr(const char *j, const char *f, char *o, int sz) {
    char pat[80]; int pl = 0;
    pat[pl++]='"';
    for (int i=0;f[i];i++) pat[pl++]=f[i];
    pat[pl++]='"'; pat[pl++]=':'; pat[pl++]='"'; pat[pl]=0;
    const char *p=j;
    while (*p) {
        if (memcmp(p,pat,(size_t)pl)==0) {
            int i=0; p+=pl;
            while (*p && *p!='"' && i<sz-1) o[i++]=*p++;
            o[i]=0; return 1;
        }
        p++;
    }
    return 0;
}

/* JSON: "field":true/false */
static int _jbool(const char *j, const char *f) {
    char pat[80]; int pl=0;
    pat[pl++]='"';
    for (int i=0;f[i];i++) pat[pl++]=f[i];
    pat[pl++]='"'; pat[pl++]=':'; pat[pl]=0;
    const char *p=strstr(j,pat);
    if (!p) return 0;
    p+=pl; while(*p==' ')p++;
    return strncmp(p,"true",4)==0;
}

/* ── HWID: MachineGuid ilk 8 hex ────────────────────────────── */
static void _hwid(char *out, int sz) {
    HKEY hk;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ|KEY_WOW64_64KEY, &hk)==ERROR_SUCCESS) {
        char guid[64]={};
        DWORD gsz=sizeof(guid);
        if (RegQueryValueExA(hk,"MachineGuid",NULL,NULL,(LPBYTE)guid,&gsz)==ERROR_SUCCESS) {
            int n=0;
            for (char *p=guid; *p && n<16; p++) {
                if (*p=='-') continue;
                out[n++]=(*p>='a'&&*p<='f')?(char)(*p-32):*p;
            }
            if (n>=8) { out[n]=0; RegCloseKey(hk); return; }
        }
        RegCloseKey(hk);
    }
    DWORD serial=0;
    GetVolumeInformationA("C:\\",NULL,0,&serial,NULL,NULL,NULL,0);
    snprintf(out,sz,"%08X",(unsigned)serial);
}

/* ── Registry key kaydet/yukle ──────────────────────────────── */
static void _saveKey(const char *key) {
    HKEY hk;
    if (RegCreateKeyExA(HKEY_CURRENT_USER,REG_PATH,0,NULL,
            REG_OPTION_NON_VOLATILE,KEY_WRITE,NULL,&hk,NULL)!=ERROR_SUCCESS) return;
    RegSetValueExA(hk,"Default",0,REG_SZ,(LPBYTE)key,(DWORD)strlen(key)+1);
    RegCloseKey(hk);
}

static int _loadKey(char *key, int sz) {
    HKEY hk;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,REG_PATH,0,KEY_READ,&hk)!=ERROR_SUCCESS) return 0;
    char buf[128]={};
    DWORD bsz=sizeof(buf),tp=0;
    RegQueryValueExA(hk,"Default",NULL,&tp,(LPBYTE)buf,&bsz);
    RegCloseKey(hk);
    /* trim */
    int len=(int)strlen(buf);
    while (len>0&&(buf[len-1]=='\r'||buf[len-1]=='\n'||buf[len-1]==' ')) buf[--len]=0;
    if (len<4) return 0;
    strncpy(key,buf,sz-1); key[sz-1]=0;
    return 1;
}

static void _delKey(void) {
    RegDeleteKeyA(HKEY_CURRENT_USER,REG_PATH);
}

/* ════════════════════════════════════════════════════════════════
   WinHTTP — HTTP (plain, port 80, direkt IP)
   ════════════════════════════════════════════════════════════════ */

static HINTERNET _openSession(void) {
    return WinHttpOpen(L"Mozilla/5.0",
                       WINHTTP_ACCESS_TYPE_NO_PROXY,
                       WINHTTP_NO_PROXY_NAME,
                       WINHTTP_NO_PROXY_BYPASS, 0);
}

/* POST path ile JSON body gönder → alloc'd yanit string (cagiran free'ler) */
static char *_http_post(const wchar_t *path, const char *body) {
    HINTERNET hS=_openSession(); if (!hS) return NULL;
    HINTERNET hC=WinHttpConnect(hS,SRV_HOST,SRV_PORT,0);
    if (!hC){WinHttpCloseHandle(hS);return NULL;}
    /* HTTPS zorunlu — Cloudflare */
    HINTERNET hR=WinHttpOpenRequest(hC,L"POST",path,NULL,
                  WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if (!hR){WinHttpCloseHandle(hC);WinHttpCloseHandle(hS);return NULL;}
    DWORD t=15000;
    WinHttpSetOption(hR,WINHTTP_OPTION_CONNECT_TIMEOUT,&t,sizeof(t));
    WinHttpSetOption(hR,WINHTTP_OPTION_RECEIVE_TIMEOUT,&t,sizeof(t));

    const wchar_t *hdrs=L"Content-Type: application/json\r\n";
    DWORD blen=(DWORD)strlen(body);
    BOOL ok=WinHttpSendRequest(hR,hdrs,(DWORD)-1,(LPVOID)body,blen,blen,0)
         && WinHttpReceiveResponse(hR,NULL);

    char *buf=NULL; DWORD tot=0;
    if (ok) {
        DWORD av=0;
        while (WinHttpQueryDataAvailable(hR,&av)&&av>0) {
            char *tmp=(char*)realloc(buf,tot+av+1);
            if (!tmp){free(buf);buf=NULL;tot=0;break;}
            buf=tmp; DWORD rd=0;
            if (WinHttpReadData(hR,buf+tot,av,&rd)) tot+=rd;
        }
        if (buf) buf[tot]=0;
    }
    WinHttpCloseHandle(hR);WinHttpCloseHandle(hC);WinHttpCloseHandle(hS);
    return buf;
}

/* GET binary → alloc'd buffer (cagiran free'ler) */
static unsigned char *_http_get_bin(const wchar_t *path, DWORD *out_sz) {
    *out_sz=0;
    HINTERNET hS=_openSession(); if (!hS) return NULL;
    HINTERNET hC=WinHttpConnect(hS,SRV_HOST,SRV_PORT,0);
    if (!hC){WinHttpCloseHandle(hS);return NULL;}
    HINTERNET hR=WinHttpOpenRequest(hC,L"GET",path,NULL,
                  WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if (!hR){WinHttpCloseHandle(hC);WinHttpCloseHandle(hS);return NULL;}
    DWORD t=30000;
    WinHttpSetOption(hR,WINHTTP_OPTION_CONNECT_TIMEOUT,&t,sizeof(t));
    WinHttpSetOption(hR,WINHTTP_OPTION_RECEIVE_TIMEOUT,&t,sizeof(t));
    BOOL ok=WinHttpSendRequest(hR,WINHTTP_NO_ADDITIONAL_HEADERS,0,NULL,0,0,0)
         && WinHttpReceiveResponse(hR,NULL);
    unsigned char *buf=NULL; DWORD tot=0;
    if (ok) {
        DWORD av=0;
        while (WinHttpQueryDataAvailable(hR,&av)&&av>0) {
            unsigned char *tmp=(unsigned char*)realloc(buf,tot+av+2);
            if (!tmp){free(buf);buf=NULL;tot=0;break;}
            buf=tmp; DWORD rd=0;
            if (WinHttpReadData(hR,buf+tot,av,&rd)) tot+=rd;
        }
        if (buf) buf[tot]=0;
    }
    WinHttpCloseHandle(hR);WinHttpCloseHandle(hC);WinHttpCloseHandle(hS);
    *out_sz=tot; return buf;
}

/* ════════════════════════════════════════════════════════════════
   Auth + DL
   ════════════════════════════════════════════════════════════════ */

/* 1=ok  0=net_err  -1=self_delete  -2=invalid_key */
static int _auth(const char *cheatKey, const char *hwid,
                 char *dlTok, char *wsUrl) {
    char body[256];
    snprintf(body,sizeof(body),
             "{\"cheatKey\":\"%s\",\"hwid\":\"%s\"}",cheatKey,hwid);
    char *resp=_http_post(L"/api/cheat/auth",body);
    if (!resp) return 0;

    if (!_jbool(resp,"ok")) {
        char action[32]={},reason[32]={};
        _jstr(resp,"action",action,sizeof(action));
        _jstr(resp,"reason",reason,sizeof(reason));
        free(resp);
        if (strcmp(action,"self_delete")==0) return -1;
        if (strcmp(reason,"hwid_mismatch")==0) return -1;
        return -2;
    }
    _jstr(resp,"dlToken",dlTok,72);
    _jstr(resp,"wsUrl",wsUrl,128);
    free(resp);
    return (dlTok[0]!=0)?1:0;
}

/* DLL indir ve XOR coz (key ile) */
static unsigned char *_dl(const char *tok, const char *key, DWORD *sz) {
    /* /api/cheat/dl/{token}/{key} */
    wchar_t path[512];
    /* tok + key ASCII → wide */
    wchar_t wtok[128]={},wkey[128]={};
    for (int i=0;tok[i]&&i<127;i++) wtok[i]=(wchar_t)(unsigned char)tok[i];
    for (int i=0;key[i]&&i<127;i++) wkey[i]=(wchar_t)(unsigned char)key[i];
    _snwprintf_s(path,512,_TRUNCATE,L"/api/cheat/dl/%s/%s",wtok,wkey);

    unsigned char *buf=_http_get_bin(path,sz);
    if (!buf||*sz<0x200){free(buf);return NULL;}

    /* XOR decode — key ile */
    DWORD kl=(DWORD)strlen(key);
    for (DWORD i=0;i<*sz;i++) buf[i]^=(unsigned char)key[i%kl];
    return buf;
}

/* ════════════════════════════════════════════════════════════════
   PE Manual Map
   ════════════════════════════════════════════════════════════════ */
static BOOL _map(const unsigned char *raw, SIZE_T rsz,
                 const char *cheatKey, const char *wsHost, unsigned short wsPort) {
    if (rsz<sizeof(IMAGE_DOS_HEADER)) return FALSE;
    const IMAGE_DOS_HEADER *dos=(const IMAGE_DOS_HEADER *)raw;
    if (dos->e_magic!=IMAGE_DOS_SIGNATURE) return FALSE;
    const IMAGE_NT_HEADERS64 *nt=
        (const IMAGE_NT_HEADERS64 *)(raw+dos->e_lfanew);
    if (nt->Signature!=IMAGE_NT_SIGNATURE) return FALSE;

    SIZE_T isz=nt->OptionalHeader.SizeOfImage;
    LPVOID base=VirtualAlloc((LPVOID)(UINT_PTR)nt->OptionalHeader.ImageBase,
                              isz,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    if (!base)
        base=VirtualAlloc(NULL,isz,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    if (!base) return FALSE;

    memcpy(base,raw,nt->OptionalHeader.SizeOfHeaders);
    const IMAGE_SECTION_HEADER *sec=
        (const IMAGE_SECTION_HEADER *)((const BYTE *)(nt+1));
    for (WORD i=0;i<nt->FileHeader.NumberOfSections;i++) {
        if (!sec[i].SizeOfRawData) continue;
        memcpy((BYTE*)base+sec[i].VirtualAddress,
               raw+sec[i].PointerToRawData,sec[i].SizeOfRawData);
    }

    /* Relocations */
    LONGLONG delta=(LONGLONG)((UINT_PTR)base-(UINT_PTR)nt->OptionalHeader.ImageBase);
    if (delta) {
        const IMAGE_DATA_DIRECTORY *rd=
            &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        BYTE *blk=(BYTE*)base+rd->VirtualAddress, *end=blk+rd->Size;
        while (blk<end) {
            IMAGE_BASE_RELOCATION *b=(IMAGE_BASE_RELOCATION *)blk;
            if (!b->SizeOfBlock) break;
            DWORD cnt=(b->SizeOfBlock-sizeof(IMAGE_BASE_RELOCATION))/sizeof(WORD);
            WORD *e=(WORD*)(b+1);
            for (DWORD j=0;j<cnt;j++) {
                if ((e[j]>>12)==IMAGE_REL_BASED_DIR64) {
                    UINT_PTR *p=(UINT_PTR*)((BYTE*)base+b->VirtualAddress+(e[j]&0x0FFF));
                    *p+=(UINT_PTR)delta;
                }
            }
            blk+=b->SizeOfBlock;
        }
    }

    /* Imports */
    const IMAGE_DATA_DIRECTORY *impD=
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (impD->VirtualAddress) {
        IMAGE_IMPORT_DESCRIPTOR *imp=
            (IMAGE_IMPORT_DESCRIPTOR*)((BYTE*)base+impD->VirtualAddress);
        for (;imp->Name;imp++) {
            HMODULE hd=LoadLibraryA((const char*)((BYTE*)base+imp->Name));
            if (!hd) continue;
            IMAGE_THUNK_DATA64
                *thk=(IMAGE_THUNK_DATA64*)((BYTE*)base+imp->FirstThunk),
                *orig=(IMAGE_THUNK_DATA64*)((BYTE*)base+
                      (imp->OriginalFirstThunk?imp->OriginalFirstThunk:imp->FirstThunk));
            for (;orig->u1.AddressOfData;thk++,orig++) {
                FARPROC fn=NULL;
                if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG64)
                    fn=GetProcAddress(hd,(LPCSTR)(orig->u1.Ordinal&0xFFFF));
                else {
                    IMAGE_IMPORT_BY_NAME *ibn=
                        (IMAGE_IMPORT_BY_NAME*)((BYTE*)base+orig->u1.AddressOfData);
                    fn=GetProcAddress(hd,ibn->Name);
                }
                thk->u1.Function=(ULONGLONG)fn;
            }
        }
    }

    FlushInstructionCache(GetCurrentProcess(),base,isz);

    /* FudInitParams yükle */
    static FudInitParams params;
    memset(&params,0,sizeof(params));
    strncpy(params.cheatKey,cheatKey,sizeof(params.cheatKey)-1);
    strncpy(params.wsHost,  wsHost,  sizeof(params.wsHost)-1);
    params.wsPort=wsPort;

    typedef BOOL (WINAPI *DllMFn)(HINSTANCE,DWORD,LPVOID);
    DllMFn ep=(DllMFn)((BYTE*)base+nt->OptionalHeader.AddressOfEntryPoint);
    ep((HINSTANCE)base,DLL_PROCESS_ATTACH,(LPVOID)&params);
    return TRUE;
}

/* wsUrl parse: ws://host:port/path → host, port */
static void _parseWs(const char *wsUrl, char *host, int hlen, unsigned short *port) {
    const char *p=wsUrl;
    *port=80;
    if (strncmp(p,"wss://",6)==0){p+=6;*port=443;}
    else if (strncmp(p,"ws://",5)==0){p+=5;}
    int i=0;
    while (*p&&*p!=':'&&*p!='/'&&i<hlen-1) host[i++]=*p++;
    host[i]=0;
    if (*p==':'){p++;*port=(unsigned short)atoi(p);}
    if (!host[0]) {
        strncpy(host,"moon-auth-service.moonsal.workers.dev",hlen-1);
        *port=443;
    }
}

/* ════════════════════════════════════════════════════════════════
   UI
   ════════════════════════════════════════════════════════════════ */
#define WM_SETSTATE (WM_USER+1)
#define ID_EDIT  101
#define ID_BTN   102
#define W_W 400
#define W_H 210

static COLORREF C(DWORD rgb){return RGB(rgb>>16,(rgb>>8)&0xFF,rgb&0xFF);}
#define CB_BG   0x0D0D0Du
#define CB_CARD 0x1A1A1Au
#define CB_BDR  0x2A2A2Au
#define CB_TXT  0xE0E0E0u
#define CB_MUT  0x888888u
#define CB_ACC  0x5C6BC0u
#define CB_OK   0x50DC78u
#define CB_FAIL 0xDC4646u
#define CB_WRN  0xF0A040u

static HWND  g_hwnd=NULL,g_hEdit=NULL,g_hBtn=NULL;
static HFONT g_fNorm=NULL,g_fBold=NULL;
static HBRUSH g_brEdit=NULL;

static char g_hwid[32]   ={};
static char g_msg[256]   ="Connecting...";
static int  g_msgCol     =0;
static int  g_showInput  =0;

static char   g_keyBuf[128]={};
static HANDLE g_keyEvent   =NULL;

static void _paint(HDC hdc,HWND hwnd) {
    RECT rc; GetClientRect(hwnd,&rc);
    HBRUSH bgBr=CreateSolidBrush(C(CB_BG));
    FillRect(hdc,&rc,bgBr); DeleteObject(bgBr);
    HPEN bdrPen=CreatePen(PS_SOLID,1,C(CB_BDR));
    SelectObject(hdc,bdrPen);
    SelectObject(hdc,GetStockObject(NULL_BRUSH));
    Rectangle(hdc,rc.left,rc.top,rc.right-1,rc.bottom-1);
    DeleteObject(bdrPen);
    HBRUSH cardBr=CreateSolidBrush(C(CB_CARD));
    RECT tr={0,0,rc.right,44}; FillRect(hdc,&tr,cardBr); DeleteObject(cardBr);
    HPEN sep=CreatePen(PS_SOLID,1,C(CB_BDR));
    SelectObject(hdc,sep);
    MoveToEx(hdc,0,44,NULL); LineTo(hdc,rc.right,44);
    DeleteObject(sep);
    SetBkMode(hdc,TRANSPARENT);
    SelectObject(hdc,g_fBold);
    SetTextColor(hdc,C(CB_TXT));
    TextOutA(hdc,18,14,"Moon Private",12);
    SelectObject(hdc,g_fNorm);
    SetTextColor(hdc,C(CB_MUT));
    TextOutA(hdc,108,17,"v2.0",4);
    SetTextColor(hdc,C(CB_MUT));
    TextOutA(hdc,rc.right-24,15,"x",1);
    SetTextColor(hdc,C(CB_MUT)); TextOutA(hdc,18,58,"HWID",4);
    SetTextColor(hdc,C(CB_TXT)); TextOutA(hdc,68,58,g_hwid,(int)strlen(g_hwid));
    COLORREF sc;
    switch(g_msgCol){case 1:sc=C(CB_OK);break;case 2:sc=C(CB_FAIL);break;
                      case 3:sc=C(CB_WRN);break;default:sc=C(CB_MUT);}
    SetTextColor(hdc,sc);
    RECT mr={18,82,rc.right-18,148};
    DrawTextA(hdc,g_msg,-1,&mr,DT_LEFT|DT_WORDBREAK);
    if (g_showInput){SetTextColor(hdc,C(CB_MUT));TextOutA(hdc,18,152,"License Key",11);}
}

static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        HINSTANCE hi=GetModuleHandleA(NULL);
        g_fNorm=CreateFontA(-13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
        g_fBold=CreateFontA(-14,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
        g_brEdit=CreateSolidBrush(RGB(0x11,0x11,0x11));
        g_keyEvent=CreateEventA(NULL,FALSE,FALSE,NULL);
        g_hEdit=CreateWindowExA(0,"EDIT","",
            WS_CHILD|ES_AUTOHSCROLL|ES_UPPERCASE,
            18,170,270,26,hwnd,(HMENU)ID_EDIT,hi,NULL);
        g_hBtn=CreateWindowA("BUTTON","Activate",
            WS_CHILD|BS_OWNERDRAW,
            296,170,86,26,hwnd,(HMENU)ID_BTN,hi,NULL);
        SendMessage(g_hEdit,WM_SETFONT,(WPARAM)g_fNorm,TRUE);
        SendMessage(g_hBtn, WM_SETFONT,(WPARAM)g_fNorm,TRUE);
        ShowWindow(g_hEdit,SW_HIDE); ShowWindow(g_hBtn,SW_HIDE);
        return 0;
    }
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        _paint(hdc,hwnd); EndPaint(hwnd,&ps); return 0;
    }
    case WM_DRAWITEM:{
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)lp;
        if (d->CtlID==ID_BTN){
            BOOL pr=(d->itemState&ODS_SELECTED);
            HBRUSH br=CreateSolidBrush(pr?C(0x4A56A0):C(CB_ACC));
            FillRect(d->hDC,&d->rcItem,br); DeleteObject(br);
            SetBkMode(d->hDC,TRANSPARENT);
            SetTextColor(d->hDC,RGB(255,255,255));
            SelectObject(d->hDC,g_fNorm);
            DrawTextA(d->hDC,"Activate",-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        return TRUE;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp,C(CB_TXT));
        SetBkColor((HDC)wp,RGB(0x11,0x11,0x11));
        return (LRESULT)g_brEdit;
    case WM_COMMAND:
        if (LOWORD(wp)==ID_BTN){
            GetWindowTextA(g_hEdit,g_keyBuf,sizeof(g_keyBuf));
            if (strlen(g_keyBuf)>=4) SetEvent(g_keyEvent);
        }
        return 0;
    case WM_SETSTATE:
        g_showInput=(int)wp;
        ShowWindow(g_hEdit,g_showInput?SW_SHOW:SW_HIDE);
        ShowWindow(g_hBtn, g_showInput?SW_SHOW:SW_HIDE);
        InvalidateRect(hwnd,NULL,TRUE); return 0;
    case WM_NCHITTEST:{
        LRESULT r=DefWindowProc(hwnd,msg,wp,lp);
        if (r==HTCLIENT){
            POINT pt={(SHORT)LOWORD(lp),(SHORT)HIWORD(lp)};
            ScreenToClient(hwnd,&pt);
            if (pt.y<=44) return HTCAPTION;
        }
        return r;
    }
    case WM_LBUTTONUP:{
        POINT pt={(SHORT)LOWORD(lp),(SHORT)HIWORD(lp)};
        RECT rc; GetClientRect(hwnd,&rc);
        if (pt.x>=rc.right-34&&pt.x<=rc.right-6&&pt.y>=6&&pt.y<=34)
            PostQuitMessage(0);
        return 0;
    }
    case WM_KEYDOWN: if(wp==VK_ESCAPE) PostQuitMessage(0); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}

static void _setMsg(const char *msg,int col,int showInput){
    strncpy(g_msg,msg,sizeof(g_msg)-1);
    g_msgCol=col;
    PostMessage(g_hwnd,WM_SETSTATE,(WPARAM)showInput,0);
    InvalidateRect(g_hwnd,NULL,TRUE);
}

/* ════════════════════════════════════════════════════════════════
   Worker thread
   ════════════════════════════════════════════════════════════════ */
static DWORD WINAPI WorkerThread(LPVOID) {
    char cheatKey[72]={};
    int  hasKey=_loadKey(cheatKey,sizeof(cheatKey));

    /* Key yoksa kullanicidan iste */
    if (!hasKey) {
        _setMsg("License key required.\nEnter your MOON-XXXX-XXXX-XXXX-XXXX key.",3,1);
        DWORD wr=WaitForSingleObject(g_keyEvent,10*60*1000);
        if (wr!=WAIT_OBJECT_0||strlen(g_keyBuf)<4){
            _setMsg("No key entered. Closing.",2,0);
            Sleep(2000); PostMessage(g_hwnd,WM_DESTROY,0,0); return 0;
        }
        strncpy(cheatKey,g_keyBuf,sizeof(cheatKey)-1);
        g_keyBuf[0]=0;
        _setMsg("Connecting...",0,0);
    }

    int tries=0;
    while (tries<25) {
        char dlTok[72]={},wsUrl[128]={};
        int r=_auth(cheatKey,g_hwid,dlTok,wsUrl);

        if (r==1) {
            /* ── Basarili → DLL indir + yukle ────────────────── */
            _saveKey(cheatKey);
            _setMsg("Downloading...",0,0);

            char wsHost[128]="moon-auth-service.moonsal.workers.dev";
            unsigned short wsPort=80;
            _parseWs(wsUrl,wsHost,sizeof(wsHost),&wsPort);

            DWORD sz=0;
            unsigned char *dll=_dl(dlTok,cheatKey,&sz);
            if (dll&&sz>0x200) {
                if (_map(dll,sz,cheatKey,wsHost,wsPort)) {
                    /* Bellek temizle */
                    volatile unsigned char *vp=dll;
                    for (DWORD vi=0;vi<sz;vi++) vp[vi]=0;
                    free(dll);

                    _setMsg("Active.",1,0);
                    Sleep(1500);
                    ShowWindow(g_hwnd,SW_HIDE);

                    /* Surecin canli kalmasi — DLL thread'leri burada calisiyor */
                    Sleep(INFINITE);
                    return 0;
                }
                free(dll);
            }
            _setMsg("Download error. Retrying...",3,0);
            Sleep(10000);
            tries++;
            continue;
        }

        if (r==-1) {
            /* HWID mismatch veya revoke */
            _delKey();
            _setMsg("Access denied — key revoked or HWID mismatch.",2,0);
            Sleep(3000); PostMessage(g_hwnd,WM_DESTROY,0,0); return 0;
        }

        if (r==-2) {
            /* Gecersiz key — sil ve tekrar iste */
            _delKey();
            cheatKey[0]=0;
            _setMsg("Invalid key. Enter a valid license key.",2,1);
            DWORD wr=WaitForSingleObject(g_keyEvent,10*60*1000);
            if (wr!=WAIT_OBJECT_0||strlen(g_keyBuf)<4){
                PostMessage(g_hwnd,WM_DESTROY,0,0); return 0;
            }
            strncpy(cheatKey,g_keyBuf,sizeof(cheatKey)-1);
            g_keyBuf[0]=0;
            tries=0;
            _setMsg("Connecting...",0,0);
            continue;
        }

        /* r==0: baglanti hatasi */
        char buf[128];
        snprintf(buf,sizeof(buf),"Connecting to server... (%d/25)",tries+1);
        _setMsg(buf,0,0);
        Sleep(15000);
        tries++;
    }

    _setMsg("Could not reach server. Check your connection.",2,0);
    Sleep(INFINITE);
    return 0;
}

/* ════════════════════════════════════════════════════════════════
   WinMain
   ════════════════════════════════════════════════════════════════ */
int WINAPI WinMain(HINSTANCE hi,HINSTANCE,LPSTR,int){
    _hwid(g_hwid,sizeof(g_hwid));

    WNDCLASSEXA wc={};
    wc.cbSize       =sizeof(wc);
    wc.style        =CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc  =WndProc;
    wc.hInstance    =hi;
    wc.hCursor      =LoadCursor(NULL,IDC_ARROW);
    wc.lpszClassName="MoonLoader";
    RegisterClassExA(&wc);

    int sx=GetSystemMetrics(SM_CXSCREEN), sy=GetSystemMetrics(SM_CYSCREEN);
    g_hwnd=CreateWindowExA(WS_EX_TOOLWINDOW,"MoonLoader","Moon Private",
        WS_POPUP|WS_VISIBLE,
        (sx-W_W)/2,(sy-W_H)/2,W_W,W_H,
        NULL,NULL,hi,NULL);

    HANDLE wt=CreateThread(NULL,0,WorkerThread,NULL,0,NULL);
    if (wt) CloseHandle(wt);

    MSG m;
    while (GetMessage(&m,NULL,0,0)){
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
