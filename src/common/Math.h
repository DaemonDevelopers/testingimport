/*
===========================================================================
Daemon BSD Source Code
Copyright (c) 2013-2016, Daemon Developers
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Daemon developers nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL DAEMON DEVELOPERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
===========================================================================
*/

#ifndef COMMON_MATH_H_
#define COMMON_MATH_H_

#include <algorithm>

#include "math/Constants.h"

namespace Math {

    // This is designed to return min if value is NaN. That's not guaranteed to work when
    // compiling with fast-math flags though.
    template<typename T> inline WARN_UNUSED_RESULT
    T Clamp(T value, T min, T max)
    {
        ASSERT_LE(min, max);
        if (!(value >= min))
            return min;
        if (!(value <= max))
            return max;
        return value;
    }

    // IsFinite: Replacements for std::isfinite that should work even with fast-math flags

    // An IEEE754 float is finite when the exponent is not all ones.
    // 'volatile' serves as an optimization barrier against the compiler assuming that
    // the float can never have a NaN bit pattern.
    inline bool IsFinite(float x)
    {
        volatile uint32_t bits = Util::bit_cast<uint32_t>(x);
        return ~bits & 0x7f800000;
    }

    inline bool IsFinite(double x)
    {
        volatile uint64_t bits = Util::bit_cast<uint64_t>(x);
        return ~bits & 0x7ff0000000000000;
    }

	inline float DegToRad( float angle )
	{
		return angle * divpi_180_f;
	}

	inline float RadToDeg( float angle )
	{
		return angle * div180_pi_f;
	}

	inline double DegToRad( double angle )
	{
		return angle * divpi_180_d;
	}

	inline double RadToDeg( double angle )
	{
		return angle * div180_pi_d;
	}
}

#include "math/Vector.h"

template<typename A>
DEPRECATED A DEG2RAD( const A a )
{
	return Math::DegToRad( a );
}

template<typename A>
DEPRECATED A RAD2DEG( const A a )
{
	return Math::RadToDeg ( a );
}

#endif //COMMON_MATH_H_
