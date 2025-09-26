// Note (Tre): The original lib was written in C++ but I rewrote it in C
/*
BSD 3-Clause License

Copyright (c) 2020, jatinchowdhury18
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once
#define ADAA_TOL 1.0e-5

#include "polylogarithm_Li2.h"
#include <math.h>
#include <stdbool.h>

// Note (Tre): I tried using single precision floats here but immediately got infinity errors. The precision is likely
// too low and we'll have to stick with doubles
typedef struct Tanh_ADAA2
{
    double x1;
    double x2;

    double d2;
    double ad1_x1;
    double ad2_x0;
    double ad2_x1;
} Tanh_ADAA2;

// First antiderivative
static inline double ADAA_Tanh_AD1(double x) { return log(cosh(x)); }
// Second antiderivative
static inline double ADAA_Tanh_AD2(double x)
{
    const double expVal = exp(-2 * x);
    return 0.5 * ((double)li2(-expVal) - x * (x + 2.0 * log(expVal + 1.) - 2.0 * log(cosh(x)))) + (pow(M_PI, 2) / 24.0);
}

static double Tanh_ADAA2_process(Tanh_ADAA2* t, double x)
{
    bool illCond1 = fabs(x - t->x1) < ADAA_TOL;
    t->ad2_x0     = ADAA_Tanh_AD2(x);
    double d1;
    if (illCond1)
    {
        d1 = ADAA_Tanh_AD1(0.5 * (x + t->x1));
    }
    else
    {
        d1 = (t->ad2_x0 - t->ad2_x1) / (x - t->x1);
    }

    double y;
    bool   illCond2 = fabs(x - t->x2) < ADAA_TOL;
    if (illCond2)
    {
        double xBar  = 0.5 * (x + t->x2);
        double delta = xBar - t->x1;

        bool illCond3 = fabs(delta) < ADAA_TOL;

        if (illCond3)
        {
            y = tanh(0.5 * (xBar + t->x1));
        }
        else
        {
            double ad1 = ADAA_Tanh_AD1(xBar);
            double ad2 = ADAA_Tanh_AD2(xBar);
            y          = (2.0 / delta) * (ad1 + (t->ad2_x1 - ad2) / delta);
        }
    }
    else
    {
        y = (2.0 / (x - t->x2)) * (d1 - t->d2);
    }

    // update state
    t->d2     = d1;
    t->x2     = t->x1;
    t->x1     = x;
    t->ad2_x1 = t->ad2_x0;

    return y;
}
