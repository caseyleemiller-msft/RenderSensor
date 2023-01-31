#pragma once

#ifndef __BaseGdiWindow_h__
#define __BaseGdiWindow_h__

// From: https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/Win7Samples/begin/LearnWin32/Direct2DCircle/cpp/basewin.h

template <class DERIVED_TYPE>
class BaseGdiWindow
{
public:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        DERIVED_TYPE *pThis = NULL;
        if (uMsg == WM_NCCREATE) // one time window creation, before window is visible
        {
            // Set ptr to instance passed as last arg of CreateWindowEx()
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pThis = (DERIVED_TYPE*)pCreate->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

            pThis->m_hwnd = hwnd; // Need as CreateWindowEx() hasn't returned yet
        }
        else
        {
            // Retrieve pointer to class instance
            pThis = (DERIVED_TYPE*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }
        if (pThis) { return pThis->HandleMessage(uMsg, wParam, lParam); }
        else       { return  DefWindowProc(hwnd, uMsg, wParam, lParam); }
    }

    BaseGdiWindow() : m_hwnd(NULL) { }

    BOOL Create(
        PCWSTR lpWindowName,
        DWORD  dwStyle,
        DWORD  dwExStyle  = 0,
        int    x          = CW_USEDEFAULT,
        int    y          = CW_USEDEFAULT,
        int    nWidth     = CW_USEDEFAULT,
        int    nHeight    = CW_USEDEFAULT,
        HWND   hWndParent = 0,
        HMENU  hMenu      = 0)
    {
        WNDCLASS wc = {0};
        wc.style         = CS_HREDRAW | CS_VREDRAW; // redraw with horz or vert resize
        wc.lpfnWndProc   = DERIVED_TYPE::WindowProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH) GetStockObject(BLACK_BRUSH);
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = ClassName();

        if (!RegisterClass(&wc))
        {
            MessageBox(NULL, TEXT("Failed to register window class!"),
                    ClassName(), MB_ICONERROR);
            return FALSE;
        }

        // Create the window, not visible yet
        m_hwnd = CreateWindowEx(dwExStyle,
                                ClassName(),
                                lpWindowName,
                                dwStyle,
                                x, y,
                                nWidth, nHeight,
                                hWndParent,
                                hMenu,
                                GetModuleHandle(NULL),
                                this);

        return (m_hwnd ? TRUE : FALSE);
    }

    HWND Window() const { return m_hwnd; }

protected:
    virtual PCWSTR  ClassName() const = 0;
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) = 0;

    HWND m_hwnd;
};

#endif
