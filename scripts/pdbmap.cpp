// pdbmap — map module RVAs to nearest function names using MS DIA.
// Usage: pdbmap <file.pdb> <imageBaseHex> <rvaHex> [<rvaHex> ...]
// Prints  "0x<RVA> -> <name>+0x<disp>"  per RVA.
//
// Build (x64, from the repo root after vcvars64.bat):
//   cl /nologo /std:c++17 /EHsc scripts\pdbmap.cpp ^
//       /I "C:\Program Files\Microsoft Visual Studio\18\Community\DIA SDK\include" ^
//       ole32.lib ^
//       "C:\Program Files\Microsoft Visual Studio\18\Community\DIA SDK\lib\amd64\diaguids.lib" ^
//       /link /out:build\pdbmap.exe
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include "dia2.h"

static std::wstring nameOf (IDiaSymbol* s)
{
    BSTR b = nullptr;
    s->get_name (&b);
    const std::wstring r = (b != nullptr) ? std::wstring (b) : std::wstring (L"?");
    if (b != nullptr) SysFreeString (b);
    return r;
}

// Nearest function/global symbol with RVA <= target, or null. Uses the
// by-address enumerator so a target inside a function resolves to that
// function regardless of exact-tag matching.
static void nearestFunction (IDiaSession* sess, DWORD targetRva, IDiaSymbol*& outBest, DWORD& outBestRva)
{
    outBest = nullptr; outBestRva = 0;
    IDiaEnumSymbolsByAddr* en = nullptr;
    if (sess->getSymbolsByAddr (&en) != S_OK || en == nullptr) return;

    for (;;)
    {
        IDiaSymbol* sym = nullptr;
        ULONG fetched = 0;
        if (en->Next (1, &sym, &fetched) != S_OK || sym == nullptr) break;

        DWORD tag = 0;
        sym->get_symTag (&tag);
        DWORD rva = 0;
        sym->get_relativeVirtualAddress (&rva);

        if ((tag == SymTagFunction || tag == SymTagThunk || tag == SymTagPublicSymbol)
            && rva <= targetRva && rva >= outBestRva)
        {
            if (outBest != nullptr) outBest->Release();
            outBest = sym;
            outBestRva = rva;
            if (rva == targetRva) { en->Release(); return; }
        }
        sym->Release();
    }
    en->Release();
}

int wmain (int argc, wchar_t** argv)
{
    setvbuf (stdout, nullptr, _IONBF, 0);
    setvbuf (stderr, nullptr, _IONBF, 0);
    if (argc < 4)
    {
        fwprintf (stderr, L"usage: pdbmap <file.pdb> <imageBaseHex> <rvaHex> [<rvaHex> ...]\n");
        return 1;
    }

    fwprintf (stderr, L"[1] coinit\n");
    if (FAILED (CoInitialize (nullptr)))
        return 10;
    fwprintf (stderr, L"[2] cocreate\n");

    IDiaDataSource* src = nullptr;
    HRESULT hr = CoCreateInstance (CLSID_DiaSource, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IDiaDataSource, reinterpret_cast<void**> (&src));
    if (FAILED (hr) || src == nullptr)
    {
        fwprintf (stderr, L"CoCreateInstance(DiaSource) failed hr=0x%08X\n", hr);
        return 2;
    }

    fwprintf (stderr, L"[3] load %ls\n", argv[1]);
    hr = src->loadDataFromPdb (argv[1]);
    if (FAILED (hr))
    {
        fwprintf (stderr, L"loadDataFromPdb failed hr=0x%08X (%ls)\n", hr, argv[1]);
        return 3;
    }

    IDiaSession* sess = nullptr;
    if (src->openSession (&sess) != S_OK || sess == nullptr)
    {
        fwprintf (stderr, L"openSession failed\n");
        return 4;
    }

    const ULONGLONG base = _wcstoui64 (argv[2], nullptr, 16);
    for (int a = 3; a < argc; ++a)
    {
        const DWORD rva = static_cast<DWORD> (wcstoul (argv[a], nullptr, 16));

        // Nearest function symbol; if none, fall back to the whole-symbol
        // set (data symbols) at the same logic.
        IDiaSymbol* best = nullptr;
        DWORD bestRva = 0;
        nearestFunction (sess, rva, best, bestRva);

        if (best != nullptr)
        {
            fwprintf (stderr, L"0x%X -> %ls+0x%X\n", rva, nameOf (best).c_str(), rva - bestRva);
            best->Release();
        }
        else
        {
            fwprintf (stderr, L"0x%X -> (no symbol)\n", rva);
        }
        (void) base;
    }

    sess->Release();
    src->Release();
    CoUninitialize();
    return 0;
}