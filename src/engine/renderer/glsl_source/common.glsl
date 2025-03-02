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

/* Bit 0: color * 1
Bit 1: color * ( -1 )
Bit 2: color += lightFactor
Bit 3: alpha * 1
Bit 4: alpha * ( -1 )
Bit 5: alpha = 1
Bit 6-9: lightFactor */

float colorModArray[3] = float[3] ( 0.0f, 1.0f, -1.0f );

vec4 ColorModulateToColor( const in uint colorMod ) {
	vec4 colorModulate = vec4( colorModArray[colorMod & 3] );
	colorModulate.a = ( colorModArray[( colorMod & 24 ) >> 3] );
	return colorModulate;
}

vec4 ColorModulateToColor( const in uint colorMod, const in float lightFactor ) {
	vec4 colorModulate = vec4( colorModArray[colorMod & 3] + ( ( colorMod & 4 ) >> 2 ) * lightFactor );
	colorModulate.a = ( colorModArray[( colorMod & 24 ) >> 3] );
	return colorModulate;
}

float ColorModulateToLightFactor( const in uint colorMod ) {
	return float( ( colorMod >> 6 ) & 0xF );
}

// This is used to skip vertex colours if the colorMod doesn't need them
bool ColorModulateToVertexColor( const in uint colorMod ) {
	return ( colorMod & 32 ) == 32;
}
