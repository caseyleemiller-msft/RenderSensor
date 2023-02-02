#pragma once

#ifndef __random_h__
#define __random_h__

#include <stdint.h> // int32_t, etc


////////////////////////////////////////////////////////////////////////////////
// 1-pass calculation of mean and variance of an array
template <class T> void MeanVariance(
    uint32_t count,    //  in: number of samples
    T*       pSamples, //  in: array of samples
    double*  pMean,    // out:     mean of samples
    double*  pVar)     // out: variance of samples
{
    T* pEnd = pSamples + count;
    double sum  = 0;
    double sum2 = 0;
    while (pSamples < pEnd)
    {
        double value = *pSamples++;
        sum  += value;
        sum2 += value * value;
    }
    *pMean       = sum  / count;
    double EX2   = sum2 / count;
    double EX_EX = *pMean * (*pMean);

    *pVar = (EX2 > EX_EX) ? (EX2 - EX_EX) : 0.0;
}

////////////////////////////////////////////////////////////////////////////////
// Gaussian random number generator with zero mean and unit variance.
float randn(void);

////////////////////////////////////////////////////////////////////////////////
// Return integer drawn from poisson distribution with parameter "p".
// Note: for p > 500, exp(-p) might overflow
// so use gaussian approximation: round(sqrt(p) * randn() + p)
uint32_t randp(float p);

////////////////////////////////////////////////////////////////////////////////
// Generate random numbers from a Poisson distribution with parameter "p".
// When p >= 20, uses Gaussian approximation: round(sqrt(p) * randn() + p)
void PoissonDist(
    float     p,     // parameter of Poisson distribution
    uint32_t  count, // number of values to generate
    uint32_t* pOut); // output values

////////////////////////////////////////////////////////////////////////////////
// Generate random integers drawn from Poisson distribution with parameter "p".
// Uses Gaussian approximation for p >= 20.
template <class T = uint32_t> void PoissonDist(
    float     lam,   // mean and variance of Poisson distribution
    uint32_t  count, // number of values to generate
    T*        pOut)  // output values drawn from Poisson distribution
{
    if (lam == 0.0f)
    {
        memset(pOut, 0x00u, count * sizeof(T));
        return;
    }

    const T maxVal = std::numeric_limits<T>::max();
    if (lam < 20.0f)
    {
        while (count--)
        {
            uint32_t value = randp(lam);
            if (value > maxVal) { value = maxVal; }
            *pOut++ = (T)value;
        }
        return;
    }
    const float mean   = lam;
    const float stddev = sqrtf(lam);
    while (count--)
    {
        float gaussian = stddev * randn() + mean;
        if (gaussian < 0.0f) { gaussian = 0.0f; } // Poisson is non-negative
        uint32_t value = (uint32_t)(gaussian + 0.5f); // round to integer
        *pOut++ = (value > maxVal) ? maxVal : (T)value;
    }
}

#endif
