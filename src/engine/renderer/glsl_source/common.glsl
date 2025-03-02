/*
===========================================================================

Daemon BSD Source Code
Copyright (c) 2024-2025 Daemon Developers
All rights reserved.

This file is part of the Daemon BSD Source Code (Daemon Source Code).

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

/* common.glsl */

/* Common defines */

/* Allows accessing each element of a uvec4 array with a singular ID
Useful to avoid wasting memory due to alignment requirements
array must be in the form of uvec4 array[] */

#define UINT_FROM_UVEC4_ARRAY( array, id ) ( ( array )[( id ) / 4][( id ) % 4] )
#define UVEC2_FROM_UVEC4_ARRAY( array, id ) ( ( id ) % 2 == 0 ? ( array )[( id ) / 2].xy : ( array )[( id ) / 2].zw )

// Common functions

vec4 UnpackColor( const in int color )
{
#if defined(HAVE_EXT_gpu_shader4)
	return unpackUnorm4x8( color );
#else
	int v = color, m = 0;

	if ( v < 0 )
	{
		v = 2147483647 - abs( v ) + 1;
		m = 128;
	}

	int a = v / 16777216;
	v -= a * 16777216;
	int b = v / 65536;
	v -= b * 65536;
	int g = v / 256;
	v -= g * 256;
	int r = v;
	a = a + m;

	return vec4( r, g, b, a ) / 255;
#endif
}

/* Bit 0: color * 1
Bit 1: color * ( -1 )
Bit 2: color += lightFactor
Bit 3: alpha * 1
Bit 4: alpha * ( -1 )
Bit 5: alpha = 1
Bits 6-11: available for future usage
Bits 12-15: lightFactor:
Bit 16: intentionally left blank, some rewrite is required (see below)
if that bit has to be used once all the other bits are exhausted.
Bits 17-32: unusable as GLSL 1.20 int is 16-bit. */

float colorModArray[3] = float[3] ( 0.0f, 1.0f, -1.0f );

vec4 ColorModulateToColor( const in int colorMod ) {
#if defined(HAVE_EXT_gpu_shader4)
	int rgbIndex = colorMod & 3;
	int alphaIndex = ( colorMod & 24 ) >> 3;
#else
	int rgbBit0 = int( mod( colorMod , 2 ) );
	int rgbBit1 = int( mod( int( colorMod / 2 ), 2 ) );
	int alphaBit0 = int( mod( int( colorMod / 8 ), 2 ) );
	int alphaBit1 = int( mod( int( colorMod / 16 ), 2 ) );
	int rgbIndex = rgbBit0 + ( rgbBit1 * 2 );
	int alphaIndex = alphaBit0 + ( alphaBit1 * 2 );
#endif

	vec4 colorModulate = vec4( colorModArray[ rgbIndex ] );
	colorModulate.a = colorModArray[ alphaIndex ];
	return colorModulate;
}

vec4 ColorModulateToColor( const in int colorMod, const in float lightFactor ) {
#if defined(HAVE_EXT_gpu_shader4)
	int rgbIndex = colorMod & 3;
	int alphaIndex = ( colorMod & 24 ) >> 3;
	int hasLight = ( colorMod & 4 ) >> 2;
#else
	int rgbBit0 = int( mod( colorMod, 2 ) );
	int rgbBit1 = int( mod( int( colorMod / 2 ), 2 ) );
	int hasLight = int( mod( int( colorMod / 4 ), 2 ) );
	int alphaBit0 = int( mod( int( colorMod / 8 ), 2 ) );
	int alphaBit1 = int( mod( int( colorMod / 16 ), 2 ) );
	int rgbIndex = rgbBit0 + ( rgbBit1 * 2 );
	int alphaIndex = alphaBit0 + ( alphaBit1 * 2 );
#endif

	vec4 colorModulate = vec4( colorModArray[ rgbIndex ] + ( hasLight * lightFactor ) );
	colorModulate.a = colorModArray[ alphaIndex ];
	return colorModulate;
}

float ColorModulateToLightFactor( const in int colorMod ) {
#if defined(HAVE_EXT_gpu_shader4)
	/* The day the 16th bit is used, this should be done instead:
	return float( ( colorMod >> 12 ) & 0xF ); */
	return float( colorMod >> 12 );
#else
	/* The day the 16th bit used, this should be rewritten to
	extract the value without the sign, like that:
	int v = colorMod;
	if ( v < 0 ) v =  32767 - abs( v ) + 1;
	return float( v / 4096 ); */
	return float( colorMod / 4096 );
#endif
}

// This is used to skip vertex colours if the colorMod doesn't need them
bool ColorModulateToVertexColor( const in int colorMod ) {
#if defined(HAVE_EXT_gpu_shader4)
	return ( colorMod & 32 ) == 32;
#else
	return int( mod( int( colorMod / 32 ), 2 ) ) == 1;
#endif
}
