// Unit tests for polygon.cpp functions

// Next two lines enable memory leak detection as per:
// https://learn.microsoft.com/en-us/visualstudio/debugger/finding-memory-leaks-using-the-crt-library?view=vs-2022
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>      // _Crt* memory leak detection functions

#include <time.h>        // time()
#include <vector>
#include <random>        // std::poisson_distribution
#include <stdio.h>
#include <stdlib.h>
#include <cstdlib>       // rand()
#include <iostream>      // ostream, endl, etc
#include <gtest/gtest.h> // google test framework

#ifndef M_PI
#define _USE_MATH_DEFINES // M_PI
#include <math.h>         // sin(), cos()
#endif

#include "random.h"
#include "RenderFXP.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////
// Memory leak detection class cut/pasted from:
// https://stackoverflow.com/questions/29174938/googletest-and-memory-leaks
class MemoryLeakDetector {
public:
    MemoryLeakDetector() {
        _CrtMemCheckpoint(&m_startState); // get snapshot of memory state
    }

    ~MemoryLeakDetector() {
        _CrtMemState stopState;
        _CrtMemCheckpoint(&stopState); // get snapshot of memory state at end
        (void)stopState;               // avoid error in release build

        // Generate failure message if memory state changed
        _CrtMemState stateDiff;
        if (_CrtMemDifference(&stateDiff, &m_startState, &stopState))
        {
            reportFail(stateDiff.lSizes[1]);
        }
    }
private:
    void reportFail(size_t unfreedBytes) {
        FAIL() << "Memory leak of " << unfreedBytes << " byte(s) detected.";
    }
    _CrtMemState m_startState;
};

////////////////////////////////////////////////////////////////////////////////
TEST(PolygonTests, CosSin) {
    MemoryLeakDetector leakDetector;

    const double deg2Rad = M_PI / 180.0;

    // Loop from 0 to 90 by 1 degree increment
    Fixedpoint degrees;
    double maxCosErr = 0.0;
    double maxSinErr = 0.0;
    for (degrees = 0; degrees <= INT_TO_FIXED(90); ++degrees)
    {
        // get sin and cos using CosSin()
        Fixedpoint estimSin, estimCos;
        CosSin(degrees, &estimCos, &estimSin);

        // get sin and cos using math.h functions
        double degreesDbl = FIXED_TO_DOUBLE(degrees);
        double idealSin = sin(degreesDbl * deg2Rad);
        double idealCos = cos(degreesDbl * deg2Rad);

        // Keep maximum absolute error
        double err = idealCos - FIXED_TO_DOUBLE(estimCos);
        maxCosErr = std::max(abs(err), maxCosErr);

        err = idealSin - FIXED_TO_DOUBLE(estimSin);
        maxSinErr = std::max(abs(err), maxSinErr);
    }
    //printf("Max abs cos err = %lf, max abs sin err = %lf\n", maxCosErr, maxSinErr);
    EXPECT_DOUBLE_EQ(maxSinErr, maxCosErr);
    EXPECT_LT(maxSinErr, 0.000054);

    // Loop from 90 to 360 by 5 degree increments
    maxCosErr = 0.0;
    maxSinErr = 0.0;
    for (degrees = INT_TO_FIXED(90); degrees <= INT_TO_FIXED(360); degrees += 5)
    {
        // get sin and cos using CosSin()
        Fixedpoint estimSin, estimCos;
        CosSin(degrees, &estimCos, &estimSin);

        // get sin and cos using math.h functions
        double degreesDbl = FIXED_TO_DOUBLE(degrees);
        double idealSin = sin(degreesDbl * deg2Rad);
        double idealCos = cos(degreesDbl * deg2Rad);

        // Keep maximum absolute error
        double err = idealCos - FIXED_TO_DOUBLE(estimCos);
        maxCosErr = std::max(abs(err), maxCosErr);

        err = idealSin - FIXED_TO_DOUBLE(estimSin);
        maxSinErr = std::max(abs(err), maxSinErr);
    }
    //printf("Max abs cos err = %lf, max abs sin err = %lf\n", maxCosErr, maxSinErr);
    EXPECT_DOUBLE_EQ(maxSinErr, maxCosErr);
    EXPECT_LT(maxSinErr, 0.000054);
}

