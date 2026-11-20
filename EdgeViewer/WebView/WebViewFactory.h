#pragma once

// The factory is Windows-only (Decision 4): on Windows DllMain.cpp uses
// CreateWebView() to queue WebView2 init; on Linux EdgeLister_Linux.cpp
// constructs the WebKitBackend directly and there is no factory step, so
// this header compiles to nothing there.
#ifdef _WIN32

#include <windows.h>  // for HRESULT on Windows
#include <string>

class ProcessorInterface;

//------------------------------------------------------------------------
// Platform abstraction. On Windows returns a WebView2Backend; on Linux
// returns a WebKitBackend. The factory is the only source-tree place
// where the choice is made (Decision 4).
//
// The factory queues creation; it does NOT return a fully-populated
// IWebView*. WebView2's CreateCoreWebView2EnvironmentWithOptions is
// asynchronous: the synchronous return value only indicates whether
// the call was successfully *queued*. The actual environment + controller
// become available via the completion callbacks, which:
//
//   - on success: move the IWebView shared_ptr into gs_Views[hWnd]
//     (the map owns the lifetime), then run the initial Navigate
//     via Navigator
//   - on failure: DestroyWindow(hWnd)
//
// DllMain should therefore treat CreateWebView's return value as the
// synchronous HRESULT of "was the call queued?" — if FAILED, destroy
// the HWND and return null; if OK, return the HWND to TC and let the
// async callback finish the job.
HRESULT CreateWebView(void* parentWindow,
                      const std::wstring& fileToLoad,
                      const ProcessorInterface* processor);
//------------------------------------------------------------------------
// PDF key routing: the view shows a PDF (true) or not / is closed (false).
// Keeps the low-level key hook installed while any view shows a PDF.
void SetPdfKeyRouting(HWND hWnd, bool isPdf);
// Quick-view Tab: move the focus to the parent and send Tab to Total Commander.
void ForwardTabToParent(HWND hWnd);
//------------------------------------------------------------------------

#endif // _WIN32
