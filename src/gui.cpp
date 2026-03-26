#include "gui.h"
#include "plugin.h"
#include "midi_export.h"
#include <atomic>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <shlwapi.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

static const wchar_t* WINDOW_CLASS_NAME = L"MidiCaptureGUI";
static std::atomic<int> sInstanceCount{0};
static bool sClassRegistered = false;

static HMODULE getMyModule() {
    HMODULE h = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&getMyModule, &h);
    return h;
}

// ============================================================
// OLE Drag & Drop Implementation
// ============================================================

class MidiDropSource : public IDropSource {
public:
    MidiDropSource() : _refCount(1) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&_refCount); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG res = InterlockedDecrement(&_refCount);
        if (res == 0) delete this;
        return res;
    }
    STDMETHODIMP QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override {
        if (fEscapePressed) return DRAGDROP_S_CANCEL;
        if (!(grfKeyState & (MK_LBUTTON | MK_RBUTTON))) return DRAGDROP_S_DROP;
        return S_OK;
    }
    STDMETHODIMP GiveFeedback(DWORD dwEffect) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
private:
    ULONG _refCount;
};

class MidiDataObject : public IDataObject {
public:
    MidiDataObject(const wchar_t* filePath) : _refCount(1) {
        size_t len = wcslen(filePath) + 1;
        _hDrop = GlobalAlloc(GHND, sizeof(DROPFILES) + (len + 1) * sizeof(wchar_t));
        if (_hDrop) {
            DROPFILES* df = (DROPFILES*)GlobalLock(_hDrop);
            df->pFiles = sizeof(DROPFILES);
            df->fWide = TRUE;
            wchar_t* p = (wchar_t*)(df + 1);
            memcpy(p, filePath, len * sizeof(wchar_t));
            p[len] = L'\0'; // double null termination
            GlobalUnlock(_hDrop);
        }
    }
    ~MidiDataObject() { if (_hDrop) GlobalFree(_hDrop); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&_refCount); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG res = InterlockedDecrement(&_refCount);
        if (res == 0) delete this;
        return res;
    }
    STDMETHODIMP GetData(FORMATETC* pfe, STGMEDIUM* pmedium) override {
        if (pfe->cfFormat == CF_HDROP && (pfe->tymed & TYMED_HGLOBAL)) {
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = duplicateGlobalMem(_hDrop);
            pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }
    STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    STDMETHODIMP QueryGetData(FORMATETC* pfe) override {
        return (pfe->cfFormat == CF_HDROP) ? S_OK : S_FALSE;
    }
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    STDMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP EnumFormatEtc(DWORD dwDir, IEnumFORMATETC** ppefe) override {
        if (dwDir == DATADIR_GET) return SHCreateStdEnumFmtEtc(1, &_fe, ppefe);
        return E_NOTIMPL;
    }
    STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    HGLOBAL duplicateGlobalMem(HGLOBAL h) {
        size_t size = GlobalSize(h);
        HGLOBAL hNew = GlobalAlloc(GHND, size);
        if (hNew) {
            void *src = GlobalLock(h), *dest = GlobalLock(hNew);
            memcpy(dest, src, size);
            GlobalUnlock(h); GlobalUnlock(hNew);
        }
        return hNew;
    }
    ULONG _refCount;
    HGLOBAL _hDrop;
    FORMATETC _fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
};

static void prepareMidiFile(GuiState* gs, wchar_t* outFilePath) {
    if (!gs || !gs->plugin) return;
    static constexpr uint32_t MAX_EXPORT = 600000;
    auto* events = new TimestampedMidiEvent[MAX_EXPORT];
    uint32_t count = gs->plugin->snapshotEvents(events, MAX_EXPORT);
    if (count == 0) {
        delete[] events;
        outFilePath[0] = L'\0';
        return;
    }

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    wsprintfW(outFilePath, L"%sMidiCapture.mid", tempPath);
    char narrowPath[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, outFilePath, -1, narrowPath, MAX_PATH, nullptr, nullptr);
    MidiExport::writeFile(narrowPath, events, count);
    delete[] events;
}

static void startDrag(GuiState* gs) {
    wchar_t filePath[MAX_PATH];
    prepareMidiFile(gs, filePath);
    if (filePath[0] == L'\0') {
        SetWindowTextW(gs->statusLabel, L"Play some notes first!");
        gs->messageTimeout = 5;
        return;
    }

    MidiDataObject* dataObj = new MidiDataObject(filePath);
    MidiDropSource* dropSrc = new MidiDropSource();
    DWORD effect = 0;
    DoDragDrop(dataObj, dropSrc, DROPEFFECT_COPY | DROPEFFECT_MOVE, &effect);
    dataObj->Release();
    dropSrc->Release();
}

