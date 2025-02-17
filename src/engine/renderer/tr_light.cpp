/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2006-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

This file is part of Daemon source code.

Daemon source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Daemon source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_light.c
#include "tr_local.h"

/*
=============
R_AddBrushModelInteractions

Determine which dynamic lights may effect this bmodel
=============
*/
void R_AddBrushModelInteractions( trRefEntity_t *ent, trRefLight_t *light, interactionType_t iaType )
{
	bspSurface_t      *surf;
	bspModel_t        *bspModel = nullptr;
	model_t           *pModel = nullptr;
	byte              cubeSideBits;

	// cull the entire model if it is outside the view frustum
	// and we don't care about proper shadowing
	if ( ent->cull == cullResult_t::CULL_OUT )
	{
		iaType = (interactionType_t) (iaType & ~IA_LIGHT);
	}

	if ( !iaType )
	{
		return;
	}

	// avoid drawing of certain objects
#if defined( USE_REFENTITY_NOSHADOWID )

	if ( light->l.inverseShadows )
	{
		if ( (iaType & IA_SHADOW) && ( light->l.noShadowID && ( light->l.noShadowID != ent->e.noShadowID ) ) )
		{
			return;
		}
	}
	else
	{
		if ( (iaType & IA_SHADOW) && ( light->l.noShadowID && ( light->l.noShadowID == ent->e.noShadowID ) ) )
		{
			return;
		}
	}

#endif

	pModel = R_GetModelByHandle( ent->e.hModel );
	bspModel = pModel->bsp;

	// do a quick AABB cull
	if ( !BoundsIntersect( light->worldBounds[ 0 ], light->worldBounds[ 1 ], ent->worldBounds[ 0 ], ent->worldBounds[ 1 ] ) )
	{
		tr.pc.c_dlightSurfacesCulled += bspModel->numSurfaces;
		return;
	}

	// do a more expensive and precise light frustum cull
	if ( !r_noLightFrustums->integer )
	{
		if ( R_CullLightWorldBounds( light, ent->worldBounds ) == cullResult_t::CULL_OUT )
		{
			tr.pc.c_dlightSurfacesCulled += bspModel->numSurfaces;
			return;
		}
	}

	cubeSideBits = R_CalcLightCubeSideBits( light, ent->worldBounds );

	// set the light bits in all the surfaces
	for (unsigned i = 0; i < bspModel->numSurfaces; i++ )
	{
		surf = bspModel->firstSurface + i;

		// skip all surfaces that don't matter for lighting only pass
		if ( surf->shader->isSky || ( !surf->shader->interactLight && surf->shader->noShadows ) )
		{
			continue;
		}

		R_AddLightInteraction( light, surf->data, surf->shader, cubeSideBits, iaType );
		tr.pc.c_dlightSurfaces++;
	}
}

/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

float R_InterpolateLightGrid( world_t *w, int from[3], int to[3],
			      float *factors[3], vec3_t ambientLight,
			      vec3_t directedLight, vec3_t lightDir ) {
	float           totalFactor = 0.0f, factor;
	float           *xFactor, *yFactor, *zFactor;
	int             gridStep[ 3 ];
	int             x, y, z;
	bspGridPoint1_t *gp1;
	bspGridPoint2_t *gp2;

	VectorClear( ambientLight );
	VectorClear( directedLight );
	VectorClear( lightDir );

	gridStep[ 0 ] = 1;
	gridStep[ 1 ] = w->lightGridBounds[ 0 ];
	gridStep[ 2 ] = gridStep[ 1 ] * w->lightGridBounds[ 1 ];

	for( x = from[ 0 ], xFactor = factors[ 0 ]; x <= to[ 0 ];
	     x++, xFactor++ ) {
		if( x < 0 || x > w->lightGridBounds[ 0 ] )
			continue;

		for( y = from[ 1 ], yFactor = factors[ 1 ]; y <= to[ 1 ];
		     y++, yFactor++ ) {
			if( y < 0 || y > w->lightGridBounds[ 1 ] )
				continue;

			for( z = from[ 2 ], zFactor = factors[ 2 ]; z <= to[ 2 ];
			     z++, zFactor++ ) {
				if( z < 0 || z > w->lightGridBounds[ 2 ] )
					continue;

				gp1 = w->lightGridData1 + x * gridStep[ 0 ] + y * gridStep[ 1 ] + z * gridStep[ 2 ];
				gp2 = w->lightGridData2 + x * gridStep[ 0 ] + y * gridStep[ 1 ] + z * gridStep[ 2 ];

				if ( !( gp1->color[ 0 ] || gp1->color[ 1 ] || gp1->color[ 2 ]) )
				{
					continue; // ignore samples in walls
				}

				factor = *xFactor * *yFactor * *zFactor;

				totalFactor += factor;

				lightDir[ 0 ] += factor * snorm8ToFloat( gp2->direction[ 0 ] - 128 );
				lightDir[ 1 ] += factor * snorm8ToFloat( gp2->direction[ 1 ] - 128 );
				lightDir[ 2 ] += factor * snorm8ToFloat( gp2->direction[ 2 ] - 128 );

				float ambientScale = 2.0f * unorm8ToFloat( gp1->ambientPart );
				float directedScale = 2.0f - ambientScale;

				ambientLight[ 0 ] += factor * ambientScale * unorm8ToFloat( gp1->color[ 0 ] );
				ambientLight[ 1 ] += factor * ambientScale * unorm8ToFloat( gp1->color[ 1 ] );
				ambientLight[ 2 ] += factor * ambientScale * unorm8ToFloat( gp1->color[ 2 ] );
				directedLight[ 0 ] += factor * directedScale * unorm8ToFloat( gp1->color[ 0 ] );
				directedLight[ 1 ] += factor * directedScale * unorm8ToFloat( gp1->color[ 1 ] );
				directedLight[ 2 ] += factor * directedScale * unorm8ToFloat( gp1->color[ 2 ] );
			}
		}
	}

	return totalFactor;
}

