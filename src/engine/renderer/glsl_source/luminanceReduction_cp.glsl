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

/* adaptiveLightingReduction_cp.glsl */

#insert common_cp

// Keep this to 8x8 because we don't want extra shared mem etc. to be allocated, and to minimize wasted lanes
layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0) uniform sampler2D renderImage;

uniform uint u_ViewWidth;
uniform uint u_ViewHeight;

/* x: log2( HDRMax )
y: reserved
z: reserved
w: reserved */
uniform vec4 u_TonemapParms2;

layout (binding = BIND_LUMINANCE) uniform atomic_uint atomicLuminance;

float ColorToLuminance( const in vec3 color ) {
    float luminance = dot( color.rgb, vec3( 0.2126f, 0.7152f, 0.0722f ) ); // sRGB luminance

    return luminance > 0.0f ? clamp( log2( luminance ) + 8, 0.0, u_TonemapParms2.x + 8 ) : 0.0f;
}

uint FloatLuminanceToUint( const in float luminance ) {
    return uint( luminance * 256 );
}

void main() {
    const ivec2 position = ivec2( gl_GlobalInvocationID.xy );
    if( position.x >= u_ViewWidth || position.y >= u_ViewHeight ) {
        return;
    };
    
    const float luminance = ColorToLuminance( texelFetch( renderImage, position, 0 ).rgb );

	#if defined(HAVE_subgroup_atomic)
		const float luminanceSum = subgroupInclusiveAdd( luminance );
		
		if( subgroupElect() ) {
			atomicCounterAddARB( atomicLuminance, FloatLuminanceToUint( luminanceSum ) );
		}
    #else
        atomicCounterAddARB( atomicLuminance, FloatLuminanceToUint( luminance ) );
    #endif
}
