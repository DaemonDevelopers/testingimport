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

#if defined(HAVE_EXT_gpu_shader4)
	#define colorPack uint
	#define colorModulatePack uint
#else
	#define colorPack vec4
	#define colorModulatePack vec4
#endif

vec4 UnpackColor( const in colorPack packedColor )
{
#if defined(HAVE_EXT_gpu_shader4)
	return unpackUnorm4x8( packedColor );
#else
	return packedColor;
#endif
}

/* colorMod uint format:

colorMod << 0: color * 1
colorMod << 1: color * ( -1 )
colorMod << 2: alpha * 1
colorMod << 3: alpha * ( -1 )
colorMod << 4: alpha = 1
colorMod << 5-26: available for future usage
colorMod << 27: color += lightFactor
colorMod << 28-31: lightFactor

colorMod float format:

colorMod[ 0 ]: color * f
colorMod[ 1 ] absolute value: lightFactor
colorMod[ 1 ] minus sign: color += lightFactor
colorMod[ 2 ]: alpha = 1
colorMod[ 3 ]: alpha * f */

vec4 ColorModulateToColor( const in colorModulatePack colorMod )
{
#if defined(HAVE_EXT_gpu_shader4)
	vec3 colorModArray = vec3( 0.0f, 1.0f, -1.0f );

	uint rgbIndex = colorMod & 3u;
	uint alphaIndex = ( colorMod >> 2u ) & 3u;

	float rgb = colorModArray[ rgbIndex ];
	float alpha = colorModArray[ alphaIndex ];
#else
	float rgb = colorMod.r;
	float alpha = colorMod.a;
#endif

	return vec4( rgb, rgb, rgb, alpha );
}

struct modBits_t {
	bool alphaAddOne;
	bool isLightStyle;
};

modBits_t ColorModulateToBits( const in colorModulatePack colorMod )
{
	modBits_t modBits;

#if defined(HAVE_EXT_gpu_shader4)
	modBits.alphaAddOne = bool( ( colorMod >> 4u ) & 1u );
	modBits.isLightStyle = bool( ( colorMod >> 27u ) & 1u );
#else
	modBits.alphaAddOne = colorMod.b != 0;
	modBits.isLightStyle = colorMod.g < 0;
#endif

	return modBits;
}

float ColorModulateToLightFactor( const in colorModulatePack colorMod )
{
#if defined(HAVE_EXT_gpu_shader4)
	return float( colorMod >> 28u );
#else
	return abs( colorMod.g );
#endif
}

void ModulateColor(
	const in vec4 colorModulation,
	const in vec4 unpackedColor,
	inout vec4 color )
{
	color *= colorModulation;
	color += unpackedColor;
}

void ColorModulateColor(
	const in colorModulatePack colorMod,
	const in colorPack packedColor,
	inout vec4 color )
{
	vec4 colorModulation = ColorModulateToColor( colorMod );

	vec4 unpackedColor = UnpackColor( packedColor );

	ModulateColor( colorModulation, unpackedColor, color );
}

void ColorModulateColor_lightFactor(
	const in colorModulatePack colorMod,
	const in colorPack packedColor,
	inout vec4 color )
{
	vec4 colorModulation = ColorModulateToColor( colorMod );
	modBits_t modBits = ColorModulateToBits( colorMod );
	float lightFactor = ColorModulateToLightFactor( colorMod );

	// This is used to skip vertex colours if the colorMod doesn't need them.
	color.a = modBits.alphaAddOne ? 1.0 : color.a;

	colorModulation.rgb += vec3( modBits.isLightStyle ? lightFactor : 0 );

	vec4 unpackedColor = UnpackColor( packedColor );

	unpackedColor.rgb *= lightFactor;

	ModulateColor( colorModulation, unpackedColor, color );
}