/*
=================
R_LightForPoint
=================
*/
int R_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir )
{
	int             i;
	int             from[ 3 ], to[ 3 ];
	float           frac[ 3 ][ 2 ];
	float           *factors[ 3 ] = { frac[ 0 ], frac[ 1 ], frac[ 2 ] };
	float           totalFactor;

	// bk010103 - this segfaults with -nolight maps
	if ( tr.world->lightGridData1 == nullptr ||
	     tr.world->lightGridData2 == nullptr )
	{
		return false;
	}

	for ( i = 0; i < 3; i++ )
	{
		float v;

		v = point[ i ] - tr.world->lightGridOrigin[ i ];
		v *= tr.world->lightGridInverseSize[ i ];
		from[ i ] = floor( v );
		frac[ i ][ 0 ] = v - from[ i ];

		if ( from[ i ] < 0 )
		{
			from[ i ] = 0;
			frac[ i ][ 0 ] = 0.0f;
		}
		else if ( from[ i ] >= tr.world->lightGridBounds[ i ] - 1 )
		{
			from[ i ] = tr.world->lightGridBounds[ i ] - 2;
			frac[ i ][ 0 ] = 1.0f;
		}

		to[ i ] = from[ i ] + 1;
		frac[ i ][ 1 ] = 1.0f - frac[ i ][ 0 ];
	}

	totalFactor = R_InterpolateLightGrid( tr.world, from, to, factors,
					      ambientLight, directedLight,
					      lightDir );

	if ( totalFactor > 0 && totalFactor < 0.99 )
	{
		totalFactor = 1.0f / totalFactor;

		VectorScale( lightDir, totalFactor, lightDir );
		VectorScale( ambientLight, totalFactor, ambientLight );
		VectorScale( directedLight, totalFactor, directedLight );
	}

	// compute z-component of direction
	lightDir[ 2 ] = 1.0f - fabsf( lightDir[ 0 ] ) - fabsf( lightDir[ 1 ] );
	if( lightDir[ 2 ] < 0.0f ) {
		float X = lightDir[ 0 ];
		float Y = lightDir[ 1 ];
		lightDir[ 0 ] = copysignf( 1.0f - fabsf( Y ), X );
		lightDir[ 1 ] = copysignf( 1.0f - fabsf( X ), Y );
	}

	VectorNormalize( lightDir );

	if ( ambientLight[ 0 ] < r_forceAmbient.Get() &&
	     ambientLight[ 1 ] < r_forceAmbient.Get() &&
	     ambientLight[ 2 ] < r_forceAmbient.Get() )
	{
		ambientLight[ 0 ] = r_forceAmbient.Get();
		ambientLight[ 1 ] = r_forceAmbient.Get();
		ambientLight[ 2 ] = r_forceAmbient.Get();
	}

	return true;
}

/*
=================
R_SetupLightOrigin
Tr3B - needs finished transformMatrix
=================
*/
void R_SetupLightOrigin( trRefLight_t *light )
{
	vec3_t transformed;

	if ( light->l.rlType == refLightType_t::RL_DIRECTIONAL )
	{
		if ( !VectorCompare( light->l.center, vec3_origin ) )
		{
			MatrixTransformPoint( light->transformMatrix, light->l.center, transformed );
			VectorSubtract( transformed, light->l.origin, light->direction );
			VectorNormalize( light->direction );

			VectorMA( light->l.origin, 10000, light->direction, light->origin );
		}
		else
		{
			vec3_t down = { 0, 0, 1 };

			MatrixTransformPoint( light->transformMatrix, down, transformed );
			VectorSubtract( transformed, light->l.origin, light->direction );
			VectorNormalize( light->direction );

			VectorMA( light->l.origin, 10000, light->direction, light->origin );

			VectorCopy( light->l.origin, light->origin );
		}
	}
	else
	{
		MatrixTransformPoint( light->transformMatrix, light->l.center, light->origin );
	}
}

/*
=================
R_SetupLightLocalBounds
=================
*/
void R_SetupLightLocalBounds( trRefLight_t *light )
{
	switch ( light->l.rlType )
	{
		case refLightType_t::RL_OMNI:
		case refLightType_t::RL_DIRECTIONAL:
			{
				light->localBounds[ 0 ][ 0 ] = -light->l.radius;
				light->localBounds[ 0 ][ 1 ] = -light->l.radius;
				light->localBounds[ 0 ][ 2 ] = -light->l.radius;
				light->localBounds[ 1 ][ 0 ] = light->l.radius;
				light->localBounds[ 1 ][ 1 ] = light->l.radius;
				light->localBounds[ 1 ][ 2 ] = light->l.radius;
				break;
			}

		case refLightType_t::RL_PROJ:
			{
				int    j;
				vec3_t farCorners[ 4 ];

				ClearBounds( light->localBounds[ 0 ], light->localBounds[ 1 ] );

				// transform frustum from world space to local space
				R_CalcFrustumFarCorners( light->localFrustum, farCorners );

				if ( !VectorCompare( light->l.projStart, vec3_origin ) )
				{
					vec3_t nearCorners[ 4 ];

					// calculate the vertices defining the top area
					R_CalcFrustumNearCorners( light->localFrustum, nearCorners );

					for ( j = 0; j < 4; j++ )
					{
						AddPointToBounds( farCorners[ j ], light->localBounds[ 0 ], light->localBounds[ 1 ] );
						AddPointToBounds( nearCorners[ j ], light->localBounds[ 0 ], light->localBounds[ 1 ] );
					}
				}
				else
				{
					vec3_t top;
					const plane_t* frustum = light->localFrustum;

					PlanesGetIntersectionPoint( frustum[ FRUSTUM_LEFT ], frustum[ FRUSTUM_RIGHT ], frustum[ FRUSTUM_TOP ], top );
					AddPointToBounds( top, light->localBounds[ 0 ], light->localBounds[ 1 ] );

					for ( j = 0; j < 4; j++ )
					{
						AddPointToBounds( farCorners[ j ], light->localBounds[ 0 ], light->localBounds[ 1 ] );
					}
				}

				break;
			}

		default:
			break;
	}

	light->sphereRadius = RadiusFromBounds( light->localBounds[ 0 ], light->localBounds[ 1 ] );
}

/*
=================
R_SetupLightWorldBounds
Tr3B - needs finished transformMatrix
=================
*/
void R_SetupLightWorldBounds( trRefLight_t *light )
{
	MatrixTransformBounds(light->transformMatrix, light->localBounds[0], light->localBounds[1], light->worldBounds[0], light->worldBounds[1]);
}

/*
=================
R_SetupLightView
=================
*/
void R_SetupLightView( trRefLight_t *light )
{
	switch ( light->l.rlType )
	{
		case refLightType_t::RL_OMNI:
		case refLightType_t::RL_PROJ:
		case refLightType_t::RL_DIRECTIONAL:
			{
				MatrixAffineInverse( light->transformMatrix, light->viewMatrix );
				break;
			}
		default:
			Sys::Drop( "R_SetupLightView: Bad rlType" );
	}
}