// Button subclass to detect drag start
static LRESULT CALLBACK buttonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    HWND parent = GetParent(hwnd);
    GuiState* gs = (GuiState*)GetWindowLongPtrW(parent, GWLP_USERDATA);

    switch (msg) {
    case WM_LBUTTONDOWN:
        if (gs) {
            gs->dragStartPoint.x = (short)LOWORD(lParam);
            gs->dragStartPoint.y = (short)HIWORD(lParam);
            gs->isDragging = true;
            SetCapture(hwnd);
        }
        break;
    case WM_MOUSEMOVE:
        if (gs && gs->isDragging) {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            if (abs(pt.x - gs->dragStartPoint.x) > GetSystemMetrics(SM_CXDRAG) ||
                abs(pt.y - gs->dragStartPoint.y) > GetSystemMetrics(SM_CYDRAG)) {
                gs->isDragging = false;
                ReleaseCapture();
                startDrag(gs);
                return 0; // Don't let button handle the click
            }
        }
        break;
    case WM_LBUTTONUP:
        if (gs && gs->isDragging) {
            gs->isDragging = false;
            ReleaseCapture();
        }
        break;
    }

    if (gs && gs->originalButtonProc)
        return CallWindowProcW(gs->originalButtonProc, hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    GuiState* gs = (GuiState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 1000, nullptr);
        break;
    case WM_TIMER:
        if (gs && gs->messageTimeout > 0) {
            gs->messageTimeout--;
            if (gs->messageTimeout == 0) {
                SetWindowTextW(gs->statusLabel, L"Midi Capture Active!");
            }
        }
        break;
    case WM_COMMAND:
        if (gs && LOWORD(wParam) == 2) { // Clear Button ID
            gs->plugin->reset();
            SetWindowTextW(gs->statusLabel, L"Memory cleared!");
            gs->messageTimeout = 5;
        }
        break;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(220, 220, 220));
        SetBkColor(hdc, RGB(40, 40, 40));
        if (gs && gs->bgBrush) return (LRESULT)gs->bgBrush;
        return (LRESULT)GetStockObject(DC_BRUSH);
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH brush = CreateSolidBrush(RGB(40, 40, 40));
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        return 1;
    }
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        break;
    case WM_NCDESTROY:
        if (gs) gs->hwnd = nullptr;
        break;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

static bool gui_is_api_supported(const clap_plugin_t* plugin, const char* api, bool is_floating) {
    return !is_floating && strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
}
static bool gui_get_preferred_api(const clap_plugin_t* plugin, const char** api, bool* is_floating) {
    *api = CLAP_WINDOW_API_WIN32; *is_floating = false; return true;
}
static bool gui_create(const clap_plugin_t* plugin, const char* api, bool is_floating) {
    if (is_floating || strcmp(api, CLAP_WINDOW_API_WIN32) != 0) return false;
    if (!sClassRegistered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = windowProc;
        wc.hInstance = getMyModule();
        wc.lpszClassName = WINDOW_CLASS_NAME;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        if (RegisterClassExW(&wc)) sClassRegistered = true;
    }
    MidiCapture* cap = static_cast<MidiCapture*>(plugin->plugin_data);
    if (!cap->_guiState) cap->_guiState = new GuiState();
    cap->_guiState->plugin = cap;
    cap->_guiState->created = true;
    sInstanceCount++;
    return true;
}
static void gui_destroy(const clap_plugin_t* plugin) {
    MidiCapture* cap = static_cast<MidiCapture*>(plugin->plugin_data);
    if (cap && cap->_guiState) {
        auto* gs = cap->_guiState;
        if (gs->hwnd) {
            if (gs->dragButton && gs->originalButtonProc) {
                SetWindowLongPtrW(gs->dragButton, GWLP_WNDPROC, (LONG_PTR)gs->originalButtonProc);
                gs->originalButtonProc = nullptr;
            }
            // Explicitly destroy children first
            if (gs->dragButton) { DestroyWindow(gs->dragButton); gs->dragButton = nullptr; }
            if (gs->clearButton) { DestroyWindow(gs->clearButton); gs->clearButton = nullptr; }
            if (gs->statusLabel) { DestroyWindow(gs->statusLabel); gs->statusLabel = nullptr; }
            
            DestroyWindow(gs->hwnd);
            gs->hwnd = nullptr;
        }
        if (gs->font) { DeleteObject(gs->font); gs->font = nullptr; }
        if (gs->smallFont) { DeleteObject(gs->smallFont); gs->smallFont = nullptr; }
        if (gs->bgBrush) { DeleteObject(gs->bgBrush); gs->bgBrush = nullptr; }
        delete gs;
        cap->_guiState = nullptr;
    }
    if (--sInstanceCount == 0 && sClassRegistered) {
        UnregisterClassW(WINDOW_CLASS_NAME, getMyModule());
        sClassRegistered = false;
    }
}

