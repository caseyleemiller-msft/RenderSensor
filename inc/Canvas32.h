// Window display system using SFML
#pragma once

#ifndef __Canvas32_h__
#define __Canvas32_h__

#include <assert.h>
#include <stdint.h> // int32_t, etc

#include "Canvas.h"

class Canvas32 : public Canvas
{
public:
    Canvas32(
        int32_t width,
        int32_t height,
        uint32_t* pFB = nullptr) : // optional externally provided memory buffer
        Canvas(width, height)
    {
        // alloc pointers to rows
        m_pFB = new uint32_t* [height];
     
        // point first row to contiguous memory block
        int32_t numPix = width * height;
        if (pFB == nullptr)
        {
            m_externalFB = false;
            m_pFB[0] = new uint32_t [numPix];
        }
        else
        {
            m_externalFB = true;
            m_pFB[0] = pFB;
        }

        // set pointers to remaining rows
        for (int i = 1; i < height; ++i) { m_pFB[i] = m_pFB[i - 1] + width; }
    }

    ~Canvas32(void)
    {
        if (m_externalFB == false) { delete [] m_pFB[0]; } // delete memory block
        delete [] m_pFB;                                   // delete ptrs to rows
    }

    // set entire framebuffer to one color
    void SetCanvas(uint32_t color)
    {
        uint32_t sz = m_width * m_height;

        uint32_t R = (color      ) & 0xFFu;
        uint32_t G = (color >>  8) & 0xFFu;
        uint32_t B = (color >> 16) & 0xFFu;
        if ((R == G) && (G == B)) // if gray color...
        {
            memset(m_pFB[0], (int)R, sz * sizeof(uint32_t)); // use fast method
        }
        else // else use slower method
        {
            uint32_t* p = m_pFB[0];
            while (sz--) { *p++ = color; }
        }
    }

    // Set a pixel (without bounds checking in release builds)
    void SetPixel(int32_t X, int32_t Y, uint32_t color)
    {
        assert((0 <= X) && (X < m_width));   // bounds check
        assert((0 <= Y) && (Y < m_height));

        m_pFB[Y][X] = color; // no noise output
    }

    //uint32_t GetPixel(int32_t X, int32_t Y) { return m_pFB[Y][X]; }

    void* GetFrameBuffer(void) { return m_pFB[0]; }

private:
    bool       m_externalFB;
    uint32_t** m_pFB;
};

#endif
