/*
    DaisySP, copyright (c) 2020 Electrosmith, Corp.
    MIT licensed; see LICENSE.txt in this directory.

    Adapted from the Polyblep helper in Source/Synthesis/oscillator.cpp at
    https://github.com/daisyaudio/DaisySP/tree/2c72eaf9eac5fc0dca1919d65d606da907832618
    Local changes: standalone inline function, namespace/name, double precision
    to match the offline renderer. The polynomial and edge conventions are unchanged.
*/
#pragma once

namespace DaisySPPolyBlep
{
    // phase is wrapped to [0, 1); phaseIncrement is in (0, 0.5).
    inline double correction (double phaseIncrement, double phase)
    {
        if (phase < phaseIncrement)
        {
            phase /= phaseIncrement;
            return phase + phase - phase * phase - 1.0;
        }
        if (phase > 1.0 - phaseIncrement)
        {
            phase = (phase - 1.0) / phaseIncrement;
            return phase * phase + phase + phase + 1.0;
        }
        return 0.0;
    }
}
