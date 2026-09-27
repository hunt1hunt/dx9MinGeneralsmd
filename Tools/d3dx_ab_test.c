#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

typedef long (__stdcall *PFN_D3DXCompileShader)(
    const char* pSrc, unsigned int SrcLen,
    void* pDefines, void* pInclude,
    const char* pFunctionName, const char* pProfile, unsigned long Flags,
    void** ppShader, void** ppErrorMsgs, void** ppConstantTable);

/* minimal COM buffer vtable access */
typedef unsigned long (__stdcall *PFN_AddRef)(void*);
typedef unsigned long (__stdcall *PFN_Release)(void*);
typedef void*      (__stdcall *PFN_GetBufferPointer)(void*);
typedef unsigned long (__stdcall *PFN_GetBufferSize)(void*);

static void* BufPtr(void* p)
{
    void** vtbl = *(void***)p;
    PFN_GetBufferPointer f = (PFN_GetBufferPointer)vtbl[3];
    return f(p);
}
static unsigned long BufSize(void* p)
{
    void** vtbl = *(void***)p;
    PFN_GetBufferSize f = (PFN_GetBufferSize)vtbl[4];
    return f(p);
}
static void BufRelease(void* p)
{
    void** vtbl = *(void***)p;
    PFN_Release f = (PFN_Release)vtbl[2];
    f(p);
}

int main(int argc, char** argv)
{
    /* argv[1] = hlsl file, argv[2] = profile (default ps_2_a) */
    FILE* f;
    FILE* o;
    long sz;
    char* src;
    HMODULE h;
    PFN_D3DXCompileShader D3DXCompileShader_;
    const char* profile;
    void* pOut = NULL; void* pErr = NULL;
    long hr;
    char outname[512];
    if (argc < 2) { printf("usage: d3dx_ab_test <file.hlsl> [profile]\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    src = (char*)malloc(sz + 1);
    fread(src, 1, sz, f); src[sz] = 0; fclose(f);

    h = LoadLibraryA("d3dx9_42.dll");
    if (!h) { printf("LoadLibrary d3dx9_42 failed %lu\n", GetLastError()); return 3; }
    D3DXCompileShader_ =
        (PFN_D3DXCompileShader)GetProcAddress(h, "D3DXCompileShader");
    if (!D3DXCompileShader_) { printf("no proc\n"); return 3; }

    profile = (argc > 2) ? argv[2] : "ps_2_a";
    hr = D3DXCompileShader_(src, sz, NULL, NULL, "main", profile, 0, &pOut, &pErr, NULL);
    printf("%s [%s] hr=0x%08lx size=%lu\n", argv[1], profile, hr,
        pOut ? BufSize(pOut) : 0);
    if (pErr) {
        const char* e = (const char*)BufPtr(pErr);
        printf("ERR: %.400s\n", e);
        BufRelease(pErr);
    }
    if (pOut) {
        char outname[512];
        sprintf(outname, "%s.cso", argv[1]);
        FILE* o = fopen(outname, "wb");
        fwrite(BufPtr(pOut), 1, BufSize(pOut), o);
        fclose(o);
        BufRelease(pOut);
    }
    return (hr == 0) ? 0 : 1;
}
