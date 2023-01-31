#pragma once

#ifndef __GdiWindow_h__
#define __GdiWindow_h__

#include <assert.h>
#include <wingdi.h> // GDI::SetPixel()
#include <vector>
#include <tuple>

#include "Canvas.h"
#include "BaseGdiWindow.h"

// Note: order of inheritance is important!  BaseGdiWindow must come first.
class GdiWindow : public BaseGdiWindow<GdiWindow>, public Canvas
{
public:
    GdiWindow(
        int32_t width,
        int32_t height) :
        Canvas(width, height),
        m_hbm(NULL),
        m_hdcMem(NULL),
        m_pRgb(NULL)
    {
        assert((width & 0x1) == 0); // bitmaps require even width
    }

    // BaseGdiWindow functions--------------------------------------------------
    PCWSTR  ClassName() const { return L"GDI Window Example"; }
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Canvas functions---------------------------------------------------------

    // set entire framebuffer to one color
    void SetCanvas(uint32_t color)
    {
        uint32_t sz = m_width * m_height;
        uint32_t R = (color >> 16) & 0xFFu;
        uint32_t G = (color >>  8) & 0xFFu;
        uint32_t B = (color      ) & 0xFFu;
        if ((R == G) && (G == B)) // if gray color...
        {
            memset(m_pRgb, (int)R, sz * sizeof(uint32_t)); // use fast method
        }
        else // else use slower method
        {
            uint32_t* p = m_pRgb;
            while (sz--) { *p++ = color; }
        }
    }

    // Set a pixel
    void SetPixel(int32_t X, int32_t Y, uint32_t color)
    {
        // If using 32-bit display, the following is equivalent to the GDI function:
        //     ::SetPixel(m_hdcMem, X, Y, color)
        m_pRgb[Y * m_width + X] = color;
    }

    void* GetFrameBuffer(void) { return m_pRgb; }

    // GdiWindow functions------------------------------------------------------

    // write text to framebuffer
    void SetText(std::string& Msg, int X, int Y, uint32_t color)
    {
        // TODO: makes a copy of Msg for make_tuple and again for push_back?
        m_text.push_back(std::make_tuple(Msg, X, Y, color));
    }

    // Display current framebuffer in window
    bool Update(void)
    {
        // Invalidate entire window, causes WM_PAINT message
        InvalidateRect(m_hwnd, NULL, FALSE);

        // Handle window events
        MSG msg = { };
        BOOL retVal = GetMessage(&msg, NULL, 0, 0);
        if (retVal > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return (retVal > 0);
    }

private:
    HBITMAP   m_hbm;    // handle of bitmap
    HDC       m_hdcMem; // memory device context
    uint32_t* m_pRgb;   // bitmap data: array of R,G,B byte per pixel

    std::vector<std::tuple<std::string, int, int, uint32_t>> m_text;
};

////////////////////////////////////////////////////////////////////////////////
LRESULT GdiWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static int winWidth, winHeight;

    switch (uMsg)
    {
    case WM_CREATE:
        {
            // Create DC and bitmap that is compatible with window
            HDC hdcWin = GetDC(m_hwnd);
            m_hdcMem = CreateCompatibleDC(hdcWin);
            ReleaseDC(m_hwnd, hdcWin);

            // Next line doesn't allow access to the pixel bits
            //m_hbm  = CreateCompatibleBitmap(m_hdcMem, m_width, m_height);

            // Create a bitmap with a pointer to pixel data so external code
            // can draw to the bitmap.
            BITMAPINFO bmi;
            memset(&bmi, 0, sizeof(BITMAPINFO));
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       =  m_width;
            bmi.bmiHeader.biHeight      = -m_height; // negative for top-down
            bmi.bmiHeader.biPlanes      = 1;          // must be 1
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;     // uncompressed RGB
            m_hbm = CreateDIBSection(m_hdcMem, &bmi, DIB_RGB_COLORS, (void**)&m_pRgb, NULL, 0);

            // attach bitmap to memory display context
            SelectObject(m_hdcMem, m_hbm);

            // retrieve bitmap struct for stride
            //GetObject(m_hbm, sizeof(BITMAP), (LPSTR)&m_bmp);

            return 0;
        }

    case WM_SIZE: // track window size
        winWidth  = LOWORD(lParam);
        winHeight = HIWORD(lParam);
        return 0;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdcWin = BeginPaint(m_hwnd, &ps);

            // Copy bitmap to window with resizing
            StretchBlt(hdcWin,              // hdcDst
                       0, 0,                // x, y : upper left corner of dst rect
                       winWidth, winHeight, // width, height of dst
                       m_hdcMem,            // hdcSrc
                       0, 0,                // x1, y1 : upper left corner of src rect
                       m_width, m_height, // width and height of src
                       SRCCOPY);            // raster op code, SRCCOPY = copy directly to dst

            // Draw strings
            for (int ii = 0; ii < m_text.size(); ++ii)
            {
                std::string Msg   = std::get<0>(m_text[ii]);
                int         X     = std::get<1>(m_text[ii]);
                int         Y     = std::get<2>(m_text[ii]);
                uint32_t    color = std::get<3>(m_text[ii]);
                SetTextColor(hdcWin, color);
                ExtTextOutA(hdcWin,             // window handle
                            X, Y,
                            ETO_IGNORELANGUAGE, // options
                            NULL,               // optional RECT for clipping
                            Msg.c_str(),        // lpString
                            (UINT)Msg.length(), // UINT length of string
                            NULL);              // optional array of distance
                                                // between chars
            }
            m_text.clear();

            EndPaint(m_hwnd, &ps);
            return 0;
        }

    case WM_DESTROY:
        DeleteDC(m_hdcMem);
        DeleteObject(m_hbm);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
}

#endif
