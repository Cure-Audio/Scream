#include <math.h>
#include <xhl/maths.h>

typedef struct SmoothedValue
{
    float current;
    float target;
    float inc;
    int   remaining;
} SmoothedValue;

static inline void smoothvalue_reset(SmoothedValue* sv, float newValue)
{
    sv->target = sv->current = newValue;
    sv->inc                  = 0;
    sv->remaining            = 0;
}

static inline float smoothvalue_tick(SmoothedValue* sv)
{
    float v = sv->current;
    xassert(sv->remaining >= 0);
    if (sv->remaining)
    {
        sv->remaining--;
        sv->current += sv->inc;
        xassert(sv->current >= 0 && sv->current <= 1);
    }

    return v;
}

static void smoothvalue_set_target(SmoothedValue* sv, float newValue, int steps)
{
    xassert(steps);
    float inc  = 0;
    float diff = newValue - sv->current;
    while (inc == 0 && newValue != sv->target && steps)
    {
        inc = diff / (float)steps;
        xassert(inc == inc);
        if (fabsf(inc) < 1.0e-5f)
        {
            inc     = 0;
            steps >>= 1;
        }
    }
    if (inc == 0)
        sv->current = newValue;

    sv->target    = newValue;
    sv->inc       = inc;
    sv->remaining = steps;
}