void R_TessLight( const trRefLight_t *light, const Color::Color& color, bool use_default_color )
{
	int j;

	switch( light->l.rlType )
	{
		case refLightType_t::RL_OMNI:
		case refLightType_t::RL_DIRECTIONAL:
			Tess_AddCube( vec3_origin, light->localBounds[ 0 ], light->localBounds[ 1 ], use_default_color ? Color::White : color );
			break;
		case refLightType_t::RL_PROJ:
			{
				vec3_t farCorners[ 4 ];
				vec4_t quadVerts[ 4 ];

				R_CalcFrustumFarCorners( light->localFrustum, farCorners );

				if ( !VectorCompare( light->l.projStart, vec3_origin ) )
				{
					vec3_t nearCorners[ 4 ];

					// calculate the vertices defining the top area
					R_CalcFrustumNearCorners( light->localFrustum, nearCorners );

					// draw outer surfaces
					for ( j = 0; j < 4; j++ )
					{
						Vector4Set( quadVerts[ 0 ], nearCorners[ j ][ 0 ], nearCorners[ j ][ 1 ], nearCorners[ j ][ 2 ], 1 );
						Vector4Set( quadVerts[ 1 ], farCorners[ j ][ 0 ], farCorners[ j ][ 1 ], farCorners[ j ][ 2 ], 1 );
						Vector4Set( quadVerts[ 2 ], farCorners[( j + 1 ) % 4 ][ 0 ], farCorners[( j + 1 ) % 4 ][ 1 ], farCorners[( j + 1 ) % 4 ][ 2 ], 1 );
						Vector4Set( quadVerts[ 3 ], nearCorners[( j + 1 ) % 4 ][ 0 ], nearCorners[( j + 1 ) % 4 ][ 1 ], nearCorners[( j + 1 ) % 4 ][ 2 ], 1 );
						Tess_AddQuadStamp2( quadVerts, use_default_color ? Color::Cyan : color );
					}

					// draw far cap
					Vector4Set( quadVerts[ 0 ], farCorners[ 3 ][ 0 ], farCorners[ 3 ][ 1 ], farCorners[ 3 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 1 ], farCorners[ 2 ][ 0 ], farCorners[ 2 ][ 1 ], farCorners[ 2 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 2 ], farCorners[ 1 ][ 0 ], farCorners[ 1 ][ 1 ], farCorners[ 1 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 3 ], farCorners[ 0 ][ 0 ], farCorners[ 0 ][ 1 ], farCorners[ 0 ][ 2 ], 1 );
					Tess_AddQuadStamp2( quadVerts, use_default_color ? Color::Red : color );

					// draw near cap
					Vector4Set( quadVerts[ 0 ], nearCorners[ 0 ][ 0 ], nearCorners[ 0 ][ 1 ], nearCorners[ 0 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 1 ], nearCorners[ 1 ][ 0 ], nearCorners[ 1 ][ 1 ], nearCorners[ 1 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 2 ], nearCorners[ 2 ][ 0 ], nearCorners[ 2 ][ 1 ], nearCorners[ 2 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 3 ], nearCorners[ 3 ][ 0 ], nearCorners[ 3 ][ 1 ], nearCorners[ 3 ][ 2 ], 1 );
					Tess_AddQuadStamp2( quadVerts, use_default_color ? Color::Green : color );
				}
				else
				{
					vec3_t top;
					const plane_t* frustum = light->localFrustum;

					// no light_start, just use the top vertex (doesn't need to be mirrored)
					PlanesGetIntersectionPoint( frustum[ FRUSTUM_LEFT ], frustum[ FRUSTUM_RIGHT ], frustum[ FRUSTUM_TOP ], top );

					// draw pyramid
					for ( j = 0; j < 4; j++ )
					{
						Color::Color32Bit iColor = use_default_color ? Color::Cyan : color;

						VectorCopy( top, tess.verts[ tess.numVertexes ].xyz );
						tess.verts[ tess.numVertexes ].color = iColor;
						tess.indexes[ tess.numIndexes++ ] = tess.numVertexes;
						tess.numVertexes++;

						VectorCopy( farCorners[( j + 1 ) % 4 ], tess.verts[ tess.numVertexes ].xyz );
						tess.verts[ tess.numVertexes ].color = iColor;
						tess.indexes[ tess.numIndexes++ ] = tess.numVertexes;
						tess.numVertexes++;

						VectorCopy( farCorners[ j ], tess.verts[ tess.numVertexes ].xyz );
						tess.verts[ tess.numVertexes ].color = iColor;
						tess.indexes[ tess.numIndexes++ ] = tess.numVertexes;
						tess.numVertexes++;
					}

					Vector4Set( quadVerts[ 0 ], farCorners[ 0 ][ 0 ], farCorners[ 0 ][ 1 ], farCorners[ 0 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 1 ], farCorners[ 1 ][ 0 ], farCorners[ 1 ][ 1 ], farCorners[ 1 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 2 ], farCorners[ 2 ][ 0 ], farCorners[ 2 ][ 1 ], farCorners[ 2 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 3 ], farCorners[ 3 ][ 0 ], farCorners[ 3 ][ 1 ], farCorners[ 3 ][ 2 ], 1 );
					Tess_AddQuadStamp2( quadVerts, use_default_color ? Color::Red : color );
				}
			}
			break;
		default:
			break;
	}
}

void R_TessLight( const trRefLight_t *light, const Color::Color& color )
{
    R_TessLight ( light, color, false );
}

void R_TessLight( const trRefLight_t *light )
{
    R_TessLight ( light, Color::Color(), true );
}