static bool gui_set_scale(const clap_plugin_t* plugin, double scale) { return true; }
static bool gui_get_size(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) {
    *width = 300; *height = 150; return true;
}
static bool gui_can_resize(const clap_plugin_t* plugin) { return false; }
static bool gui_get_resize_hints(const clap_plugin_t* plugin, clap_gui_resize_hints_t* hints) { return false; }
static bool gui_adjust_size(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) {
    *width = 300; *height = 150; return true;
}
static bool gui_set_size(const clap_plugin_t* plugin, uint32_t width, uint32_t height) {
    MidiCapture* cap = static_cast<MidiCapture*>(plugin->plugin_data);
    if (cap && cap->_guiState && cap->_guiState->hwnd) 
        SetWindowPos(cap->_guiState->hwnd, nullptr, 0, 0, (int)width, (int)height, SWP_NOMOVE | SWP_NOZORDER);
    return true;
}
static bool gui_set_parent(const clap_plugin_t* plugin, const clap_window_t* window) {
    MidiCapture* cap = static_cast<MidiCapture*>(plugin->plugin_data);
    if (!cap || !cap->_guiState) return false;
    auto* gs = cap->_guiState;

    HWND parent = (HWND)window->win32;
    gs->hwnd = CreateWindowExW(0, WINDOW_CLASS_NAME, L"MidiCapture", WS_CHILD | WS_VISIBLE,
        0, 0, 300, 150, parent, nullptr, getMyModule(), nullptr);
    if (!gs->hwnd) return false;
    SetWindowLongPtrW(gs->hwnd, GWLP_USERDATA, (LONG_PTR)gs);

    gs->bgBrush = CreateSolidBrush(RGB(40, 40, 40));
    gs->font = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    // Drag Button
    gs->dragButton = CreateWindowExW(0, L"BUTTON", L"\xD83D\xDCCB  DRAG MIDI",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 15, 15, 210, 60, gs->hwnd, (HMENU)1, getMyModule(), nullptr);
    if (gs->dragButton && gs->font) {
        SendMessageW(gs->dragButton, WM_SETFONT, (WPARAM)gs->font, TRUE);
        gs->originalButtonProc = (WNDPROC)SetWindowLongPtrW(gs->dragButton, GWLP_WNDPROC, (LONG_PTR)buttonProc);
    }

    // Clear Button (Trashcan symbol L"\xD83D\xDDD1")
    gs->clearButton = CreateWindowExW(0, L"BUTTON", L"\xD83D\xDDD1",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 235, 15, 50, 60, gs->hwnd, (HMENU)2, getMyModule(), nullptr);
    if (gs->clearButton && gs->font) {
        SendMessageW(gs->clearButton, WM_SETFONT, (WPARAM)gs->font, TRUE);
    }

    gs->statusLabel = CreateWindowExW(0, L"STATIC", L"Midi Capture Active!",
        WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 90, 260, 50, gs->hwnd, nullptr, getMyModule(), nullptr);
    if (gs->statusLabel) {
        gs->smallFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        SendMessageW(gs->statusLabel, WM_SETFONT, (WPARAM)gs->smallFont, TRUE);
    }
    return true;
}

static bool gui_set_transient(const clap_plugin_t* plugin, const clap_window_t* window) { return false; }
static void gui_suggest_title(const clap_plugin_t* plugin, const char* title) {}
static bool gui_show(const clap_plugin_t* plugin) {
    MidiCapture* cap = static_cast<MidiCapture*>(plugin->plugin_data);
    if (cap && cap->_guiState && cap->_guiState->hwnd) 
        ShowWindow(cap->_guiState->hwnd, SW_SHOW);
    return true;
}
static bool gui_hide(const clap_plugin_t* plugin) {
    MidiCapture* cap = static_cast<MidiCapture*>(plugin->plugin_data);
    if (cap && cap->_guiState && cap->_guiState->hwnd) 
        ShowWindow(cap->_guiState->hwnd, SW_HIDE);
    return true;
}

static const clap_plugin_gui_t sGuiExtension = {
    gui_is_api_supported, gui_get_preferred_api, gui_create, gui_destroy,
    gui_set_scale, gui_get_size, gui_can_resize, gui_get_resize_hints,
    gui_adjust_size, gui_set_size, gui_set_parent, gui_set_transient,
    gui_suggest_title, gui_show, gui_hide,
};
#else
static const clap_plugin_gui_t sGuiExtension = {};
#endif

namespace MidiCaptureGui {
    const clap_plugin_gui_t* getExtension() { return &sGuiExtension; }
}