////////////////////////////////////////////////////////////////////////////////
TEST(PolygonTests, Random) {
    const uint32_t count = 1008 * 768; // 100000u;

    float* gSamples = (float*)malloc(count * sizeof(float));;
    EXPECT_NE(gSamples, nullptr);

    uint32_t* pSamples = (uint32_t*)malloc(count * sizeof(uint32_t));;
    EXPECT_NE(pSamples, nullptr);

    // Test that randn() generates zero-mean, unit variance values
    for (int i = 0; i < count; ++i) { gSamples[i] = randn(); }
    double mean, var;
    MeanVariance<float>(count, gSamples, &mean, &var);
    //printf("mean= %lf, var= %lf\n", mean, var);
    EXPECT_NEAR(mean, 0.0, 1e-2);
    EXPECT_NEAR(var,  1.0, 1e-2);

    // Test that poisson random number generator works for p < 20
    float p = 2.0f;
    PoissonDist<uint32_t>(p, count, pSamples);
    MeanVariance<uint32_t>(count, pSamples, &mean, &var);
    //printf("poisson( 0.5): mean= %lf, var= %lf\n", mean, var);
    EXPECT_NEAR(mean, (double)p, 1e-2);
    EXPECT_NEAR(var,  (double)p, 1e-2);

    // Test that poisson random number generator works for p = 20.0
    p = 20.0f;
    PoissonDist<uint32_t>(p, count, pSamples);
    MeanVariance<uint32_t>(count, pSamples, &mean, &var);
    //printf("poisson(20.0): mean= %lf, var= %lf\n", mean, var);
    EXPECT_NEAR(mean, (double)p, 1e-1);
    EXPECT_NEAR(var,  (double)p, 1e-1);

    free(gSamples);
    free(pSamples);
}

////////////////////////////////////////////////////////////////////////////////
// Determine radius in a raster scan without sqrt() in inner loop
TEST(PolygonTests, RadiusRaster) {

    const int width  = 1008;
    const int height =  768;

    const int widthDiv2  = width  / 2;
    const int heightDiv2 = height / 2;

    for (int r = -heightDiv2; r < heightDiv2; ++r)
    {
        // set up radius for first pixel of this row
        // Note: due to cast to int: iradius * iradius <= radius2
        int radius2  = r * r + widthDiv2 * widthDiv2;
        int iradius  = (int)sqrtf((float)radius2); // "integer radius" = floor(sqrt(radius2))
#ifdef MINIMIZE_MULTIPLIES
        int iradius2 = iradius * iradius;
#endif
        for (int c = -widthDiv2; c < widthDiv2; ++c)
        {

            // Code that uses iradius would normally go here.

            // update radius2 for next column (c + 1):
            // we're at c^2 and need to get to (c+1)^2
            // so delta = (c+1)^2 - c^2
            //          = c^2 + 2*c + 1 - c^2
            //          =       2*c + 1
            radius2 += 2 * c + 1; // = radius^2 for (c+1)

#ifdef MINIMIZE_MULTIPLIES
            // The following uses only shifts and adds (for FPGA design)
            if (iradius2 > radius2)
            {
                // iradius2 = (iradius - 1) * (iradius - 1)
                //          =  iradius2 - 2 * iradius + 1
                iradius2 -= 2 * iradius - 1;
                --iradius;
            }

            // else if ((iradius + 1) * (iradius + 1) <= radius2)
            else if    ((iradius2 +  2 * iradius + 1) <= radius2)
            {
                // iradius2 = (iradius + 1) * (iradius + 1)
                //          =  iradius2 + 2 * iradius + 1
                iradius2 += 2 * iradius + 1;
                ++iradius;
            }
#else
            // For architectures with single cycle multiply:
            if (iradius * iradius < radius2) { ++iradius; } // avoid sqrt()
            if (iradius * iradius > radius2) { --iradius; } // avoid sqrt()
#endif
            // check radius is correct
            uint32_t iradiusIdeal = (uint32_t)hypotf((float)r, (float)(c + 1));
            if (iradius != iradiusIdeal)
            {
                printf("r=%d, c=%d, radius=%u, radius2=%u ihypotf(r,c)=%u\n",
                        r,    c,    iradius,   radius2,   iradiusIdeal);
            }
            EXPECT_EQ(iradius, iradiusIdeal);
        }
    }
}

// TODO: timing test for rendering 200 frames
// TODO: timing test for pipeline processing 200 frames
// TODO: average linear frames to obtain original dark frame