/*
=================
R_SetupLightFrustum
=================
*/
void R_SetupLightFrustum( trRefLight_t *light )
{
	switch ( light->l.rlType )
	{
		case refLightType_t::RL_OMNI:
		case refLightType_t::RL_DIRECTIONAL:
			{
				int    i;
				vec3_t planeNormal;
				vec3_t planeOrigin;
				axis_t axis;

				QuatToAxis( light->l.rotation, axis );

				for ( i = 0; i < 3; i++ )
				{
					VectorMA( light->l.origin, light->l.radius, axis[ i ], planeOrigin );
					VectorNegate( axis[ i ], planeNormal );
					VectorNormalize( planeNormal );

					VectorCopy( planeNormal, light->frustum[ i ].normal );
					light->frustum[ i ].dist = DotProduct( planeOrigin, planeNormal );
				}

				for ( i = 0; i < 3; i++ )
				{
					VectorMA( light->l.origin, -light->l.radius, axis[ i ], planeOrigin );
					VectorCopy( axis[ i ], planeNormal );
					VectorNormalize( planeNormal );

					VectorCopy( planeNormal, light->frustum[ i + 3 ].normal );
					light->frustum[ i + 3 ].dist = DotProduct( planeOrigin, planeNormal );
				}

				for ( i = 0; i < 6; i++ )
				{
					vec_t length, ilength;

					light->frustum[ i ].type = PLANE_NON_AXIAL;

					// normalize
					length = VectorLength( light->frustum[ i ].normal );

					if ( length )
					{
						ilength = 1.0f / length;
						light->frustum[ i ].normal[ 0 ] *= ilength;
						light->frustum[ i ].normal[ 1 ] *= ilength;
						light->frustum[ i ].normal[ 2 ] *= ilength;
						light->frustum[ i ].dist *= ilength;
					}

					SetPlaneSignbits( &light->frustum[ i ] );
				}

				break;
			}

		case refLightType_t::RL_PROJ:
			{
				int    i;

				// transform local frustum to world space
				plane_t worldFrustum[ 6 ];
				for ( i = 0; i < 6; i++ )
				{
					MatrixTransformPlane( light->transformMatrix, light->localFrustum[ i ], worldFrustum[ i ] );
				}

				// normalize all frustum planes
				for ( i = 0; i < 6; i++ )
				{
					PlaneNormalize( worldFrustum[ i ] );

					VectorCopy( worldFrustum[ i ].normal, light->frustum[ i ].normal );
					light->frustum[ i ].dist = worldFrustum[ i ].dist;

					light->frustum[ i ].type = PLANE_NON_AXIAL;

					SetPlaneSignbits( &light->frustum[ i ] );
				}

				break;
			}

		default:
			break;
	}
}

/*
=================
R_SetupLightProjection
=================
*/
// *INDENT-OFF*
void R_SetupLightProjection( trRefLight_t *light )
{
	switch ( light->l.rlType )
	{
		case refLightType_t::RL_OMNI:
		case refLightType_t::RL_DIRECTIONAL:
			{
				MatrixSetupScale( light->projectionMatrix, 1.0f / light->l.radius, 1.0f / light->l.radius, 1.0f / light->l.radius );
				break;
			}

		case refLightType_t::RL_PROJ:
			{
				plane_t lightProject[ 4 ];

				{
					vec3_t right, up, normal;

					float rLen = VectorNormalize2( light->l.projRight, right );
					float uLen = VectorNormalize2( light->l.projUp, up );

					CrossProduct( up, right, normal );
					VectorNormalize( normal );

					vec_t dist = DotProduct( light->l.projTarget, normal );

					if ( dist < 0 )
					{
						dist = -dist;
						VectorInverse( normal );
					}

					VectorScale( right, ( 0.5f * dist ) / rLen, right );
					VectorScale( up, - ( 0.5f * dist ) / uLen, up );

					PlaneSet( lightProject[ 0 ], right[ 0 ], right[ 1 ], right[ 2 ], 0 );
					PlaneSet( lightProject[ 1 ], up[ 0 ], up[ 1 ], up[ 2 ], 0 );
					PlaneSet( lightProject[ 2 ], normal[ 0 ], normal[ 1 ], normal[ 2 ], 0 );
				}

				// now offset to center
				{
					vec3_t targetGlobal;
					VectorSet( targetGlobal,
						light->l.projTarget[ 0 ], light->l.projTarget[ 1 ],
						light->l.projTarget[ 2 ] );

					{
						vec_t a = DotProduct( targetGlobal, lightProject[ 0 ].normal );
						vec_t b = DotProduct( targetGlobal, lightProject[ 2 ].normal );
						vec_t ofs = 0.5 - a / b;

						VectorMA( lightProject[ 0 ].normal, ofs, lightProject[ 2 ].normal, lightProject[ 0 ].normal );
					}

					{
						vec_t a = DotProduct( targetGlobal, lightProject[ 1 ].normal );
						vec_t b = DotProduct( targetGlobal, lightProject[ 2 ].normal );
						vec_t ofs = 0.5 - a / b;

						VectorMA( lightProject[ 0 ].normal, ofs, lightProject[ 2 ].normal, lightProject[ 0 ].normal );
					}
				}

				{
					vec3_t start;

					if ( !VectorCompare( light->l.projStart, vec3_origin ) )
					{
						VectorCopy( light->l.projStart, start );
					}
					else
					{
						VectorClear( start );
					}

					vec3_t stop;

					if ( !VectorCompare( light->l.projEnd, vec3_origin ) )
					{
						VectorCopy( light->l.projEnd, stop );
					}
					else
					{
						VectorCopy( light->l.projTarget, stop );
					}

					// Calculate the falloff vector
					vec3_t falloff;

					{
						VectorSubtract( stop, start, falloff );

						vec_t falloffLen = VectorNormalize( falloff );
						light->falloffLength = falloffLen;

						if ( falloffLen <= 0 )
						{
							falloffLen = 1;
						}

						//FIXME ?
						VectorScale( falloff, 1.0f / falloffLen, falloff );
					}

					PlaneSet( lightProject[ 3 ], falloff, -DotProduct( start, falloff ) );
				}

				// we want the planes of s=0, s=q, t=0, and t=q
				plane_t *frustum = light->localFrustum;

				frustum[ FRUSTUM_LEFT ] = lightProject[ 0 ];

				frustum[ FRUSTUM_BOTTOM ] = lightProject[ 1 ];

				{
					vec3_t normal;
					VectorSubtract( lightProject[ 2 ].normal, lightProject[ 0 ].normal, normal );

					PlaneSet( frustum[ FRUSTUM_RIGHT ], normal, 0 );
				}

				{
					vec3_t normal;
					VectorSubtract( lightProject[ 2 ].normal, lightProject[ 1 ].normal, normal );

					PlaneSet( frustum[ FRUSTUM_TOP ], normal, 0 );
				}

				{
					// we want the planes of s=0 and s=1 for front and rear clipping planes
					frustum[ FRUSTUM_NEAR ] = lightProject[ 3 ];
				}

				{
					vec3_t normal;
					VectorNegate( lightProject[ 3 ].normal, normal );
					vec_t dist = - lightProject[ 3 ].dist - 1.0f;

					PlaneSet( frustum[ FRUSTUM_FAR ], normal, dist );
				}

				{
					// calculate the new projection matrix from the frustum planes
					float *proj = light->projectionMatrix;
					MatrixFromPlanes( proj, frustum[ FRUSTUM_LEFT ], frustum[ FRUSTUM_RIGHT ], frustum[ FRUSTUM_BOTTOM ], frustum[ FRUSTUM_TOP ], frustum[ FRUSTUM_NEAR ], frustum[ FRUSTUM_FAR ] );

					//MatrixMultiply2(proj, newProjection);

					// scale the falloff texture coordinate so that 0.5 is at the apex and 0.0
					// as at the base of the pyramid.
					// TODO: I don't like hacking the matrix like this, but all attempts to use
					// a transformation seemed to affect too many other things.
					//proj[10] *= 0.5f;
				}

				// normalise all frustum planes
				for ( int i = 0; i < 6; i++ )
				{
					PlaneNormalize( frustum[ i ] );
				}

				break;
			}

		default:
			Sys::Drop( "R_SetupLightProjection: Bad rlType" );
	}
}

