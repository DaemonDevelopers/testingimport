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

vec4 UnpackColor( const in int packedColor )
{
#if defined(HAVE_EXT_gpu_shader4)
	uint uPackedColor = ( packedColor & 0x7FFFFFFF ) | ( int( packedColor < 0 ) << 31 );
	return unpackUnorm4x8( uPackedColor );
#else
	int v = packedColor;

	int a = v / 4096; v -= a * 4096;
	int b = v / 256; v -= b * 256;
	int g = v / 16; v -= g * 16;
	int r = v;

	return vec4( r, g, b, a ) / 15;
#endif
}

/* colorMod format:

Bit 0: color * 1
Bit 1: color * ( -1 )
Bit 2: color += lightFactor
Bit 3: alpha * 1
Bit 4: alpha * ( -1 )
Bit 5: alpha = 1
Bits 6-12: available for future usage
Bits 13-16: lightFactor
Bits 17-32: unusable as GLSL 1.20 int is 16-bit.

int( colorMod < 0 ) can be used as a an emulated 17th bit
since GLSL 1.20 stores the sign elsewhere. */

float colorModArray[3] = float[3] ( 0.0f, 1.0f, -1.0f );

float ColorModulateToLightFactor( const in int colorMod ) {
#if defined(HAVE_EXT_gpu_shader4)
	return float( colorMod >> 6 );
#else
	return float( colorMod / 64 );
#endif
}

#define GLSL120_USE_MOD 0

void ColorModulateToColor(
	const in int colorMod,
	const in vec4 unpackedColor,
	inout vec4 color )
{
	#if defined(HAVE_EXT_gpu_shader4)
		int rgbIndex = colorMod & 3;
		int alphaIndex = ( colorMod >> 2 ) & 3;
	#else
		#if GLSL_120_USE_MOD
			int rgbBit0 = int( mod( int( colorMod / pow( 2, 0 ) ), 2 ) );
			int rgbBit1 = int( mod( int( colorMod / pow( 2, 1 ) ), 2 ) );
			int alphaBit0 = int( mod( int( colorMod / pow( 2, 2 ) ), 2 ) );
			int alphaBit1 = int( mod( int( colorMod / pow( 2, 3 ) ), 2 ) );
		#else
			int v = colorMod;
			int w = int( v / 2 );
			int rgbBit0 = v - 2 * w;
			v = w; w = int( v / 2 );
			int rgbBit1 = v - 2 * w;
			v = w; w = int( v / 2 );
			int alphaBit0 = v - 2 * w;
			v = w; w = int( v / 2 );
			int alphaBit1 = v - 2 * w;
		#endif

		int rgbIndex = rgbBit0 + ( rgbBit1 * 2 );
		int alphaIndex = alphaBit0 + ( alphaBit1 * 2 );
	#endif

	vec4 colorModulate = vec4( colorModArray[ rgbIndex ] );
	colorModulate.a = colorModArray[ alphaIndex ];

	color *= colorModulate;
	color += unpackedColor;
}

void ColorModulateToColor_Generic(
	const in int colorMod,
	const in vec4 unpackedColor,
	inout vec4 color )
{
	#if defined(HAVE_EXT_gpu_shader4)
		int rgbIndex = colorMod & 3;
		int alphaIndex = ( colorMod >> 2 ) & 3;
		int skipVertexFormat = ( colorMod >> 4 ) & 1;
		int hasLight = ( colorMod >> 5 ) & 1;
		int lightFactor = colorMod >> 6;
	#else
		#if GLSL_120_USE_MOD
			int rgbBit0 = int( mod( int( colorMod / pow( 2, 0 ) ), 2 ) );
			int rgbBit1 = int( mod( int( colorMod / pow( 2, 1 ) ), 2 ) );
			int alphaBit0 = int( mod( int( colorMod / pow( 2, 2 ) ), 2 ) );
			int alphaBit1 = int( mod( int( colorMod / pow( 2, 3 ) ), 2 ) );
			int hasLight = int( mod( int( colorMod / pow( 2, 4 ) ), 2 ) );
			int skipVertexFormat = int( mod( int( colorMod / pow( 2, 5 ) ), 2 ) );
			int lightFactor = colorMod / pow( 2, 6 );
		#else
			int v = colorMod;
			int w = int( v / 2 );
			int rgbBit0 = v - 2 * w;
			v = w; w = int( v / 2 );
			int rgbBit1 = v - 2 * w;
			v = w; w = int( v / 2 );
			int alphaBit0 = v - 2 * w;
			v = w; w = int( v / 2 );
			int alphaBit1 = v - 2 * w;
			v = w; w = int( v / 2 );
			int hasLight = v - 2 * w;
			v = w; w = int( v / 2 );
			int skipVertexFormat = v - 2 * w;
			w = int( v / 2 );
			int lightFactor = w;
		#endif

		int rgbIndex = rgbBit0 + ( rgbBit1 * 2 );
		int alphaIndex = alphaBit0 + ( alphaBit1 * 2 );
	#endif

	// This is used to skip vertex colours if the colorMod doesn't need them.
	color.a = bool( skipVertexFormat ) ? 1.0 : color.a;

	vec4 colorModulate = vec4( colorModArray[ rgbIndex ] + ( hasLight * lightFactor ) );
	colorModulate.a = colorModArray[ alphaIndex ];

	color *= colorModulate;
	color += unpackedColor * vec4( lightFactor, lightFactor, lightFactor, 1.0 );
}
