/*
===========================================================================

Daemon BSD Source Code
Copyright (c) 2024 Daemon Developers
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

/* cull_cp.glsl */

#insert common_cp
#insert material_cp

// Keep this to 64 because we don't want extra shared mem etc. to be allocated, and to minimize wasted lanes
layout (local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(binding = 0) uniform sampler2D depthImage;

layout(std430, binding = BIND_SURFACE_DESCRIPTORS) readonly restrict buffer surfaceDescriptorsSSBO {
	SurfaceDescriptor surfaces[];
};

layout(std430, binding = BIND_SURFACE_COMMANDS) writeonly restrict buffer surfaceCommandsSSBO {
	SurfaceCommand surfaceCommands[];
};

layout(std430, binding = BIND_PORTAL_SURFACES) restrict buffer portalSurfacesSSBO {
	PortalSurface portalSurfaces[];
};

#if defined( r_materialDebug )
	#define DEBUG_INVOCATION_SIZE 5
	#define DEBUG_ID( id ) ( id * DEBUG_INVOCATION_SIZE )

	layout(std430, binding = BIND_DEBUG) writeonly restrict buffer debugSSBO {
		uvec4 debug[];
	};
#endif

uniform uint u_Frame;
uniform uint u_ViewID;
uniform uint u_TotalDrawSurfs;
uniform uint u_SurfaceCommandsOffset;
uniform vec4 u_Frustum[6]; // xyz - normal, w - distance
uniform bool u_UseFrustumCulling;
uniform bool u_UseOcclusionCulling;
uniform vec3 u_CameraPosition;
uniform uint u_FirstPortalGroup;
uniform uint u_TotalPortals;
uniform mat4 u_ModelViewMatrix;
uniform uint u_ViewWidth;
uniform uint u_ViewHeight;
uniform float u_P00;
uniform float u_P11;

// Based on https://zeux.io/2023/01/12/approximate-projected-bounds/
bool ProjectSphere( in vec3 origin, in float radius, in float zNear, in float P00, in float P11, inout vec4 boundingBox ) {
	if ( -origin.z - radius < zNear ) {
		return false;
	}

	vec3 cr = origin * radius;
	float czr2 = origin.z * origin.z - radius * radius;

	float vx = sqrt( origin.x * origin.x + czr2 );
	float minx = origin.x * origin.x + czr2 >= 0.0 ? ( vx * origin.x - cr.z ) / ( vx * origin.z + cr.x ) : -1.0;
	float maxx = origin.x * origin.x + czr2 >= 0.0 ? ( vx * origin.x + cr.z ) / ( vx * origin.z - cr.x ) : 1.0;

	float vy = sqrt( origin.y * origin.y + czr2 );
	float miny = origin.y * origin.y + czr2 >= 0.0 ? ( vy * origin.y - cr.z ) / ( vy * origin.z + cr.y ) : -1.0;
	float maxy = origin.y * origin.y + czr2 >= 0.0 ? ( vy * origin.y + cr.z ) / ( vy * origin.z - cr.y ) : 1.0;

	boundingBox = vec4( minx * P00, miny * P11, maxx * P00, maxy * P11 );
	boundingBox = boundingBox.xwzy * vec4( 0.5f, -0.5f, 0.5f, -0.5f ) + vec4( 0.5, 0.5, 0.5, 0.5 ); // clip space -> uv space

	return true;
}

bool CullSurface( in BoundingSphere boundingSphere ) {
	bool culled = false;

	// Frustum culling
	// Skip far plane because we always make it encompass the whole map in the current direction
	// This might need to be changed later for shadowmaps since lights could have some far plane set
	if( u_UseFrustumCulling ) {
		for( int i = 0; i < 5; i++ ) {
			const float distance = dot( u_Frustum[i].xyz, boundingSphere.origin ) - u_Frustum[i].w;

			if( distance < -boundingSphere.radius ) {
				culled = true;
				break;
			}
		}
	}
	
	// Occlusion culling for surfaces that passed frustum culling
	vec4 boundingBox = vec4( -1.0, 1.0, -1.0, 1.0 );
	const vec3 viewSpaceCenter = vec3( u_ModelViewMatrix * vec4( boundingSphere.origin, 1.0 ) );
	
	// Only do occlusion culling on the main view because portals use stencil buffer,
	// so doing depth reduction there would be complicated

	// ProjectSphere() gives us the screen-space AABB of the surface, based on its bounding sphere
	if( u_UseOcclusionCulling && u_ViewID == 0 && !culled
		&& ProjectSphere( viewSpaceCenter, boundingSphere.radius, r_zNear, u_P00, u_P11, boundingBox ) ) {
		boundingBox.xz = vec2( min( 1 - boundingBox.x, 1 - boundingBox.z ), max( 1 - boundingBox.x, 1 - boundingBox.z ) );
		const float width = clamp( ( boundingBox.z - boundingBox.x ) * float( u_ViewWidth ), 0.0, float( u_ViewWidth ) );
		const float height = clamp( ( boundingBox.w - boundingBox.y ) * float( u_ViewHeight ), 0.0, float( u_ViewHeight ) );

		// Sample the depth-reduction image at the mip-level where the surface covers 4 pixels
		const int level = int( floor( log2( max( width, height ) ) ) );

		// Coords for the 4 pixels covered by the AABB, adjusted for partially off-screen surfaces as needed
		vec2 surfaceCoords = vec2( u_ViewWidth >> level, u_ViewHeight >> level );
		surfaceCoords *= ( boundingBox.xy + boundingBox.zw ) * 0.5;
		const ivec2 surfaceCoordsFloor = ivec2( clamp( surfaceCoords.x, 0.0, float( u_ViewWidth >> level ) ), clamp( surfaceCoords.y, 0.0, float( u_ViewHeight >> level ) ) );
		ivec4 depthCoords = ivec4( surfaceCoordsFloor.xy,
								   surfaceCoordsFloor.x + ( surfaceCoords.x - surfaceCoordsFloor.x >= 0.5 ? 1 : -1 ),
								   surfaceCoordsFloor.y + ( surfaceCoords.y - surfaceCoordsFloor.y >= 0.5 ? 1 : -1 ) );
		depthCoords.x = clamp( depthCoords.x, 0, int( ( u_ViewWidth >> level ) - 1 ) );
		depthCoords.y = clamp( depthCoords.y, 0, int( ( u_ViewHeight >> level ) - 1 ) );
		depthCoords.z = clamp( depthCoords.z, 0, int( ( u_ViewWidth >> level ) - 1 ) );
		depthCoords.w = clamp( depthCoords.w, 0, int( ( u_ViewHeight >> level ) - 1 ) );

		vec4 depthValues;
		depthValues.x = texelFetch( depthImage, depthCoords.xy, level ).r;
		depthValues.y = texelFetch( depthImage, depthCoords.zy, level ).r;
		depthValues.z = texelFetch( depthImage, depthCoords.xw, level ).r;
		depthValues.w = texelFetch( depthImage, depthCoords.zw, level ).r;

		const float surfaceDepth = max( max( max( depthValues.x, depthValues.y ), depthValues.z ), depthValues.w );

		culled = ( 1 + r_zNear / ( viewSpaceCenter.z + boundingSphere.radius ) ) > surfaceDepth;
	}

	return culled;
}

void ProcessSurfaceCommands( const in SurfaceDescriptor surface, const in bool enabled ) {
	for( uint i = 0; i < MAX_SURFACE_COMMANDS; i++ ) {
		const uint commandID = surface.surfaceCommandIDs[i];
		if( commandID == 0 ) { // Reserved for no-command
			return;
		}
		// Subtract 1 because of no-command
		surfaceCommands[commandID + u_SurfaceCommandsOffset - 1].enabled = enabled;
		
		#if defined( r_materialDebug )
			debug[DEBUG_ID( GLOBAL_INVOCATION_ID ) + 1][i] = commandID;
		#endif
	}
}

void main() {
	const uint globalGroupID = GLOBAL_GROUP_ID;
	const uint globalInvocationID = GLOBAL_INVOCATION_ID;

	// Portals
	const uint portalID = globalInvocationID - u_FirstPortalGroup * 64;
	if( globalGroupID >= u_FirstPortalGroup && ( portalID < u_TotalPortals ) ) {
		const uint portalSurfaceID = portalID + ( u_Frame * MAX_VIEWS + u_ViewID ) * u_TotalPortals;
		PortalSurface surface = portalSurfaces[portalSurfaceID];
		bool culled = CullSurface( surface.boundingSphere );

		portalSurfaces[portalSurfaceID].distance = !culled ? distance( u_CameraPosition, surface.boundingSphere.origin ) : -1.0;
		return;
	}

	// Regular surfaces
	if( globalInvocationID >= u_TotalDrawSurfs ) {
		return;
	}
	SurfaceDescriptor surface = surfaces[globalInvocationID];
	bool culled = CullSurface( surface.boundingSphere );

	ProcessSurfaceCommands( surface, !culled );
	
	#if defined( r_materialDebug )
		debug[DEBUG_ID( globalInvocationID )].x = culled ? 1 : 0;
	#endif
}
