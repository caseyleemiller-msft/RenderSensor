#pragma once

#ifndef __SpadSim_h__
#define __SpadSim_h__

#include <vector>
#include <array>
#include "random.h" // PoissonDist

class SpadSim
{
public:

    ////////////////////////////////////////////////////////////////////////////
    SpadSim(
        int32_t  width  = 1008,
        int32_t  height =  768,
        uint32_t seedDF =    1u) :  // Dark frame random seed
        m_width(width),
        m_height(height)
    {
        assert((width & 0x1) == 0); // bitmaps require even width

        // Compute constants
        const int numPix = width * height;
        const int widthDiv2  = width  / 2;
        const int heightDiv2 = height / 2;
        const int maxRadius2 = heightDiv2 * heightDiv2 +
                                widthDiv2 * widthDiv2;
        const int maxRadius = static_cast<const int>
                                  (sqrtf(static_cast<float>(maxRadius2)));

        // Init the lens radial distortion LUT----------------------------------
        m_lensDistLUT.reserve(maxRadius + 1);
        m_lensDistLUT.resize(maxRadius + 1);
        const float radiusHFOV = static_cast<float>(widthDiv2); // radius @ (X=screenWidthDiv2, Y=0)
        const float scaleHFOV  = 1.0f + radiusHFOV * 0.3f / (maxRadius - 1);
        for (int r = 0; r <= maxRadius; ++r)
        {
            // (scale > 1) --> barrel
            // (scale < 1) --> pincushion
            float scale = 1.0f + r * 0.3f / maxRadius;

            // adjust so scale = 1 at (X=screenWidthDiv2, Y=0) so that horizontal
            // field of view is unchanged in distorted image.
            scale /= scaleHFOV;

            // final scale factor with 8 fractional bits.
            uint32_t value = (uint32_t)(scale * 256.0f + 0.5f); // round to int
            assert(value <= std::numeric_limits<uint16_t>::max());
            m_lensDistLUT[r] = (uint16_t)value;
        }

        // relative illumination (aka lens vignetting)--------------------------
        m_relativeIllumLUT.reserve(maxRadius + 1);
        m_relativeIllumLUT.resize(maxRadius + 1);
        for (int r = 0; r <= maxRadius; ++r)
        {
            m_relativeIllumLUT[r] = (maxRadius - r) * 256 / maxRadius;
        }

        // generate dark frame--------------------------------------------------

        // DF at 60C sensor temperature and max exposure of 11111 microseconds (= 1/90)
        m_pDF60.reserve(numPix);
        m_pDF60.resize(numPix);
        srand(seedDF);
        PoissonDist<uint32_t>(2.0, numPix, m_pDF60.data()); // SPAD avg DF pixel value is 2.0 at 60C

        // Add 1% hot pixels to pDF60
        const int numHot = 1 * numPix / 100;
        for (int ii = 0; ii < numHot; ++ii)
        {
            // random locations
            uint32_t r = rand() % height;
            uint32_t c = rand() % width;

            // Generate Poisson (with mean 80)
            PoissonDist<uint32_t>(80.0f, 1u, m_pDF60.data() + r * width + c);
        }

        m_pDF.reserve(numPix);
        m_pDF.resize(numPix);
        SetDarkFrame(60.0f, 7.0f, 11111u); // create initial dark frame

        // Create Poisson noise 2D LUT------------------------------------------
        const uint32_t noiseHeight = 256; // >= largest pixel value
        const uint32_t noiseWidth  = 256; // large enough for accurate Poisson stats
        for (int r = 0; r < noiseHeight; ++r) // loop thru rows
        {
            uint16_t* pRow = m_noiseLUT.data() + r * noiseWidth;
            PoissonDist<uint16_t>((float)r, noiseWidth, pRow); // Fill row with randp(r)
        }

        // Set PWL LUT----------------------------------------------------------
        for (int ii = 0; ii < (1 << 12); ++ii)
        {
            int value = (ii <= 6) ? ii : (int)(sqrtf(ii * 6.0f) + 0.5f);
            if (value > 255) { value = 255; } // clip to uint8
            m_pwlLUT[ii] = (uint8_t)value;
        }

        // Set LUT to create RGB from 8-bit grayscale---------------------------
        for (uint32_t ii = 0; ii < 256; ++ii)
        {
            m_byte2rgbLUT[ii] = (ii << 16) | (ii << 8) | ii;
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    ~SpadSim(void)
    {
    }

    // TODO: funcs to set custom LUTs or dark frame

    ////////////////////////////////////////////////////////////////////////////
    // Generate dark frame at a specified sensor temperature and exposure time.
    //
    // 1. Let "D60" be the dark frame captured at 60C sensor temperature and maximum
    //    exposure time (11 msec for 90fps), can generate dark frame at temp T via:
    //        D(T, r, c) = D60(r, c) * 2^((T - 60) / doubleTemp)
    // 2. Dark frame (at a fixed sensor temperature) scales linearly with exposure
    //    time.  So 11/2 msec at 60C sensor temperture results in D60/2.
    //
    // Typical average pixel value is 0.17 for 30C and 2.0 for 60C at max exposure
    void SetDarkFrame(
        float     sensorTempC,  // in: sensor temperature in degrees C for pD
        float     doubleTempC,  // in: doubling temperature in degrees C
                                //     (usually 7-9 for SPAD sensors)
        uint32_t  expTimeUsec)  // in: exposure time in micro seconds
    {
        const uint32_t maxExpTimeUsec = 11111; // 90 fps
        const float exposureScale = (float)expTimeUsec / maxExpTimeUsec;

        uint32_t* pDF60 = m_pDF60.data();
        uint32_t* pDF   = m_pDF.data();
        size_t   count  = m_pDF60.size();
        while (count--)
        {
            float DmaxExp = *pDF60++ * exp2f((sensorTempC - 60.0f) / doubleTempC);
            *pDF++ = (uint32_t)(DmaxExp * exposureScale + 0.5f);
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    void AddDistortion(
        const uint32_t* pRd,                     // input rendered frame
        uint32_t*       pWr,                     // output frame
        bool            enableLensDist = true,   // barrel/pincushion
        bool            enableDF       = true,   // enable dark frame
        bool            enablePWL      = true)   // enable PWL
    {
        const int widthDiv2  = m_width  / 2;
        const int heightDiv2 = m_height / 2;
        const int maxRadius2 = heightDiv2 * heightDiv2 +
                                widthDiv2 * widthDiv2;
        const int maxRadius = (int)sqrtf(static_cast<float>(maxRadius2));

        m_noiseIdx = rand() & 0xFFu; // avoid fixed noise when enableDF = false

        // Add sensor and lens effects to rendered frame
        const uint32_t* pDF = m_pDF.data();
        for (int r = -heightDiv2; r < heightDiv2; ++r)
        {
            // set up radius for first pixel of this row
            // Note: due to cast to int: iradius * iradius <= radius2
            int radius2 = r * r + widthDiv2 * widthDiv2;
            int iradius = (int)sqrtf((float)radius2); // "integer radius"

            for (int c = -widthDiv2; c < widthDiv2; ++c)
            {
                uint32_t value;

                // lens distortion
                if (enableLensDist)
                {
                    int rd = r * m_lensDistLUT[iradius] / 256 + heightDiv2;
                    int cd = c * m_lensDistLUT[iradius] / 256 + widthDiv2;
                    if ((0 <= rd) && (rd < m_height) && // image boundry check
                        (0 <= cd) && (cd < m_width)    )
                    {
                        // TODO: bilinear interp: Make rd, cd and lensDistLut Fixedpoint
                        value = pRd[rd * m_width + cd]; // nearest neighbor interp
                    }
                    else
                    {
                        value = 0; // value for out of bounds
                    }
                }
                else // else lens distortion disabled
                {
                    value = *pRd++;
                }

                // TODO: lens blur goes here (or absorb it into lens
                // distortion interpolation).  A 3x3 filter via FIFO might work.

                // relative illumination (aka lens vignetting)
                value = (value * m_relativeIllumLUT[iradius]) >> 8;

                // add dark frame
                if (enableDF) { value += *pDF++; }

                // add Poisson noise, must be done AFTER adding dark
                // frame (otherwise in the absense of a scene (e.g. lens covered)
                // output would be dark frame rather than noisy dark frame)
                if (value < 256u) // if LUT can be used...
                {
                    value = m_noiseLUT[value * 256 + m_noiseIdx];
                    m_noiseIdx++; // increment index with intentional roll-over
                }
                else              // else generate Poisson sample on-the-fly
                {
                    PoissonDist<uint32_t>((float)value, 1u, &value);
                }

                // TODO: SPAD nonlinearity to convert from photons to counts.
                // (but PWL can linearize and compress so can probably skip)
                // This could be implemented as a linearly interpolated LUT.

                // PWL compression from 12-bits to 8-bits
                if (enablePWL)
                {
                    if (value > 4095) { value = 4095; } // clip to 12 bits
                    value = m_pwlLUT[value];
                }

                if (value > 255u) { value = 255u; } // clip to 8 bits
                *pWr++ = m_byte2rgbLUT[value]; // convert to format needed by GdiWindow

                // update iradius for next column (c + 1):
                // we're at c^2 and need to get to (c+1)^2
                // so delta = (c+1)^2 - c^2
                //          = c^2 + 2*c + 1 - c^2
                //          =       2*c + 1
                radius2 += 2 * c + 1; // compute radius^2 for (c+1)

                // Adjust iradius so that: iradius^2 <= radius2
                // TODO: 2 mults can be replaced with shifts/adds (see RadiusRaster unit test)
                if (iradius * iradius < radius2) { ++iradius; } // avoid sqrt()
                if (iradius * iradius > radius2) { --iradius; } // avoid sqrt()
            }
        }
    }

private:
    int32_t m_width;
    int32_t m_height;

    std::vector<uint32_t> m_pDF60;
    std::vector<uint32_t> m_pDF;

    // LUTs that take radius as input
    std::vector<uint16_t> m_lensDistLUT;      //  barrel/pincushion distortion
    std::vector<uint8_t>  m_relativeIllumLUT; // Q8 fractional multiplier

    // LUTs that take pixel value as input
    uint8_t m_noiseIdx;
    std::array<uint16_t, 256*256> m_noiseLUT;    // 2D, 8-bit input, 16-bit output
    std::array<uint8_t,     4096> m_pwlLUT;      // 12-bit input,  8-bit output
    std::array<uint32_t,     256> m_byte2rgbLUT; //  8-bit input, 32-bit output
};

#endif
