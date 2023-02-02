#include <cstdlib>   // rand()
#include <math.h>    // expf()

#include "random.h"
#include "memory.h"  // memset()

////////////////////////////////////////////////////////////////////////////////
// Approximation to unit normal distribution.
// Returns number in range: -6.0 <= x <= 6.0
// Note: in a true unit normal distribution, only 0.00034% of samples fall outside ±6.
static float IrwinHallDist(void)
{
    // Get sum of 12 numbers randomly drawn from range [0, RAND_MAX]
    // Note: potential overflow of sum will bias final output to smaller values
    // Note: loop unrolled for speed
    uint32_t sum = rand();
    sum += rand();
    sum += rand();
    sum += rand();

    sum += rand();
    sum += rand();
    sum += rand();
    sum += rand();

    sum += rand();
    sum += rand();
    sum += rand();
    sum += rand();

    // create float in range: 0 <= x <= 12.0
    float value = (float)sum / RAND_MAX;

    // note: at this point variance will automatically be 1.0 since
    // variance of uniform number in [0, 1] is (1 / 12) so variance of
    // sum of 12 uniform numbers in [0, 1] is 1.0

    return (value - 6.0f); // subtract mean to get zero mean output
}

////////////////////////////////////////////////////////////////////////////////
// Return integer drawn from poisson distribution with parameter "lam".
// Uses inversion by sequential search from:
// https://en.wikipedia.org/wiki/Poisson_distribution#Related_distributions
uint32_t randp(float lam)
{
    // Note: need uniform number in [0, 1) (if u = 1.0 then infinite loop below)
    const float u = (float)rand() / (RAND_MAX + 1); // random float in [0, 1)

    // Knuth algorithm
    // Probability of k events: Pr{k  } = lam^k * exp(-lam) / k!
    //                      --> Pr{k=0} =         exp(-lam)
    uint32_t x = 0;          // candidate output value
    float    p = expf(-lam); // init to Pr{x=0}
    float    s = p;          // init sum of probabilities (cumulative distribution)
    while (u > s)            // while uniform random is > cumulative probabilities
    {
        ++x;                 // move to next candidate output value
        p *= lam / x;        // update Pr{k=x}
        s += p;              // update sum of probabilities
    }
    return x;
}

////////////////////////////////////////////////////////////////////////////////
// Gaussian random number generator, zero mean and unit variance
// Note: randn() is a wrapper so we can later replace IrwinHallDist() with
// more accurate Gaussian random number generator.
float randn(void) { return IrwinHallDist(); }

