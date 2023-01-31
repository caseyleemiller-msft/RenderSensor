#pragma once

#ifndef __Canvas_h__
#define __Canvas_h__

#include <stdint.h> // int32_t, etc

// Interface class to abstract data type of a 2D drawing canvas.
// Note: uint32_t color type is considered generic enough to work with all pixel
// types (e.g. 8, 16, 24 or 32-bit grayscale, RGBA, floats, etc)
class Canvas
{
public:
    Canvas(int32_t width, int32_t height) : m_width(width), m_height(height) { }
    ~Canvas(void) { }

    // set entire canvas to one color
    virtual void SetCanvas(uint32_t color) = 0;

    // Set a pixel
    // (does X and Y bounds checking in Debug but not Release builds)
    virtual void SetPixel(int32_t X, int32_t Y, uint32_t color) = 0;

    // Get pointer to pixel value buffer
    virtual void* GetFrameBuffer() = 0;

    inline int32_t Width(void)  const { return m_width;  }
    inline int32_t Height(void) const { return m_height; }

protected: // Note: protected so derived classes have access to member variables
    int32_t m_width;
    int32_t m_height;
};

#endif
