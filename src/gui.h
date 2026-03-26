#pragma once

#include <clap/clap.h>

#ifdef _WIN32
#include <windows.h>
#endif

// Forward declare
class MidiCapture;

#ifdef _WIN32
struct GuiState {
    MidiCapture* plugin = nullptr;
    HWND hwnd = nullptr;
    HWND dragButton = nullptr;
    HWND clearButton = nullptr;
    HWND statusLabel = nullptr;
    HFONT font = nullptr;
    HFONT smallFont = nullptr;
    HBRUSH bgBrush = nullptr;
    POINT dragStartPoint = {0,0};
    bool isDragging = false;
    bool created = false;
    WNDPROC originalButtonProc = nullptr;
    int messageTimeout = 0; // counts seconds until reverting to default
};
#endif

namespace MidiCaptureGui {
    const clap_plugin_gui_t* getExtension();
}