// *INDENT-ON*

/*
=================
R_AddLightInteraction
=================
*/
bool R_AddLightInteraction( trRefLight_t *light, surfaceType_t *surface, shader_t *surfaceShader, byte cubeSideBits,
                                interactionType_t iaType )
{
	int           iaIndex;
	interaction_t *ia;

	// skip all surfaces that don't matter for lighting only pass
	if ( surfaceShader )
	{
		if ( surfaceShader->isSky || ( !surfaceShader->interactLight && surfaceShader->noShadows ) )
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	// instead of checking for overflow, we just mask the index
	// so it wraps around
	iaIndex = tr.refdef.numInteractions & INTERACTION_MASK;
	ia = &tr.refdef.interactions[ iaIndex ];
	tr.refdef.numInteractions++;

	light->noSort = iaIndex == 0;

	// connect to interaction grid
	if ( !light->firstInteraction )
	{
		light->firstInteraction = ia;
	}

	if ( light->lastInteraction )
	{
		light->lastInteraction->next = ia;
	}

	light->lastInteraction = ia;

	// update counters
	light->numInteractions++;

	if( !(iaType & IA_LIGHT) ) {
		light->numShadowOnlyInteractions++;
	}
	if( !(iaType & (IA_SHADOW | IA_SHADOWCLIP) ) ) {
		light->numLightOnlyInteractions++;
	}

	ia->next = nullptr;

	ia->type = iaType;

	ia->light = light;
	ia->entity = tr.currentEntity;
	ia->surface = surface;
	ia->shader = surfaceShader;
	ia->shaderNum = surfaceShader->sortedIndex;

	ia->cubeSideBits = cubeSideBits;

	ia->scissorX = light->scissor.coords[ 0 ];
	ia->scissorY = light->scissor.coords[ 1 ];
	ia->scissorWidth = light->scissor.coords[ 2 ] - light->scissor.coords[ 0 ];
	ia->scissorHeight = light->scissor.coords[ 3 ] - light->scissor.coords[ 1 ];

	tr.pc.c_dlightInteractions++;

	return true;
}

/*
=================
InteractionCompare
compare function for qsort()
=================
*/
static int InteractionCompare( const void *a, const void *b )
{
	// shader first
	if ( ( ( interaction_t * ) a )->shaderNum < ( ( interaction_t * ) b )->shaderNum )
	{
		return -1;
	}

	else if ( ( ( interaction_t * ) a )->shaderNum > ( ( interaction_t * ) b )->shaderNum )
	{
		return 1;
	}

	// then entity
	if ( ( ( interaction_t * ) a )->entity == &tr.worldEntity && ( ( interaction_t * ) b )->entity != &tr.worldEntity )
	{
		return -1;
	}

	else if ( ( ( interaction_t * ) a )->entity != &tr.worldEntity && ( ( interaction_t * ) b )->entity == &tr.worldEntity )
	{
		return 1;
	}

	else if ( ( ( interaction_t * ) a )->entity < ( ( interaction_t * ) b )->entity )
	{
		return -1;
	}

	else if ( ( ( interaction_t * ) a )->entity > ( ( interaction_t * ) b )->entity )
	{
		return 1;
	}

	return 0;
}

/*
=================
R_SortInteractions
=================
*/
void R_SortInteractions( trRefLight_t *light )
{
	int           i;
	int           iaFirstIndex;
	interaction_t *iaFirst;
	interaction_t *ia;
	interaction_t *iaLast;

	if ( r_noInteractionSort->integer )
	{
		return;
	}

	if ( !light->numInteractions || light->noSort )
	{
		return;
	}

	iaFirst = light->firstInteraction;
	iaFirstIndex = light->firstInteraction - tr.refdef.interactions;

	// sort by material etc. for geometry batching in the renderer backend
	qsort( iaFirst, light->numInteractions, sizeof( interaction_t ), InteractionCompare );

	// fix linked list
	iaLast = nullptr;

	for ( i = 0; i < light->numInteractions; i++ )
	{
		ia = &tr.refdef.interactions[ iaFirstIndex + i ];

		if ( iaLast )
		{
			iaLast->next = ia;
		}

		ia->next = nullptr;

		iaLast = ia;
	}
}

/*
=================
R_IntersectRayPlane
=================
*/
static void R_IntersectRayPlane( const vec3_t v1, const vec3_t v2, cplane_t *plane, vec3_t res )
{
	vec3_t v;
	float  sect;

	VectorSubtract( v1, v2, v );
	sect = - ( DotProduct( plane->normal, v1 ) - plane->dist ) / DotProduct( plane->normal, v );
	VectorScale( v, sect, v );
	VectorAdd( v1, v, res );
}

/*
=================
R_AddPointToLightScissor
=================
*/
static void R_AddPointToLightScissor( trRefLight_t *light, const vec3_t world )
{
	vec4_t eye, clip, normalized, window;

	R_TransformWorldToClip( world, tr.viewParms.world.viewMatrix, tr.viewParms.projectionMatrix, eye, clip );
	R_TransformClipToWindow( clip, &tr.viewParms, normalized, window );

	light->scissor.coords[ 0 ] = std::min( light->scissor.coords[ 0 ], ( int ) window[ 0 ] );
	light->scissor.coords[ 1 ] = std::min( light->scissor.coords[ 1 ], ( int ) window[ 1 ] );
	light->scissor.coords[ 2 ] = std::max( light->scissor.coords[ 2 ], ( int ) window[ 0 ] );
	light->scissor.coords[ 3 ] = std::max( light->scissor.coords[ 3 ], ( int ) window[ 1 ] );
}

static int R_ClipEdgeToPlane( cplane_t plane, const vec3_t in_world1, const vec3_t in_world2, vec3_t out_world1, vec3_t out_world2 )
{
	vec3_t intersect;

	// check edge to frustrum plane
	int side1 = ( ( DotProduct( plane.normal, in_world1 ) - plane.dist ) >= 0.0f );
	int side2 = ( ( DotProduct( plane.normal, in_world2 ) - plane.dist ) >= 0.0f );

	int sides = side1 | ( side2 << 1 );

	// edge completely behind plane
	if ( !sides )
	{
		return 0;
	}

	if ( !side1 || !side2 )
	{
		R_IntersectRayPlane( in_world1, in_world2, &plane, intersect );
	}

	if ( !side1 )
	{
		VectorCopy( intersect, out_world1 );
		VectorCopy( in_world2, out_world2 );
	}
	else if ( !side2 )
	{
		VectorCopy( in_world1, out_world1 );
		VectorCopy( intersect, out_world2 );
	}
	else
	{
		// edge doesn't intersect plane
		VectorCopy( in_world1, out_world1 );
		VectorCopy( in_world2, out_world2 );
	}
	
	return sides;
}

/*
=================
R_AddEdgeToLightScissor
=================
*/
static void R_AddEdgeToLightScissor( trRefLight_t *light, const vec3_t in_world1, const vec3_t in_world2 )
{
	int      i;
	cplane_t *frust;
	vec3_t  clip1, clip2;

	if ( r_lightScissors->integer == 1 )
	{
		// only clip against near plane
		frust = &tr.viewParms.frustums[ 0 ][ FRUSTUM_NEAR ];
		int sides = R_ClipEdgeToPlane( *frust, in_world1, in_world2, clip1, clip2 );
		if ( !sides )
		{
			return;
		}
		R_AddPointToLightScissor( light, clip1 );
		R_AddPointToLightScissor( light, clip2 );
	}
	else if ( r_lightScissors->integer == 2 )
	{
		// clip against all planes
		for ( i = 0; i < FRUSTUM_PLANES; i++ )
		{
			frust = &tr.viewParms.frustums[ 0 ][ i ];

			int sides = R_ClipEdgeToPlane( *frust, in_world1, in_world2, clip1, clip2 );

			if ( !sides )
			{
				continue; // edge behind plane
			}

			R_AddPointToLightScissor( light, clip1 );
			R_AddPointToLightScissor( light, clip2 );
		}
	}
}

/*
=================
R_SetupLightScissor
Recturns the screen space rectangle taken by the box.
        (Clips the box to the near plane to have correct results even if the box intersects the near plane)
Tr3B - recoded from Tenebrae2
=================
*/
void R_SetupLightScissor( trRefLight_t *light )
{
	vec3_t v1, v2;

	light->scissor.coords[ 0 ] = tr.viewParms.viewportX;
	light->scissor.coords[ 1 ] = tr.viewParms.viewportY;
	light->scissor.coords[ 2 ] = tr.viewParms.viewportX + tr.viewParms.viewportWidth;
	light->scissor.coords[ 3 ] = tr.viewParms.viewportY + tr.viewParms.viewportHeight;

	// transform world light corners to eye space -> clip space -> window space
	// and extend the light scissor's mins maxs by resulting window coords
	light->scissor.coords[ 0 ] = 100000000;
	light->scissor.coords[ 1 ] = 100000000;
	light->scissor.coords[ 2 ] = -100000000;
	light->scissor.coords[ 3 ] = -100000000;

	switch ( light->l.rlType )
	{
		case refLightType_t::RL_OMNI:
			{
				// top plane
				VectorSet( v1, light->worldBounds[ 1 ][ 0 ], light->worldBounds[ 1 ][ 1 ], light->worldBounds[ 1 ][ 2 ] );
				VectorSet( v2, light->worldBounds[ 0 ][ 0 ], light->worldBounds[ 1 ][ 1 ], light->worldBounds[ 1 ][ 2 ] );
				R_AddEdgeToLightScissor( light, v1, v2 );

				VectorSet( v1, light->worldBounds[ 1 ][ 0 ], light->worldBounds[ 1 ][ 1 ], light->worldBounds[ 1 ][ 2 ] );
				VectorSet( v2, light->worldBounds[ 1 ][ 0 ], light->worldBounds[ 0 ][ 1 ], light->worldBounds[ 1 ][ 2 ] );
				R_AddEdgeToLightScissor( light, v1, v2 );

				VectorSet( v1, light->worldBounds[ 0 ][ 0 ], light->worldBounds[ 0 ][ 1 ], light->worldBounds[ 1 ][ 2 ] );
				VectorSet( v2, light->worldBounds[ 0 ][ 0 ], light->worldBounds[ 1 ][ 1 ], light->worldBounds[ 1 ][ 2 ] );
				R_AddEdgeToLightScissor( light, v1, v2 );

				VectorSet( v1, light->worldBounds[ 0 ][ 0 ], light->worldBounds[ 0 ][ 1 ], light->worldBounds[ 1 ][ 2 ] );
				VectorSet( v2, light->worldBounds[ 1 ][ 0 ], light->worldBounds[ 0 ][ 1 ], light->worldBounds[ 1 ][ 2 ] );
				R_AddEdgeToLightScissor( light, v1, v2 );

				// bottom plane
				VectorSet( v1, light->worldBounds[ 1 ][ 0 ], light->worldBounds[ 1 ][ 1 ], light->worldBounds[ 0 ][ 2 ] );
				VectorSet( v2, light->worldBounds[ 0 ][ 0 ], light->worldBounds[ 1 ][ 1 ], light->worldBounds[ 0 ][ 2 ] );
				R_AddEdgeToLightScissor( light, v1, v2 );

				VectorSet( v1, light->worldBounds[ 1 ][ 0 ], light->worldBounds[ 1 ][ 1 ], light->worldBounds[ 0 ][ 2 ] );
				VectorSet( v2, light->worldBounds[ 1 ][ 0 ], light->worldBounds[ 0 ][ 1 ], light->worldBounds[ 0 ][ 2 ] );
				R_AddEdgeToLightScissor( light, v1, v2 );

				VectorSet( v1, light->worldBounds[ 0 ][ 0 ], light->worldBounds[ 0 ][ 1 ], light->worldBounds[ 0 ][ 2 ] );
				VectorSet( v2, light->worldBounds[ 0 ][ 0 ], light->worldBounds[ 1 ][ 1 ], light->worldBounds[ 0 ][ 2 ] );
				R_AddEdgeToLightScissor( light, v1, v2 );

				VectorSet( v1, light->worldBounds[ 0 ][ 0 ], light->worldBounds[ 0 ][ 1 ], light->worldBounds[ 0 ][ 2 ] );
				VectorSet( v2, light->worldBounds[ 1 ][ 0 ], light->worldBounds[ 0 ][ 1 ], light->worldBounds[ 0 ][ 2 ] );
				R_AddEdgeToLightScissor( light, v1, v2 );

				// sides
				VectorSet( v1, light->worldBounds[ 0 ][ 0 ], light->worldBounds[ 1 ][ 1 ], light->worldBounds[ 0 ][ 2 ] );
				VectorSet( v2, light->worldBounds[ 0 ][ 0 ], light->worldBounds[ 1 ][ 1 ], light->worldBounds[ 1 ][ 2 ] );
				R_AddEdgeToLightScissor( light, v1, v2 );

				VectorSet( v1, light->worldBounds[ 1 ][ 0 ], light->worldBounds[ 1 ][ 1 ], light->worldBounds[ 0 ][ 2 ] );
				VectorSet( v2, light->worldBounds[ 1 ][ 0 ], light->worldBounds[ 1 ][ 1 ], light->worldBounds[ 1 ][ 2 ] );
				R_AddEdgeToLightScissor( light, v1, v2 );

				VectorSet( v1, light->worldBounds[ 0 ][ 0 ], light->worldBounds[ 0 ][ 1 ], light->worldBounds[ 0 ][ 2 ] );
				VectorSet( v2, light->worldBounds[ 0 ][ 0 ], light->worldBounds[ 0 ][ 1 ], light->worldBounds[ 1 ][ 2 ] );
				R_AddEdgeToLightScissor( light, v1, v2 );

				VectorSet( v1, light->worldBounds[ 1 ][ 0 ], light->worldBounds[ 0 ][ 1 ], light->worldBounds[ 0 ][ 2 ] );
				VectorSet( v2, light->worldBounds[ 1 ][ 0 ], light->worldBounds[ 0 ][ 1 ], light->worldBounds[ 1 ][ 2 ] );
				R_AddEdgeToLightScissor( light, v1, v2 );
				break;
			}

		case refLightType_t::RL_PROJ:
			{
				int    j;
				vec3_t farCorners[ 4 ];
				plane_t frustum[ 6 ];
				for ( j = 0; j < 6; j++ )
				{
					VectorCopy( light->frustum[ j ].normal, frustum[ j ].normal );
					frustum[ j ].dist = light->frustum[ j ].dist;
				}

				R_CalcFrustumFarCorners( frustum, farCorners );

				if ( !VectorCompare( light->l.projStart, vec3_origin ) )
				{
					vec3_t nearCorners[ 4 ];

					// calculate the vertices defining the top area
					R_CalcFrustumNearCorners( frustum, nearCorners );

					for ( j = 0; j < 4; j++ )
					{
						// outer quad
						R_AddEdgeToLightScissor( light, nearCorners[ j ], farCorners[ j ] );
						R_AddEdgeToLightScissor( light, farCorners[ j ], farCorners[( j + 1 ) % 4 ] );
						R_AddEdgeToLightScissor( light, farCorners[( j + 1 ) % 4 ], nearCorners[( j + 1 ) % 4 ] );
						R_AddEdgeToLightScissor( light, nearCorners[( j + 1 ) % 4 ],  nearCorners[ j ] );

						// far cap
						R_AddEdgeToLightScissor( light, farCorners[ j ], farCorners[( j + 1 ) % 4 ] );

						// near cap
						R_AddEdgeToLightScissor( light, nearCorners[ j ], nearCorners[( j + 1 ) % 4 ] );
					}
				}
				else
				{
					vec3_t top;

					// no light_start, just use the top vertex (doesn't need to be mirrored)
					PlanesGetIntersectionPoint( frustum[ FRUSTUM_LEFT ], frustum[ FRUSTUM_RIGHT ], frustum[ FRUSTUM_TOP ], top );

					for ( j = 0; j < 4; j++ )
					{
						R_AddEdgeToLightScissor( light, farCorners[ j ], farCorners[( j + 1 ) % 4 ] );
						R_AddEdgeToLightScissor( light, top, farCorners[ j ] );
					}
				}

				break;
			}

		default:
			break;
	}

	light->scissor.coords[ 0 ] = Math::Clamp( light->scissor.coords[ 0 ], tr.viewParms.viewportX, tr.viewParms.viewportX + tr.viewParms.viewportWidth );
	light->scissor.coords[ 2 ] = Math::Clamp( light->scissor.coords[ 2 ], tr.viewParms.viewportX, tr.viewParms.viewportX + tr.viewParms.viewportWidth );

	light->scissor.coords[ 1 ] = Math::Clamp( light->scissor.coords[ 1 ], tr.viewParms.viewportY, tr.viewParms.viewportY + tr.viewParms.viewportHeight );
	light->scissor.coords[ 3 ] = Math::Clamp( light->scissor.coords[ 3 ], tr.viewParms.viewportY, tr.viewParms.viewportY + tr.viewParms.viewportHeight );
}

/*
=============
R_CalcLightCubeSideBits
=============
*/
// *INDENT-OFF*
byte R_CalcLightCubeSideBits( trRefLight_t *light, vec3_t worldBounds[ 2 ] )
{
	int        i;
	int        cubeSide;
	byte       cubeSideBits;
	float      xMin, xMax, yMin, yMax;
	float      zNear, zFar;
	float      fovX, fovY;
	vec3_t     angles;
	matrix_t   tmpMatrix, rotationMatrix, transformMatrix, viewMatrix, projectionMatrix, viewProjectionMatrix;
	frustum_t  frustum;
	cplane_t   *clipPlane;
	int        r;
	bool   anyClip;
	bool   culled;

	if ( light->l.rlType != refLightType_t::RL_OMNI || !glConfig2.shadowMapping || r_noShadowPyramids->integer )
	{
		return CUBESIDE_CLIPALL;
	}

	cubeSideBits = 0;

	for ( cubeSide = 0; cubeSide < 6; cubeSide++ )
	{
		switch ( cubeSide )
		{
			default: // 0
				{
					// view parameters
					VectorSet( angles, 0, 0, 0 );
					break;
				}

			case 1:
				{
					VectorSet( angles, 0, 180, 0 );
					break;
				}

			case 2:
				{
					VectorSet( angles, 0, 90, 0 );
					break;
				}

			case 3:
				{
					VectorSet( angles, 0, 270, 0 );
					break;
				}

			case 4:
				{
					VectorSet( angles, -90, 0, 0 );
					break;
				}

			case 5:
				{
					VectorSet( angles, 90, 0, 0 );
					break;
				}
		}

		// Quake -> OpenGL view matrix from light perspective
		MatrixFromAngles( rotationMatrix, angles[ PITCH ], angles[ YAW ], angles[ ROLL ] );
		MatrixSetupTransformFromRotation( transformMatrix, rotationMatrix, light->origin );
		MatrixAffineInverse( transformMatrix, tmpMatrix );

		// convert from our coordinate system (looking down X)
		// to OpenGL's coordinate system (looking down -Z)
		MatrixMultiply( quakeToOpenGLMatrix, tmpMatrix, viewMatrix );

		// OpenGL projection matrix
		fovX = 90;
		fovY = 90;

		zNear = 1.0f;
		zFar = light->sphereRadius;

		xMax = zNear * tanf( fovX * Math::divpi_360_f );
		xMin = -xMax;

		yMax = zNear * tanf( fovY * Math::divpi_360_f );
		yMin = -yMax;

		MatrixPerspectiveProjection( projectionMatrix, xMin, xMax, yMin, yMax, zNear, zFar );

		// calculate frustum planes using the modelview projection matrix
		MatrixMultiply( projectionMatrix, viewMatrix, viewProjectionMatrix );
		R_SetupFrustum2( frustum, viewProjectionMatrix );

		// use the frustum planes to cut off shadowmaps beyond the light volume
		anyClip = false;
		culled = false;

		for ( i = 0; i < 5; i++ )
		{
			clipPlane = &frustum[ i ];

			r = BoxOnPlaneSide( worldBounds[ 0 ], worldBounds[ 1 ], clipPlane );

			if ( r == 2 )
			{
				culled = true;
				break;
			}

			if ( r == 3 )
			{
				anyClip = true;
			}
		}

		if ( !culled )
		{
			if ( !anyClip )
			{
				// completely inside frustum
				tr.pc.c_pyramid_cull_ent_in++;
			}
			else
			{
				// partially clipped
				tr.pc.c_pyramid_cull_ent_clip++;
			}

			cubeSideBits |= ( 1 << cubeSide );
		}
		else
		{
			// completely outside frustum
			tr.pc.c_pyramid_cull_ent_out++;
		}
	}

	tr.pc.c_pyramidTests++;

	return cubeSideBits;
}

// *INDENT-ON*

/*
=================
R_SetupLightLOD
=================
*/
void R_SetupLightLOD( trRefLight_t *light )
{
	float radius;
	float flod, lodscale;
	float projectedRadius;
	int   lod;
	int   numLods;

	if ( light->l.noShadows )
	{
		light->shadowLOD = -1;
		return;
	}

	numLods = 5;

	// compute projected bounding sphere
	// and use that as a criteria for selecting LOD
	radius = light->sphereRadius;

	if ( ( projectedRadius = R_ProjectRadius( radius, light->l.origin ) ) != 0 )
	{
		lodscale = r_shadowLodScale->value;

		if ( lodscale > 20 )
		{
			lodscale = 20;
		}

		flod = 1.0f - projectedRadius * lodscale;
	}
	else
	{
		// object intersects near view plane, e.g. view weapon
		flod = 0;
	}

	flod *= numLods;
	lod = Q_ftol( flod );

	if ( lod < 0 )
	{
		lod = 0;
	}
	else if ( lod >= numLods )
	{
		//lod = numLods - 1;
	}

	lod += r_shadowLodBias->integer;

	if ( lod < 0 )
	{
		lod = 0;
	}

	if ( lod >= numLods )
	{
		// don't draw any shadow
		lod = -1;

		//lod = numLods - 1;
	}

	// never give ultra quality for point lights
	if ( lod == 0 && light->l.rlType == refLightType_t::RL_OMNI )
	{
		lod = 1;
	}

	light->shadowLOD = lod;
}

/*
=================
R_SetupLightShader
=================
*/
void R_SetupLightShader( trRefLight_t *light )
{
	if ( !light->l.attenuationShader )
	{
		switch ( light->l.rlType )
		{
			default:
			case refLightType_t::RL_OMNI:
				light->shader = tr.defaultDynamicLightShader;
				break;

			case refLightType_t::RL_PROJ:
				light->shader = tr.defaultProjectedLightShader;
				break;
		}
	}
	else
	{
		light->shader = R_GetShaderByHandle( light->l.attenuationShader );
	}
}

/*
===============
R_ComputeFinalAttenuation
===============
*/
void R_ComputeFinalAttenuation( shaderStage_t *pStage, trRefLight_t *light )
{
	matrix_t matrix;

	GLimp_LogComment( "--- R_ComputeFinalAttenuation ---\n" );

	RB_CalcTexMatrix( &pStage->bundle[ TB_COLORMAP ], matrix );

	MatrixMultiply( matrix, light->attenuationMatrix, light->attenuationMatrix2 );
}

/*
=================
R_CullLightTriangle

Returns CULL_IN, CULL_CLIP, or CULL_OUT
=================
*/
cullResult_t R_CullLightWorldBounds( trRefLight_t *light, vec3_t worldBounds[ 2 ] )
{
	int      i;
	cplane_t *frust;
	bool anyClip;
	int      r;

	if ( r_nocull->integer )
	{
		return cullResult_t::CULL_CLIP;
	}

	// check against frustum planes
	anyClip = false;

	for ( i = 0; i < 6; i++ )
	{
		frust = &light->frustum[ i ];

		r = BoxOnPlaneSide( worldBounds[ 0 ], worldBounds[ 1 ], frust );

		if ( r == 2 )
		{
			// completely outside frustum
			return cullResult_t::CULL_OUT;
		}

		if ( r == 3 )
		{
			anyClip = true;
		}
	}

	if ( !anyClip )
	{
		// completely inside frustum
		return cullResult_t::CULL_IN;
	}

	// partially clipped
	return cullResult_t::CULL_CLIP;
}
