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
// tr_main.c -- main control flow for each frame
#include "tr_local.h"
#include "Material.h"

trGlobals_t tr;

// convert from our coordinate system (looking down X)
// to OpenGL's coordinate system (looking down -Z)
const matrix_t quakeToOpenGLMatrix =
{
    0,  0, -1,  0,
   -1,  0,  0,  0,
    0,  1,  0,  0,
    0,  0,  0,  1
};

int            shadowMapResolutions[ 5 ] = { 2048, 1024, 512, 256, 128 };
int            sunShadowMapResolutions[ 5 ] = { 2048, 2048, 1024, 1024, 1024 };

refimport_t    ri;

// entities that will have procedurally generated surfaces will just
// point at this for their sorting surface
surfaceType_t entitySurface = surfaceType_t::SF_ENTITY;

/*
=============
R_CalcFaceNormal
=============
*/
void R_CalcFaceNormal( vec3_t normal,
		       const vec3_t v0, const vec3_t v1, const vec3_t v2 )
{
	vec3_t u, v;

	// compute the face normal based on vertex points
	VectorSubtract( v2, v0, u );
	VectorSubtract( v1, v0, v );
	CrossProduct( u, v, normal );

	VectorNormalize( normal );
}


/*
=============
R_CalcTangents
=============
*/
void R_CalcTangents( vec3_t tangent, vec3_t binormal,
		     const vec3_t v0, const vec3_t v1, const vec3_t v2,
		     const vec2_t t0, const vec2_t t1, const vec2_t t2 )
{
	vec3_t dpx, dpy;
	vec2_t dtx, dty;

	VectorSubtract(v1, v0, dpx);
	VectorSubtract(v2, v0, dpy);
	Vector2Subtract(t1, t0, dtx);
	Vector2Subtract(t2, t0, dty);

	float area = dtx[0] * dty[1] - dtx[1] * dty[0];
	if( area < 0.0f ) {
		dtx[0] = -dtx[0];
		dtx[1] = -dtx[1];
		dty[0] = -dty[0];
		dty[1] = -dty[1];
	}

	tangent[0] = dpx[0] * dty[1] - dtx[1] * dpy[0];
	tangent[1] = dpx[1] * dty[1] - dtx[1] * dpy[1];
	tangent[2] = dpx[2] * dty[1] - dtx[1] * dpy[2];

	binormal[0] = dtx[0] * dpy[0] - dpx[0] * dty[0];
	binormal[1] = dtx[0] * dpy[1] - dpx[1] * dty[0];
	binormal[2] = dtx[0] * dpy[2] - dpx[2] * dty[0];

	VectorNormalize( tangent );
	VectorNormalize( binormal );
}

void R_CalcTangents( vec3_t tangent, vec3_t binormal,
		     const vec3_t v0, const vec3_t v1, const vec3_t v2,
		     const f16vec2_t t0, const f16vec2_t t1, const f16vec2_t t2 )
{
	vec2_t t0f, t1f, t2f;

	t0f[ 0 ] = halfToFloat( t0[ 0 ] );
	t0f[ 1 ] = halfToFloat( t0[ 1 ] );
	t1f[ 0 ] = halfToFloat( t1[ 0 ] );
	t1f[ 1 ] = halfToFloat( t1[ 1 ] );
	t2f[ 0 ] = halfToFloat( t2[ 0 ] );
	t2f[ 1 ] = halfToFloat( t2[ 1 ] );

	vec3_t dpx, dpy;
	vec2_t dtx, dty;

	VectorSubtract(v1, v0, dpx);
	VectorSubtract(v2, v0, dpy);
	Vector2Subtract(t1f, t0f, dtx);
	Vector2Subtract(t2f, t0f, dty);

	float area = dtx[0] * dty[1] - dtx[1] * dty[0];
	if( area < 0.0f ) {
		dtx[0] = -dtx[0];
		dtx[1] = -dtx[1];
		dty[0] = -dty[0];
		dty[1] = -dty[1];
	}

	tangent[0] = dpx[0] * dty[1] - dtx[1] * dpy[0];
	tangent[1] = dpx[1] * dty[1] - dtx[1] * dpy[1];
	tangent[2] = dpx[2] * dty[1] - dtx[1] * dpy[2];

	binormal[0] = dtx[0] * dpy[0] - dpx[0] * dty[0];
	binormal[1] = dtx[0] * dpy[1] - dpx[1] * dty[0];
	binormal[2] = dtx[0] * dpy[2] - dpx[2] * dty[0];

	VectorNormalize( tangent );
	VectorNormalize( binormal );
}


void R_QtangentsToTBN( const i16vec4_t qtangent, vec3_t tangent,
		       vec3_t binormal, vec3_t normal )
{
	vec4_t quat;
	const vec3_t x = { 1.0f, 0.0f, 0.0f };
	const vec3_t y = { 0.0f, 1.0f, 0.0f };
	const vec3_t z = { 0.0f, 0.0f, 1.0f };

	snorm16ToFloat( qtangent, quat );
	QuatTransformVector( quat, x, tangent );
	QuatTransformVector( quat, y, binormal );
	QuatTransformVector( quat, z, normal );

	if( quat[ 3 ] < 0.0f ) {
		VectorNegate( tangent, tangent );
	}
}

void R_QtangentsToNormal( const i16vec4_t qtangent, vec3_t normal )
{
	vec4_t quat;
	const vec3_t z = { 0.0f, 0.0f, 1.0f };

	snorm16ToFloat( qtangent, quat );
	QuatTransformVector( quat, z, normal );
}

void R_TBNtoQtangents( const vec3_t tangent, const vec3_t binormal,
		       const vec3_t normal, i16vec4_t qtangent )
{
	vec3_t tangent2, binormal2, normal2;
	vec4_t q;
	bool flipped = false;
	float trace, scale, dot;
	vec3_t mid, tangent3, binormal3;

	// orthogonalize the input vectors
	// preserve normal vector as precise as possible
	if( VectorLengthSquared( normal ) < 0.001f ) {
		// degenerate case, compute new normal
		CrossProduct( tangent, binormal, normal2 );
		VectorNormalizeFast( normal2 );
	} else {
		VectorCopy( normal, normal2 );
	}

	// project tangent and binormal onto the normal orthogonal plane
	VectorMA(tangent, -DotProduct(normal2, tangent), normal2, tangent2 );
	VectorMA(binormal, -DotProduct(normal2, binormal), normal2, binormal2 );

	// check for several degenerate cases
	if( VectorLengthSquared( tangent2 ) < 0.001f ) {
		if( VectorLengthSquared( binormal2 ) < 0.001f ) {
			PerpendicularVector( tangent2, normal2 );
			CrossProduct( normal2, tangent2, binormal2 );
		} else {
			VectorNormalizeFast( binormal2 );
			CrossProduct( binormal2, normal2, tangent2 );
		}
	} else {
		VectorNormalizeFast( tangent2 );
		if( VectorLengthSquared( binormal2 ) < 0.001f ) {
			CrossProduct( normal2, tangent2, binormal2 );
		} else {
			// compute mid vector and project into mid-orthogonal plane
			VectorNormalizeFast( binormal2 );
			VectorAdd( tangent2, binormal2, mid );
			if( VectorLengthSquared( mid ) < 0.001f ) {
				CrossProduct( binormal2, normal2, mid );
			} else {
				VectorNormalizeFast( mid );
			}

			VectorMA(tangent2, -DotProduct(mid, tangent2), mid, tangent3 );
			VectorMA(binormal2, -DotProduct(mid, binormal2), mid, binormal3 );

			if( VectorLengthSquared( tangent3 ) < 0.001f ) {
				CrossProduct( mid, normal2, tangent3 );
				VectorNegate( tangent3, binormal3 );
			}

			VectorNormalizeFast( tangent3 );
			VectorNormalizeFast( binormal3 );

			VectorAdd( mid, tangent3, tangent2 );
			VectorAdd( mid, binormal3, binormal2 );

			VectorNormalizeFast( tangent2 );
			VectorNormalizeFast( binormal2 );
		}
	}

	// check of orientation
	CrossProduct( binormal2, normal2, tangent3 );
	dot = DotProduct( tangent2, tangent3 );
	if( dot < 0.0f ) {
		flipped = true;
		VectorNegate( tangent2, tangent2 );
	}

	if ( ( trace = tangent2[ 0 ] + binormal2[ 1 ] + normal2[ 2 ] ) > 0.0f )
	{
		trace += 1.0f;
		scale = 0.5f * Q_rsqrt_fast( trace );

		q[ 3 ] = trace * scale;
		q[ 2 ] = ( tangent2 [ 1 ] - binormal2[ 0 ] ) * scale;
		q[ 1 ] = ( normal2  [ 0 ] - tangent2 [ 2 ] ) * scale;
		q[ 0 ] = ( binormal2[ 2 ] - normal2  [ 1 ] ) * scale;
	}

	else if ( tangent2[ 0 ] > binormal2[ 1 ] && tangent2[ 0 ] > normal2[ 2 ] )
	{
		trace = tangent2[ 0 ] - binormal2[ 1 ] - normal2[ 2 ] + 1.0f;
		scale = 0.5f * Q_rsqrt_fast( trace );

		q[ 0 ] = trace * scale;
		q[ 1 ] = ( tangent2 [ 1 ] + binormal2[ 0 ] ) * scale;
		q[ 2 ] = ( normal2  [ 0 ] + tangent2 [ 2 ] ) * scale;
		q[ 3 ] = ( binormal2[ 2 ] - normal2  [ 1 ] ) * scale;
	}

	else if ( binormal2[ 1 ] > normal2[ 2 ] )
	{
		trace = -tangent2[ 0 ] + binormal2[ 1 ] - normal2[ 2 ] + 1.0f;
		scale = 0.5f * Q_rsqrt_fast( trace );

		q[ 1 ] = trace * scale;
		q[ 0 ] = ( tangent2 [ 1 ] + binormal2[ 0 ] ) * scale;
		q[ 3 ] = ( normal2  [ 0 ] - tangent2 [ 2 ] ) * scale;
		q[ 2 ] = ( binormal2[ 2 ] + normal2  [ 1 ] ) * scale;
	}

	else
	{
		trace = -tangent2[ 0 ] - binormal2[ 1 ] + normal2[ 2 ] + 1.0f;
		scale = 0.5f * Q_rsqrt_fast( trace );

		q[ 2 ] = trace * scale;
		q[ 3 ] = ( tangent2 [ 1 ] - binormal2[ 0 ] ) * scale;
		q[ 0 ] = ( normal2  [ 0 ] + tangent2 [ 2 ] ) * scale;
		q[ 1 ] = ( binormal2[ 2 ] + normal2  [ 1 ] ) * scale;
	}

	if( q[ 3 ] < 0.0f ) {
		q[ 0 ] = -q[ 0 ];
		q[ 1 ] = -q[ 1 ];
		q[ 2 ] = -q[ 2 ];
		q[ 3 ] = -q[ 3 ];
	}

	i16vec4_t resqtangent;
	floatToSnorm16( q, resqtangent );

	if( resqtangent[ 3 ] == 0 )
	{
		resqtangent[ 3 ] = 1;
	}

	if( flipped )
	{
		qtangent[ 0 ] = -resqtangent[ 0 ];
		qtangent[ 1 ] = -resqtangent[ 1 ];
		qtangent[ 2 ] = -resqtangent[ 2 ];
		qtangent[ 3 ] = -resqtangent[ 3 ];
	}
	else
	{
		qtangent[ 0 ] = resqtangent[ 0 ];
		qtangent[ 1 ] = resqtangent[ 1 ];
		qtangent[ 2 ] = resqtangent[ 2 ];
		qtangent[ 3 ] = resqtangent[ 3 ];
	}

#if 0
	vec3_t T, B, N;
	R_QtangentsToTBN( qtangent, T, B, N );
	ASSERT_LT(Distance(N, normal2), 0.01f);
	if( flipped ) {
		VectorNegate( T, T );
	}
	ASSERT_LT(Distance(T, tangent2), 0.01f);
	ASSERT_LT(Distance(B, binormal2), 0.01f);
#endif
}

/*
=================
R_CullBox

Returns CULL_IN, CULL_CLIP, or CULL_OUT
=================
*/
cullResult_t R_CullBox( vec3_t worldBounds[ 2 ] )
{
	bool anyClip;
	cplane_t *frust;
	int i, r;

	if ( r_nocull->integer )
	{
		return cullResult_t::CULL_CLIP;
	}

	// check against frustum planes
	anyClip = false;

	for ( i = 0; i < FRUSTUM_PLANES; i++ )
	{
		frust = &tr.viewParms.frustums[ 0 ][ i ];

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

/*
=================
R_CullLocalBox

Returns CULL_IN, CULL_CLIP, or CULL_OUT
=================
*/
cullResult_t R_CullLocalBox( vec3_t localBounds[ 2 ] )
{
	vec3_t   worldBounds[ 2 ];

	// transform into world space
	MatrixTransformBounds(tr.orientation.transformMatrix, localBounds[0], localBounds[1], worldBounds[0], worldBounds[1]);

	return R_CullBox( worldBounds );
}

/*
=================
R_CullLocalPointAndRadius
=================
*/
cullResult_t R_CullLocalPointAndRadius( vec3_t pt, float radius )
{
	vec3_t transformed;

	R_LocalPointToWorld( pt, transformed );

	return R_CullPointAndRadius( transformed, radius );
}

/*
=================
R_CullPointAndRadius
=================
*/
cullResult_t R_CullPointAndRadius( vec3_t pt, float radius )
{
	int      i;
	float    dist;
	cplane_t *frust;
	bool mightBeClipped = false;

	if ( r_nocull->integer )
	{
		return cullResult_t::CULL_CLIP;
	}

	// check against frustum planes
	for ( i = 0; i < Util::ordinal(frustumBits_t::FRUSTUM_PLANES); i++ )
	{
		frust = &tr.viewParms.frustums[ 0 ][ i ];

		dist = DotProduct( pt, frust->normal ) - frust->dist;

		if ( dist < -radius )
		{
			return cullResult_t::CULL_OUT;
		}
		else if ( dist <= radius )
		{
			mightBeClipped = true;
		}
	}

	if ( mightBeClipped )
	{
		return cullResult_t::CULL_CLIP;
	}

	return cullResult_t::CULL_IN; // completely inside frustum
}

/*
=================
R_FogWorldBox
=================
*/
int R_FogWorldBox( vec3_t bounds[ 2 ] )
{
	int   i, j;
	fog_t *fog;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL )
	{
		return 0;
	}

	for ( i = 1; i < tr.world->numFogs; i++ )
	{
		fog = &tr.world->fogs[ i ];

		for ( j = 0; j < 3; j++ )
		{
			if ( bounds[ 0 ][ j ] >= fog->bounds[ 1 ][ j ] )
			{
				break;
			}

			if ( bounds[ 1 ][ j ] <= fog->bounds[ 0 ][ j ] )
			{
				break;
			}
		}

		if ( j == 3 )
		{
			return i;
		}
	}

	return 0;
}

/*
=================
R_LocalNormalToWorld
=================
*/
void R_LocalNormalToWorld( const vec3_t local, vec3_t world )
{
	MatrixTransformNormal( tr.orientation.transformMatrix, local, world );
}

/*
=================
R_LocalPointToWorld
=================
*/
void R_LocalPointToWorld( const vec3_t local, vec3_t world )
{
	MatrixTransformPoint( tr.orientation.transformMatrix, local, world );
}

/*
==========================
R_TransformWorldToClip
==========================
*/
void R_TransformWorldToClip( const vec3_t src, const float *cameraViewMatrix, const float *projectionMatrix, vec4_t eye,
                             vec4_t dst )
{
	vec4_t src2;

	VectorCopy( src, src2 );
	src2[ 3 ] = 1;

	MatrixTransform4( cameraViewMatrix, src2, eye );
	MatrixTransform4( projectionMatrix, eye, dst );
}

/*
==========================
R_TransformModelToClip
==========================
*/
void R_TransformModelToClip( const vec3_t src, const float *modelViewMatrix, const float *projectionMatrix, vec4_t eye, vec4_t dst )
{
	vec4_t src2;

	VectorCopy( src, src2 );
	src2[ 3 ] = 1;

	MatrixTransform4( modelViewMatrix, src2, eye );
	MatrixTransform4( projectionMatrix, eye, dst );
}

/*
==========================
R_TransformClipToWindow
==========================
*/
void R_TransformClipToWindow( const vec4_t clip, const viewParms_t *view, vec4_t normalized, vec4_t window )
{
	normalized[ 0 ] = clip[ 0 ] / clip[ 3 ];
	normalized[ 1 ] = clip[ 1 ] / clip[ 3 ];
	normalized[ 2 ] = ( clip[ 2 ] + clip[ 3 ] ) / ( 2 * clip[ 3 ] );

	window[ 0 ] = view->viewportX + ( 0.5f * ( 1.0f + normalized[ 0 ] ) * view->viewportWidth );
	window[ 1 ] = view->viewportY + ( 0.5f * ( 1.0f + normalized[ 1 ] ) * view->viewportHeight );
	window[ 2 ] = normalized[ 2 ];

	window[ 0 ] = ( int )( window[ 0 ] + 0.5 );
	window[ 1 ] = ( int )( window[ 1 ] + 0.5 );
}

/*
================
R_ProjectRadius
================
*/
float R_ProjectRadius( float r, vec3_t location )
{
	float  pr;
	float  dist;
	float  c;
	vec3_t p;
	float  projected[ 4 ];

	c = DotProduct( tr.viewParms.orientation.axis[ 0 ], tr.viewParms.orientation.origin );
	dist = DotProduct( tr.viewParms.orientation.axis[ 0 ], location ) - c;

	if ( dist <= 0 )
	{
		return 0;
	}

	p[ 0 ] = 0;
	p[ 1 ] = fabsf( r );
	p[ 2 ] = -dist;

	projected[ 0 ] = p[ 0 ] * tr.viewParms.projectionMatrix[ 0 ] +
	                 p[ 1 ] * tr.viewParms.projectionMatrix[ 4 ] + p[ 2 ] * tr.viewParms.projectionMatrix[ 8 ] + tr.viewParms.projectionMatrix[ 12 ];

	projected[ 1 ] = p[ 0 ] * tr.viewParms.projectionMatrix[ 1 ] +
	                 p[ 1 ] * tr.viewParms.projectionMatrix[ 5 ] + p[ 2 ] * tr.viewParms.projectionMatrix[ 9 ] + tr.viewParms.projectionMatrix[ 13 ];

	projected[ 2 ] = p[ 0 ] * tr.viewParms.projectionMatrix[ 2 ] +
	                 p[ 1 ] * tr.viewParms.projectionMatrix[ 6 ] + p[ 2 ] * tr.viewParms.projectionMatrix[ 10 ] + tr.viewParms.projectionMatrix[ 14 ];

	projected[ 3 ] = p[ 0 ] * tr.viewParms.projectionMatrix[ 3 ] +
	                 p[ 1 ] * tr.viewParms.projectionMatrix[ 7 ] + p[ 2 ] * tr.viewParms.projectionMatrix[ 11 ] + tr.viewParms.projectionMatrix[ 15 ];

	pr = projected[ 1 ] / projected[ 3 ];

	if ( pr > 1.0f )
	{
		pr = 1.0f;
	}

	return pr;
}

/*
=================
R_SetupEntityWorldBounds
Tr3B - needs R_RotateEntityForViewParms
=================
*/
void R_SetupEntityWorldBounds( trRefEntity_t *ent )
{
	MatrixTransformBounds(tr.orientation.transformMatrix, ent->localBounds[0], ent->localBounds[1], ent->worldBounds[0], ent->worldBounds[1]);
}

/*
=================
R_RotateEntityForViewParms

Generates an orientation for an entity and viewParms
Does NOT produce any GL calls
Called by both the front end and the back end
=================
*/
void R_RotateEntityForViewParms( const trRefEntity_t *ent, const viewParms_t *viewParms, orientationr_t * orientation )
{
	vec3_t delta;
	float  axisLength;

	if ( ent->e.reType != refEntityType_t::RT_MODEL )
	{
		* orientation = viewParms->world;
		return;
	}

	VectorCopy( ent->e.origin, orientation ->origin );

	VectorCopy( ent->e.axis[ 0 ], orientation ->axis[ 0 ] );
	VectorCopy( ent->e.axis[ 1 ], orientation ->axis[ 1 ] );
	VectorCopy( ent->e.axis[ 2 ], orientation ->axis[ 2 ] );

	MatrixSetupTransformFromVectorsFLU( orientation ->transformMatrix, orientation ->axis[ 0 ], orientation ->axis[ 1 ], orientation ->axis[ 2 ], orientation ->origin );
	MatrixAffineInverse( orientation ->transformMatrix, orientation ->viewMatrix );
	MatrixMultiply( viewParms->world.viewMatrix, orientation ->transformMatrix, orientation ->modelViewMatrix );

	// calculate the viewer origin in the model's space
	// needed for fog, specular, and environment mapping
	VectorSubtract( viewParms->orientation.origin, orientation ->origin, delta );

	// compensate for scale in the axes if necessary
	if ( ent->e.nonNormalizedAxes )
	{
		axisLength = VectorLength( ent->e.axis[ 0 ] );

		if ( !axisLength )
		{
			axisLength = 0;
		}
		else
		{
			axisLength = 1.0f / axisLength;
		}
	}
	else
	{
		axisLength = 1.0f;
	}

	orientation ->viewOrigin[ 0 ] = DotProduct( delta, orientation ->axis[ 0 ] ) * axisLength;
	orientation ->viewOrigin[ 1 ] = DotProduct( delta, orientation ->axis[ 1 ] ) * axisLength;
	orientation ->viewOrigin[ 2 ] = DotProduct( delta, orientation ->axis[ 2 ] ) * axisLength;
}

/*
=================
R_RotateEntityForLight

Generates an orientation for an entity and light
Does NOT produce any GL calls
Called by both the front end and the back end
=================
*/
void R_RotateEntityForLight( const trRefEntity_t *ent, const trRefLight_t *light, orientationr_t * orientation )
{
	vec3_t delta;
	float  axisLength;

	if ( ent->e.reType != refEntityType_t::RT_MODEL )
	{
		*orientation = {};

		orientation ->axis[ 0 ][ 0 ] = 1;
		orientation ->axis[ 1 ][ 1 ] = 1;
		orientation ->axis[ 2 ][ 2 ] = 1;

		VectorCopy( light->l.origin, orientation ->viewOrigin );

		MatrixIdentity( orientation ->transformMatrix );
		MatrixMultiply( light->viewMatrix, orientation ->transformMatrix, orientation ->viewMatrix );
		MatrixCopy( orientation ->viewMatrix, orientation ->modelViewMatrix );
		return;
	}

	VectorCopy( ent->e.origin, orientation ->origin );

	VectorCopy( ent->e.axis[ 0 ], orientation ->axis[ 0 ] );
	VectorCopy( ent->e.axis[ 1 ], orientation ->axis[ 1 ] );
	VectorCopy( ent->e.axis[ 2 ], orientation ->axis[ 2 ] );

	MatrixSetupTransformFromVectorsFLU( orientation ->transformMatrix, orientation ->axis[ 0 ], orientation ->axis[ 1 ], orientation ->axis[ 2 ], orientation ->origin );
	MatrixAffineInverse( orientation ->transformMatrix, orientation ->viewMatrix );

	MatrixMultiply( light->viewMatrix, orientation ->transformMatrix, orientation ->modelViewMatrix );

	// calculate the viewer origin in the model's space
	// needed for fog, specular, and environment mapping
	VectorSubtract( light->l.origin, orientation ->origin, delta );

	// compensate for scale in the axes if necessary
	if ( ent->e.nonNormalizedAxes )
	{
		axisLength = VectorLength( ent->e.axis[ 0 ] );

		if ( !axisLength )
		{
			axisLength = 0;
		}
		else
		{
			axisLength = 1.0f / axisLength;
		}
	}
	else
	{
		axisLength = 1.0f;
	}

	orientation ->viewOrigin[ 0 ] = DotProduct( delta, orientation ->axis[ 0 ] ) * axisLength;
	orientation ->viewOrigin[ 1 ] = DotProduct( delta, orientation ->axis[ 1 ] ) * axisLength;
	orientation ->viewOrigin[ 2 ] = DotProduct( delta, orientation ->axis[ 2 ] ) * axisLength;
}

/*
=================
R_RotateLightForViewParms
=================
*/
void R_RotateLightForViewParms( const trRefLight_t *light, const viewParms_t *viewParms, orientationr_t * orientation )
{
	vec3_t delta;

	VectorCopy( light->l.origin, orientation ->origin );

	QuatToAxis( light->l.rotation, orientation ->axis );

	MatrixSetupTransformFromVectorsFLU( orientation ->transformMatrix, orientation ->axis[ 0 ], orientation ->axis[ 1 ], orientation ->axis[ 2 ], orientation ->origin );
	MatrixAffineInverse( orientation ->transformMatrix, orientation ->viewMatrix );
	MatrixMultiply( viewParms->world.viewMatrix, orientation ->transformMatrix, orientation ->modelViewMatrix );

	// calculate the viewer origin in the light's space
	// needed for fog, specular, and environment mapping
	VectorSubtract( viewParms->orientation.origin, orientation ->origin, delta );

	orientation ->viewOrigin[ 0 ] = DotProduct( delta, orientation ->axis[ 0 ] );
	orientation ->viewOrigin[ 1 ] = DotProduct( delta, orientation ->axis[ 1 ] );
	orientation ->viewOrigin[ 2 ] = DotProduct( delta, orientation ->axis[ 2 ] );
}

/*
=================
R_RotateForViewer

Sets up the modelview matrix for a given viewParm
=================
*/
void R_RotateForViewer()
{
	matrix_t transformMatrix;

	tr.orientation = {};
	tr.orientation.axis[ 0 ][ 0 ] = 1;
	tr.orientation.axis[ 1 ][ 1 ] = 1;
	tr.orientation.axis[ 2 ][ 2 ] = 1;
	VectorCopy( tr.viewParms.orientation.origin, tr.orientation.viewOrigin );

	MatrixIdentity( tr.orientation.transformMatrix );

	// transform by the camera placement
	MatrixSetupTransformFromVectorsFLU( transformMatrix,
	                                    tr.viewParms.orientation.axis[ 0 ], tr.viewParms.orientation.axis[ 1 ], tr.viewParms.orientation.axis[ 2 ], tr.viewParms.orientation.origin );

	MatrixAffineInverse( transformMatrix, tr.orientation.viewMatrix2 );

	// convert from our right handed coordinate system (looking down X)
	// to OpenGL's right handed coordinate system (looking down -Z)
	MatrixMultiply( quakeToOpenGLMatrix, tr.orientation.viewMatrix2, tr.orientation.viewMatrix );

	MatrixCopy( tr.orientation.viewMatrix, tr.orientation.modelViewMatrix );

	tr.viewParms.world = tr.orientation;
}

/*
** SetFarClip
*/
static void SetFarClip()
{
	float farthestCornerDistance;
	int   i;
	// if not rendering the world (icons, menus, etc)
	// set a 2k far clip plane
	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL )
	{
		tr.viewParms.zFar = 2048;
		return;
	}

	//
	// set far clipping planes dynamically
	//
	farthestCornerDistance = 0;

	// check visBounds
	for ( i = 0; i < 8; i++ )
	{
		vec3_t v;
		float  distance;

		if ( glConfig2.usingMaterialSystem ) {
			VectorCopy( materialSystem.worldViewBounds[0], tr.viewParms.visBounds[0] );
			VectorCopy( materialSystem.worldViewBounds[1], tr.viewParms.visBounds[1] );
		}

		if ( i & 1 )
		{
			v[ 0 ] = tr.viewParms.visBounds[ 0 ][ 0 ];
		}
		else
		{
			v[ 0 ] = tr.viewParms.visBounds[ 1 ][ 0 ];
		}

		if ( i & 2 )
		{
			v[ 1 ] = tr.viewParms.visBounds[ 0 ][ 1 ];
		}
		else
		{
			v[ 1 ] = tr.viewParms.visBounds[ 1 ][ 1 ];
		}

		if ( i & 4 )
		{
			v[ 2 ] = tr.viewParms.visBounds[ 0 ][ 2 ];
		}
		else
		{
			v[ 2 ] = tr.viewParms.visBounds[ 1 ][ 2 ];
		}

		distance = DistanceSquared( v, tr.viewParms.orientation.origin );

		if ( distance > farthestCornerDistance )
		{
			farthestCornerDistance = distance;
		}
	}

	tr.viewParms.zFar = sqrt( farthestCornerDistance );
}

/*
===============
R_SetupProjection
===============
*/
// *INDENT-OFF*
static void R_SetupProjection( bool infiniteFarClip )
{
	float zNear, zFar;
	float *proj = tr.viewParms.projectionMatrix;

	// dynamically compute far clip plane distance
	SetFarClip();

	// portal views are constrained to their surface plane
	if ( tr.viewParms.portalLevel == 0 )
	{
		tr.viewParms.zNear = r_znear->value;
	}

	zNear = tr.viewParms.zNear;

	if ( r_zfar->value )
	{
		zFar = tr.viewParms.zFar = std::max( tr.viewParms.zFar, r_zfar->value );
	}
	else if ( infiniteFarClip )
	{
		zFar = tr.viewParms.zFar = 0;
	}
	else
	{
		zFar = tr.viewParms.zFar;
	}

	if ( zFar <= 0 || infiniteFarClip ) // || r_showBspNodes->integer)
	{
		MatrixPerspectiveProjectionFovXYInfiniteRH( proj, tr.refdef.fov_x, tr.refdef.fov_y, zNear );
	}
	else
	{
		MatrixPerspectiveProjectionFovXYRH( proj, tr.refdef.fov_x, tr.refdef.fov_y, zNear, zFar );
	}

	if ( tr.viewParms.portalLevel == 0 ) {
		MatrixCopy( tr.viewParms.projectionMatrix, tr.viewParms.projectionMatrixNonPortal );
	}
}

// *INDENT-ON*

/*
=================
R_SetupUnprojection
create a matrix with similar functionality like gluUnproject, project from window space to world space
=================
*/
static void R_SetupUnprojection()
{
	float *unprojectMatrix = tr.viewParms.unprojectionMatrix;

	MatrixCopy( tr.viewParms.projectionMatrix, unprojectMatrix );
	MatrixMultiply2( unprojectMatrix, quakeToOpenGLMatrix );
	MatrixMultiply2( unprojectMatrix, tr.viewParms.world.viewMatrix2 );
	MatrixInverse( unprojectMatrix );

	MatrixMultiplyTranslation( unprojectMatrix, -1.0, -1.0, -1.0 );
	MatrixMultiplyScale( unprojectMatrix, 2.0f / glConfig.vidWidth, 2.0f / glConfig.vidHeight, 2.0 );
}

/*
=================
R_SetupFrustum

Setup that culling frustum planes for the current view
=================
*/
static void R_SetupFrustum()
{
	float  xs, xc;
	float  ang;
	vec3_t planeOrigin;

	if ( tr.viewParms.portalLevel > 0 )
	{
		// this is a portal, so constrain the culling frustum to the portal surface
		matrix_t invTransform;
		MatrixAffineInverse(tr.viewParms.world.viewMatrix, invTransform);

		//transform planes back to world space for culling
		for (int i = 0; i <= FRUSTUM_NEAR; i++)
		{
			plane_t plane;
			VectorCopy(tr.viewParms.portalFrustum[i].normal, plane.normal);
			plane.dist = tr.viewParms.portalFrustum[i].dist;

			MatrixTransformPlane2(invTransform, plane);

			VectorCopy(plane.normal, tr.viewParms.frustums[0][i].normal);
			tr.viewParms.frustums[0][i].dist = plane.dist;

			SetPlaneSignbits(&tr.viewParms.frustums[0][i]);
			tr.viewParms.frustums[0][i].type = PLANE_NON_AXIAL;
		}
	}
	else
	{
		ang = DEG2RAD( tr.viewParms.fovX * 0.5f );
		xs = sinf( ang );
		xc = cosf( ang );

		VectorScale( tr.viewParms.orientation.axis[ 0 ], xs, tr.viewParms.frustums[ 0 ][ 0 ].normal );
		VectorMA( tr.viewParms.frustums[ 0 ][ 0 ].normal, xc, tr.viewParms.orientation.axis[ 1 ], tr.viewParms.frustums[ 0 ][ 0 ].normal );

		VectorScale( tr.viewParms.orientation.axis[ 0 ], xs, tr.viewParms.frustums[ 0 ][ 1 ].normal );
		VectorMA( tr.viewParms.frustums[ 0 ][ 1 ].normal, -xc, tr.viewParms.orientation.axis[ 1 ], tr.viewParms.frustums[ 0 ][ 1 ].normal );

		ang = DEG2RAD( tr.viewParms.fovY * 0.5f );
		xs = sinf( ang );
		xc = cosf( ang );

		VectorScale( tr.viewParms.orientation.axis[ 0 ], xs, tr.viewParms.frustums[ 0 ][ 2 ].normal );
		VectorMA( tr.viewParms.frustums[ 0 ][ 2 ].normal, xc, tr.viewParms.orientation.axis[ 2 ], tr.viewParms.frustums[ 0 ][ 2 ].normal );

		VectorScale( tr.viewParms.orientation.axis[ 0 ], xs, tr.viewParms.frustums[ 0 ][ 3 ].normal );
		VectorMA( tr.viewParms.frustums[ 0 ][ 3 ].normal, -xc, tr.viewParms.orientation.axis[ 2 ], tr.viewParms.frustums[ 0 ][ 3 ].normal );

		for ( int i = 0; i < 4; i++ )
		{
			tr.viewParms.frustums[ 0 ][ i ].type = PLANE_NON_AXIAL;
			tr.viewParms.frustums[ 0 ][ i ].dist = DotProduct( tr.viewParms.orientation.origin, tr.viewParms.frustums[ 0 ][ i ].normal );
			SetPlaneSignbits( &tr.viewParms.frustums[ 0 ][ i ] );
		}

		// Tr3B: set extra near plane which is required by the dynamic occlusion culling
		tr.viewParms.frustums[ 0 ][ FRUSTUM_NEAR ].type = PLANE_NON_AXIAL;
		VectorCopy( tr.viewParms.orientation.axis[ 0 ], tr.viewParms.frustums[ 0 ][ FRUSTUM_NEAR ].normal );

		VectorMA( tr.viewParms.orientation.origin, r_znear->value, tr.viewParms.frustums[ 0 ][ FRUSTUM_NEAR ].normal, planeOrigin );
		tr.viewParms.frustums[ 0 ][ FRUSTUM_NEAR ].dist = DotProduct( planeOrigin, tr.viewParms.frustums[ 0 ][ FRUSTUM_NEAR ].normal );
		SetPlaneSignbits( &tr.viewParms.frustums[ 0 ][ FRUSTUM_NEAR ] );
	}
}

/*
=================
R_SetupFrustum

Setup that culling frustum planes for the current view
=================
*/
// *INDENT-OFF*
void R_SetupFrustum2( frustum_t frustum, const matrix_t mvp )
{
	// http://www2.ravensoft.com/users/ggribb/plane%20extraction.pdf
	int i;

	// left
	frustum[ FRUSTUM_LEFT ].normal[ 0 ] = mvp[ 3 ] + mvp[ 0 ];
	frustum[ FRUSTUM_LEFT ].normal[ 1 ] = mvp[ 7 ] + mvp[ 4 ];
	frustum[ FRUSTUM_LEFT ].normal[ 2 ] = mvp[ 11 ] + mvp[ 8 ];
	frustum[ FRUSTUM_LEFT ].dist = - ( mvp[ 15 ] + mvp[ 12 ] );

	// right
	frustum[ FRUSTUM_RIGHT ].normal[ 0 ] = mvp[ 3 ] - mvp[ 0 ];
	frustum[ FRUSTUM_RIGHT ].normal[ 1 ] = mvp[ 7 ] - mvp[ 4 ];
	frustum[ FRUSTUM_RIGHT ].normal[ 2 ] = mvp[ 11 ] - mvp[ 8 ];
	frustum[ FRUSTUM_RIGHT ].dist = - ( mvp[ 15 ] - mvp[ 12 ] );

	// bottom
	frustum[ FRUSTUM_BOTTOM ].normal[ 0 ] = mvp[ 3 ] + mvp[ 1 ];
	frustum[ FRUSTUM_BOTTOM ].normal[ 1 ] = mvp[ 7 ] + mvp[ 5 ];
	frustum[ FRUSTUM_BOTTOM ].normal[ 2 ] = mvp[ 11 ] + mvp[ 9 ];
	frustum[ FRUSTUM_BOTTOM ].dist = - ( mvp[ 15 ] + mvp[ 13 ] );

	// top
	frustum[ FRUSTUM_TOP ].normal[ 0 ] = mvp[ 3 ] - mvp[ 1 ];
	frustum[ FRUSTUM_TOP ].normal[ 1 ] = mvp[ 7 ] - mvp[ 5 ];
	frustum[ FRUSTUM_TOP ].normal[ 2 ] = mvp[ 11 ] - mvp[ 9 ];
	frustum[ FRUSTUM_TOP ].dist = - ( mvp[ 15 ] - mvp[ 13 ] );

	// near
	frustum[ FRUSTUM_NEAR ].normal[ 0 ] = mvp[ 3 ] + mvp[ 2 ];
	frustum[ FRUSTUM_NEAR ].normal[ 1 ] = mvp[ 7 ] + mvp[ 6 ];
	frustum[ FRUSTUM_NEAR ].normal[ 2 ] = mvp[ 11 ] + mvp[ 10 ];
	frustum[ FRUSTUM_NEAR ].dist = - ( mvp[ 15 ] + mvp[ 14 ] );

	// far
	frustum[ FRUSTUM_FAR ].normal[ 0 ] = mvp[ 3 ] - mvp[ 2 ];
	frustum[ FRUSTUM_FAR ].normal[ 1 ] = mvp[ 7 ] - mvp[ 6 ];
	frustum[ FRUSTUM_FAR ].normal[ 2 ] = mvp[ 11 ] - mvp[ 10 ];
	frustum[ FRUSTUM_FAR ].dist = - ( mvp[ 15 ] - mvp[ 14 ] );

	for ( i = 0; i < 6; i++ )
	{
		vec_t length, ilength;

		frustum[ i ].type = PLANE_NON_AXIAL;

		// normalize
		length = VectorLength( frustum[ i ].normal );

		if ( length )
		{
			ilength = 1.0f / length;
			frustum[ i ].normal[ 0 ] *= ilength;
			frustum[ i ].normal[ 1 ] *= ilength;
			frustum[ i ].normal[ 2 ] *= ilength;
			frustum[ i ].dist *= ilength;
		}

		SetPlaneSignbits( &frustum[ i ] );
	}
}

// *INDENT-ON*

void R_CalcFrustumNearCornersUnsafe( const plane_t frustum[ FRUSTUM_NEAR + 1 ], vec3_t (&corners)[ 4 ] )
{
	PlanesGetIntersectionPoint( frustum[ FRUSTUM_LEFT ], frustum[ FRUSTUM_TOP ], frustum[ FRUSTUM_NEAR ], corners[ 0 ] );
	PlanesGetIntersectionPoint( frustum[ FRUSTUM_RIGHT ], frustum[ FRUSTUM_TOP ], frustum[ FRUSTUM_NEAR ], corners[ 1 ] );
	PlanesGetIntersectionPoint( frustum[ FRUSTUM_RIGHT ], frustum[ FRUSTUM_BOTTOM ], frustum[ FRUSTUM_NEAR ], corners[ 2 ] );
	PlanesGetIntersectionPoint( frustum[ FRUSTUM_LEFT ], frustum[ FRUSTUM_BOTTOM ], frustum[ FRUSTUM_NEAR ], corners[ 3 ] );
}

void R_CalcFrustumFarCornersUnsafe( const plane_t frustum[ FRUSTUM_FAR + 1 ], vec3_t (&corners)[ 4 ] )
{
	PlanesGetIntersectionPoint( frustum[ FRUSTUM_LEFT ], frustum[ FRUSTUM_TOP ], frustum[ FRUSTUM_FAR ], corners[ 0 ] );
	PlanesGetIntersectionPoint( frustum[ FRUSTUM_RIGHT ], frustum[ FRUSTUM_TOP ], frustum[ FRUSTUM_FAR ], corners[ 1 ] );
	PlanesGetIntersectionPoint( frustum[ FRUSTUM_RIGHT ], frustum[ FRUSTUM_BOTTOM ], frustum[ FRUSTUM_FAR ], corners[ 2 ] );
	PlanesGetIntersectionPoint( frustum[ FRUSTUM_LEFT ], frustum[ FRUSTUM_BOTTOM ], frustum[ FRUSTUM_FAR ], corners[ 3 ] );
}

static void CopyPlane( const cplane_t *in, cplane_t *out )
{
	VectorCopy( in->normal, out->normal );
	out->dist = in->dist;
	out->type = in->type;
	out->signbits = in->signbits;
	out->pad[ 0 ] = in->pad[ 0 ];
	out->pad[ 1 ] = in->pad[ 1 ];
}

static void R_SetupSplitFrustums()
{
	int    i, j;
	float  lambda;
	float  ratio;
	vec3_t planeOrigin;
	float  zNear, zFar;

	lambda = r_parallelShadowSplitWeight->value;
	ratio = tr.viewParms.zFar / tr.viewParms.zNear;

	for ( j = 0; j < 5; j++ )
	{
		CopyPlane( &tr.viewParms.frustums[ 0 ][ j ], &tr.viewParms.frustums[ 1 ][ j ] );
	}

	for ( i = 1; i <= ( r_parallelShadowSplits->integer + 1 ); i++ )
	{
		float si = i / ( float )( r_parallelShadowSplits->integer + 1 );

		zFar = 1.005f * lambda * ( tr.viewParms.zNear * powf( ratio, si ) ) + ( 1 - lambda ) * ( tr.viewParms.zNear + ( tr.viewParms.zFar - tr.viewParms.zNear ) * si );

		if ( i <= r_parallelShadowSplits->integer )
		{
			tr.viewParms.parallelSplitDistances[ i - 1 ] = zFar;
		}

		tr.viewParms.frustums[ i ][ FRUSTUM_FAR ].type = PLANE_NON_AXIAL;
		VectorNegate( tr.viewParms.orientation.axis[ 0 ], tr.viewParms.frustums[ i ][ FRUSTUM_FAR ].normal );

		VectorMA( tr.viewParms.orientation.origin, zFar, tr.viewParms.orientation.axis[ 0 ], planeOrigin );
		tr.viewParms.frustums[ i ][ FRUSTUM_FAR ].dist = DotProduct( planeOrigin, tr.viewParms.frustums[ i ][ FRUSTUM_FAR ].normal );
		SetPlaneSignbits( &tr.viewParms.frustums[ i ][ FRUSTUM_FAR ] );

		if ( i <= ( r_parallelShadowSplits->integer ) )
		{
			zNear = zFar - ( zFar * 0.005f );
			tr.viewParms.frustums[ i + 1 ][ FRUSTUM_NEAR ].type = PLANE_NON_AXIAL;
			VectorCopy( tr.viewParms.orientation.axis[ 0 ], tr.viewParms.frustums[ i + 1 ][ FRUSTUM_NEAR ].normal );

			VectorMA( tr.viewParms.orientation.origin, zNear, tr.viewParms.orientation.axis[ 0 ], planeOrigin );
			tr.viewParms.frustums[ i + 1 ][ FRUSTUM_NEAR ].dist = DotProduct( planeOrigin, tr.viewParms.frustums[ i + 1 ][ FRUSTUM_NEAR ].normal );
			SetPlaneSignbits( &tr.viewParms.frustums[ i + 1 ][ FRUSTUM_NEAR ] );
		}

		for ( j = 0; j < 4; j++ )
		{
			CopyPlane( &tr.viewParms.frustums[ 0 ][ j ], &tr.viewParms.frustums[ i ][ j ] );
		}
	}
}

/*
=================
R_MirrorPoint
=================
*/
void R_MirrorPoint( vec3_t in, orientation_t *surface, orientation_t *camera, vec3_t out )
{
	int    i;
	vec3_t local;
	vec3_t transformed;
	float  d;

	VectorSubtract( in, surface->origin, local );

	VectorClear( transformed );

	for ( i = 0; i < 3; i++ )
	{
		d = DotProduct( local, surface->axis[ i ] );
		VectorMA( transformed, d, camera->axis[ i ], transformed );
	}

	VectorAdd( transformed, camera->origin, out );
}

/*
=============
R_PlaneForSurface
=============
*/
void R_PlaneForSurface( surfaceType_t *surfType, cplane_t *plane )
{
	srfTriangles_t *tri;
	srfPoly_t      *poly;
	srfVert_t      *v1, *v2, *v3;

	if ( !surfType )
	{
		memset( plane, 0, sizeof( *plane ) );
		plane->normal[ 0 ] = 1;
		return;
	}

	plane_t plane4;
	switch ( *surfType )
	{
		case surfaceType_t::SF_FACE:
			*plane = ( ( srfSurfaceFace_t * ) surfType )->plane;
			return;

		case surfaceType_t::SF_TRIANGLES:
			tri = ( srfTriangles_t * ) surfType;
			v1 = tri->verts + tri->triangles[ 0 ].indexes[ 0 ];
			v2 = tri->verts + tri->triangles[ 0 ].indexes[ 1 ];
			v3 = tri->verts + tri->triangles[ 0 ].indexes[ 2 ];
			PlaneFromPoints( plane4, v1->xyz, v2->xyz, v3->xyz );
			VectorCopy( plane4.normal, plane->normal );
			plane->dist = plane4.dist;
			return;

		case surfaceType_t::SF_POLY:
			poly = ( srfPoly_t * ) surfType;
			PlaneFromPoints( plane4, poly->verts[ 0 ].xyz, poly->verts[ 1 ].xyz, poly->verts[ 2 ].xyz );
			VectorCopy( plane4.normal, plane->normal );
			plane->dist = plane4.dist;
			return;

		default:
			memset( plane, 0, sizeof( *plane ) );
			plane->normal[ 0 ] = 1;
			return;
	}
}

/*
=================
R_GetPortalOrientations

entityNum is the entity that the portal surface is a part of, which may
be moving and rotating.

Returns true if it should be mirrored
=================
*/
static bool R_GetPortalOrientations( drawSurf_t *drawSurf, orientation_t *surface, orientation_t *camera, vec3_t pvsOrigin,
    bool *mirror, vec3_t *outOrigin, vec3_t *outAxis )
{
	cplane_t originalPlane, plane;

	// create plane axis for the portal we are seeing
	R_PlaneForSurface( drawSurf->surface, &originalPlane );

	// rotate the plane if necessary
	vec3_t portalCenter;
	if ( drawSurf->entity != &tr.worldEntity )
	{
		tr.currentEntity = drawSurf->entity;
		VectorCopy( drawSurf->entity->e.origin, portalCenter );

		// get the orientation of the entity
		R_RotateEntityForViewParms( tr.currentEntity, &tr.viewParms, &tr.orientation );

		// rotate the plane, but keep the non-rotated version for matching
		// against the portalSurface entities
		R_LocalNormalToWorld( originalPlane.normal, plane.normal );
		plane.dist = originalPlane.dist + DotProduct( plane.normal, tr.orientation.origin );
	}
	else
	{
		VectorCopy( tr.world->portals[drawSurf->portalNum].origin, portalCenter );
		plane = originalPlane;
	}

	VectorCopy( plane.normal, surface->axis[ 0 ] );
	PerpendicularVector( surface->axis[ 1 ], surface->axis[ 0 ] );
	CrossProduct( surface->axis[ 0 ], surface->axis[ 1 ], surface->axis[ 2 ] );

	// locate the portal entity closest to this plane.
	// origin will be the origin of the portal, origin2 will be
	// the origin of the camera
	trRefEntity_t* currentPortal = nullptr;
	trRefEntity_t* e;
	float minDistance = FLT_MAX;
	for ( int i = 0; i < tr.refdef.numEntities; i++ ) {
		e = &tr.refdef.entities[i];

		if ( e->e.reType != refEntityType_t::RT_PORTALSURFACE ) {
			continue;
		}

		const float distance = Distance( e->e.origin, portalCenter );
		if ( distance < minDistance ) {
			minDistance = distance;
			currentPortal = e;
		}
	}

	if( currentPortal ) {
		// project the origin onto the surface plane to get
		// an origin point we can rotate around
		e = currentPortal;
		float distance = DotProduct( e->e.origin, plane.normal ) - plane.dist;
		VectorMA( e->e.origin, -distance, surface->axis[ 0 ], surface->origin );

		// if the entity is just a mirror, don't use as a camera point
		if ( e->e.oldorigin[ 0 ] == e->e.origin[ 0 ] && e->e.oldorigin[ 1 ] == e->e.origin[ 1 ] && e->e.oldorigin[ 2 ] == e->e.origin[ 2 ] ) {
			VectorScale( plane.normal, plane.dist, surface->origin );
			VectorCopy( surface->origin, camera->origin );
			VectorSubtract( vec3_origin, surface->axis[ 0 ], camera->axis[ 0 ] );
			VectorCopy( surface->axis[ 1 ], camera->axis[ 1 ] );
			VectorCopy( surface->axis[ 2 ], camera->axis[ 2 ] );

			R_MirrorPoint( tr.viewParms.orientation.origin, surface, camera, *outOrigin );
			VectorAdd( e->e.oldorigin, plane.normal, pvsOrigin );

			VectorMA( tr.viewParms.orientation.axis[0], -2 * DotProduct( tr.viewParms.orientation.axis[0], plane.normal ), plane.normal, outAxis[0] );
			VectorMA( tr.viewParms.orientation.axis[1], -2 * DotProduct( tr.viewParms.orientation.axis[1], plane.normal ), plane.normal, outAxis[1] );
			CrossProduct( outAxis[0], outAxis[1], outAxis[2] );

			*mirror = true;
			return true;
		}

		// optionally rotate
		if ( e->e.oldframe ) {
			vec3_t transformed;
			// if a speed is specified
			if ( e->e.frame ) {
				// continuous rotate
				distance = ( tr.refdef.time / 1000.0f ) * e->e.frame;
				VectorCopy( e->e.axis[ 1 ], transformed );
				RotatePointAroundVector( e->e.axis[ 1 ], e->e.axis[ 0 ], transformed, distance );
				CrossProduct( e->e.axis[ 0 ], e->e.axis[ 1 ], e->e.axis[ 2 ] );
			} else {
				// bobbing rotate
				distance = sinf( tr.refdef.time * 0.003f );
				distance *= 4;
				VectorCopy( e->e.axis[ 1 ], transformed );
				RotatePointAroundVector( e->e.axis[ 1 ], e->e.axis[ 0 ], transformed, distance );
				CrossProduct( e->e.axis[ 0 ], e->e.axis[ 1 ], e->e.axis[ 2 ] );
			}
		}

		/* The calculation of the new axes and origin works by first calculating the transforms from world->portal camera
		and portal surface->world, then transforming the current orientation and origin:
		first from portal surface->world, then world->portal camera */

		// World->portal camera
		vec3_t axisAngles;
		AxisToAngles( e->e.axis, axisAngles );
		quat_t worldToCameraQuat;
		QuatFromAngles( worldToCameraQuat, axisAngles[0], axisAngles[1], axisAngles[2] );

		// Portal surface->world
		axis_t drawSurfAxis;
		VectorCopy( plane.normal, drawSurfAxis[0] );
		VectorInverse( drawSurfAxis[0] );
		CrossProduct( drawSurfAxis[0], axisDefault[2], drawSurfAxis[1] );
		VectorInverse( drawSurfAxis[1] );
		CrossProduct( drawSurfAxis[0], drawSurfAxis[1], drawSurfAxis[2] );

		AxisToAngles( drawSurfAxis, axisAngles );
		quat_t surfToWorldQuatPitch;
		quat_t surfToWorldQuatYaw;
		quat_t surfToWorldQuatRoll;
		QuatFromAngles( surfToWorldQuatPitch, -axisAngles[0], 0.0, 0.0 );
		QuatFromAngles( surfToWorldQuatYaw, 0.0, -axisAngles[1], 0.0 );
		QuatFromAngles( surfToWorldQuatRoll, 0.0, 0.0, -axisAngles[2] );

		// Axis transform
		/* We have to transform both of the first 2 axes to always get correct rotation,
		but we can get the last one with a cross product */
		for ( int i = 0; i < 2; i++ ) {
			vec3_t currentAxis;
			VectorCopy( tr.viewParms.orientation.axis[i], currentAxis );

			// QuatTransformVector rotates on a local plane so transform in the worlds XY plane first
			float currentAxisZ = currentAxis[2];
			currentAxis[2] = 0.0;
			QuatTransformVector( surfToWorldQuatYaw, tr.viewParms.orientation.axis[i], currentAxis );
			currentAxis[2] = currentAxisZ;

			// Have to keep rotation separate for each axis in a local to world transform to get the correct result
			QuatTransformVector( surfToWorldQuatPitch, currentAxis, currentAxis );
			QuatTransformVector( surfToWorldQuatRoll, currentAxis, currentAxis );
			QuatTransformVector( worldToCameraQuat, currentAxis, currentAxis );
			VectorCopy( currentAxis, outAxis[i] );
		}

		CrossProduct( outAxis[0], outAxis[1], outAxis[2] );

		// Origin transform
		vec3_t newOrigin;
		VectorSubtract( portalCenter, tr.viewParms.orientation.origin, newOrigin );

		float currentAxisZ = newOrigin[2];
		newOrigin[2] = 0.0;
		QuatTransformVector( surfToWorldQuatYaw, newOrigin, newOrigin );
		newOrigin[2] = currentAxisZ;

		QuatTransformVector( surfToWorldQuatPitch, newOrigin, newOrigin );
		QuatTransformVector( surfToWorldQuatRoll, newOrigin, newOrigin );
		QuatTransformVector( worldToCameraQuat, newOrigin, newOrigin );
		VectorSubtract( e->e.oldorigin, newOrigin, newOrigin );

		VectorCopy( newOrigin, *outOrigin );

		// now get the camera origin and orientation
		VectorCopy( e->e.oldorigin, camera->origin );
		VectorCopy( e->e.oldorigin, pvsOrigin );
		AxisCopy( e->e.axis, camera->axis );
		VectorInverse( camera->axis[0] );
		VectorInverse( camera->axis[1] );

		*mirror = false;
		return true;
	}

	// if we didn't locate a portal entity, don't render anything.
	// We don't want to just treat it as a mirror, because without a
	// portal entity the server won't have communicated a proper entity set
	// in the snapshot

	// Unfortunately, with local movement prediction it is easily possible
	// to see a surface before the server has communicated the matching
	// portal surface entity, so we don't want to print anything here...

	return false;
}

static bool IsMirror( const drawSurf_t *drawSurf )
{
	int           i;
	cplane_t      originalPlane, plane;
	trRefEntity_t *e;
	float         d;

	// create plane axis for the portal we are seeing
	R_PlaneForSurface( drawSurf->surface, &originalPlane );

	// rotate the plane if necessary
	if ( tr.currentEntity != &tr.worldEntity )
	{
		// get the orientation of the entity
		R_RotateEntityForViewParms( tr.currentEntity, &tr.viewParms, &tr.orientation );

		// rotate the plane, but keep the non-rotated version for matching
		// against the portalSurface entities
		R_LocalNormalToWorld( originalPlane.normal, plane.normal );
		plane.dist = originalPlane.dist + DotProduct( plane.normal, tr.orientation.origin );

		// translate the original plane
		originalPlane.dist = originalPlane.dist + DotProduct( originalPlane.normal, tr.orientation.origin );
	}
	else
	{
		plane = originalPlane;
	}

	// locate the portal entity closest to this plane.
	// origin will be the origin of the portal, origin2 will be
	// the origin of the camera
	for ( i = 0; i < tr.refdef.numEntities; i++ )
	{
		e = &tr.refdef.entities[ i ];

		if ( e->e.reType != refEntityType_t::RT_PORTALSURFACE )
		{
			continue;
		}

		d = DotProduct( e->e.origin, originalPlane.normal ) - originalPlane.dist;

		if ( d > 64 || d < -64 )
		{
			continue;
		}

		// if the entity is just a mirror, don't use as a camera point
		if ( e->e.oldorigin[ 0 ] == e->e.origin[ 0 ] && e->e.oldorigin[ 1 ] == e->e.origin[ 1 ] && e->e.oldorigin[ 2 ] == e->e.origin[ 2 ] )
		{
			return true;
		}

		return false;
	}

	return false;
}

/*
** PortalOffScreenOrOutOfRange
**
** Determines if a surface is completely offscreen or out of the portal range,
** also computes a conservative screen rectangle bounds for the surface
**
** Return value:
**    0 = on screen, in range
**    1 = on screen, out of range
**    2 = off screen
*/
int PortalOffScreenOrOutOfRange( const drawSurf_t *drawSurf, screenRect_t& surfRect )
{
	screenRect_t parentRect;
	parentRect.coords[0] = tr.viewParms.scissorX;
	parentRect.coords[1] = tr.viewParms.scissorY;
	parentRect.coords[2] = tr.viewParms.scissorX + tr.viewParms.scissorWidth - 1;
	parentRect.coords[3] = tr.viewParms.scissorY + tr.viewParms.scissorHeight - 1;
	surfRect = parentRect;

	tr.currentEntity = drawSurf->entity;

	// rotate if necessary
	AABB aabb;
	if ( tr.currentEntity != &tr.worldEntity ) {
		VectorCopy( tr.currentEntity->e.origin, aabb.origin );
		VectorCopy( tr.currentEntity->localBounds[0], aabb.mins );
		VectorCopy( tr.currentEntity->localBounds[1], aabb.maxs );
	} else {
		tr.orientation = tr.viewParms.world;
		VectorCopy( tr.world->portals[drawSurf->portalNum].origin, aabb.origin );
		VectorCopy( tr.world->portals[drawSurf->portalNum].mins, aabb.mins );
		VectorCopy( tr.world->portals[drawSurf->portalNum].maxs, aabb.maxs );
	}

	if ( drawSurf->portalNum == -1 ) {
		return 0;
	}

	screenRect_t newRect;
	Vector4Set( newRect.coords, 999999, 999999, -999999, -999999 );

	uint32_t pointOr = 0;
	uint32_t pointAnd = ( uint32_t ) ~0;

	// TODO: Can we just drop the scissor test here? Then we could simply do R_CullBox()
	vec3_t verts[8];
	VectorCopy( aabb.mins, verts[0] );
	VectorSet( verts[1], aabb.maxs[0], aabb.mins[1], aabb.mins[2] );
	VectorSet( verts[2], aabb.mins[0], aabb.maxs[1], aabb.mins[2] );
	VectorSet( verts[3], aabb.maxs[0], aabb.maxs[1], aabb.mins[2] );
	VectorCopy( aabb.maxs, verts[4] );
	VectorSet( verts[5], aabb.maxs[0], aabb.mins[1], aabb.maxs[2] );
	VectorSet( verts[6], aabb.mins[0], aabb.maxs[1], aabb.maxs[2] );
	VectorSet( verts[7], aabb.maxs[0], aabb.maxs[1], aabb.maxs[2] );

	for ( uint32_t i = 0; i < 8; i++ ) {
		uint32_t pointFlags = 0;
		vec4_t normalized;
		vec4_t window;

		vec4_t clip, eye;
		R_TransformModelToClip( verts[i], tr.orientation.modelViewMatrix, tr.viewParms.projectionMatrix, eye, clip );

		R_TransformClipToWindow( clip, &tr.viewParms, normalized, window );

		newRect.coords[0] = std::min( newRect.coords[0], ( int ) window[0] );
		newRect.coords[1] = std::min( newRect.coords[1], ( int ) window[1] );
		newRect.coords[2] = std::max( newRect.coords[2], ( int ) window[0] );
		newRect.coords[3] = std::max( newRect.coords[3], ( int ) window[1] );

		for ( int j = 0; j < 3; j++ ) {
			if ( clip[j] >= clip[3] ) {
				pointFlags |= ( 1 << ( j * 2 ) );
			} else if ( clip[j] <= -clip[3] ) {
				pointFlags |= ( 1 << ( j * 2 + 1 ) );
			}
		}

		pointAnd &= pointFlags;
		pointOr |= pointFlags;
	}

	// if the surface intersects the near plane, then expand the scissor rect to cover the screen because of back projection
	// OPTIMIZE: can be avoided by clipping triangle edges with the near plane
	if ( pointOr & 0x20 ) {
		newRect = parentRect;
	}

	surfRect.coords[0] = std::max( newRect.coords[0], surfRect.coords[0] );
	surfRect.coords[1] = std::max( newRect.coords[1], surfRect.coords[1] );
	surfRect.coords[2] = std::min( newRect.coords[2], surfRect.coords[2] );
	surfRect.coords[3] = std::min( newRect.coords[3], surfRect.coords[3] );

	// trivially reject
	if ( pointAnd ) {
		return 2;
	}

	// mirrors can early out at this point, since we don't do a fade over distance
	// with them (although we could)
	if ( IsMirror( drawSurf ) ) {
		return 0;
	}

	if ( Distance( tr.viewParms.pvsOrigin, aabb.origin ) > ( drawSurf->shader->portalRange * drawSurf->shader->portalRange ) ) {
		return 1;
	}

	return 0;
}

/*
========================
R_SetupPortalFrustum

Creates an oblique view space frustum that closely bounds the part
of the portal render view that is on screen

This frustum can be used for culling surfaces when rendering the portal view
if the frustum planes are transformed using the inverse view matrix
========================
*/
static void R_SetupPortalFrustum( const viewParms_t& oldParms, const orientation_t& camera, viewParms_t& newParms )
{
	// points of the bounding screen rectangle for the portal surface
	vec3_t sbottomleft = { float(newParms.scissorX), float(newParms.scissorY), -1.0f };
	vec3_t stopright = { float(newParms.scissorX + newParms.scissorWidth - 1), float(newParms.scissorY + newParms.scissorHeight - 1), -1.0f };
	vec3_t sbottomright = { stopright[0], sbottomleft[1], -1.0f };
	vec3_t stopleft = { sbottomleft[0], stopright[1], -1.0f };


	vec3_t bottomleft, bottomright, topright, topleft;
	matrix_t invProjViewPortMatrix;

	ASSERT(newParms.portalLevel > 0);

	// need to unproject the bounding rectangle to view space
	MatrixCopy(oldParms.projectionMatrix, invProjViewPortMatrix);
	MatrixInverse(invProjViewPortMatrix);
	MatrixMultiplyTranslation(invProjViewPortMatrix, -1.0, -1.0, -1.0);
	MatrixMultiplyScale(invProjViewPortMatrix, 2.0f / glConfig.vidWidth, 2.0f / glConfig.vidHeight, 2.0);

	frustum_t& frustum = newParms.portalFrustum;
	MatrixTransformPoint(invProjViewPortMatrix, sbottomleft, bottomleft);
	MatrixTransformPoint(invProjViewPortMatrix, sbottomright, bottomright);
	MatrixTransformPoint(invProjViewPortMatrix, stopright, topright);
	MatrixTransformPoint(invProjViewPortMatrix, stopleft, topleft);

	// create frustum planes for the sides of the view space region
	CrossProduct(bottomright, bottomleft, frustum[FRUSTUM_BOTTOM].normal);
	CrossProduct(topright, bottomright, frustum[FRUSTUM_RIGHT].normal);
	CrossProduct(topleft, topright, frustum[FRUSTUM_TOP].normal);
	CrossProduct(bottomleft, topleft, frustum[FRUSTUM_LEFT].normal);

	for (int i = 0; i < 4; i++)
	{
		VectorNormalize(frustum[i].normal);
		SetPlaneSignbits(&frustum[i]);
		frustum[i].dist = 0; // all side planes intersect the view origin
		frustum[i].type = PLANE_NON_AXIAL;
	}

	vec4_t worldNearPlane;

	// find near plane for portal surface
	// note that unlike a normal frustum near plane, this plane may be oblique!
	VectorNegate(camera.axis[0], worldNearPlane);
	worldNearPlane[3] = DotProduct(camera.origin, worldNearPlane);

	// transform to view space
	frustum[FRUSTUM_NEAR].normal[0] = -DotProduct(worldNearPlane, newParms.orientation.axis[1]);
	frustum[FRUSTUM_NEAR].normal[1] = DotProduct(worldNearPlane, newParms.orientation.axis[2]);
	frustum[FRUSTUM_NEAR].normal[2] = -DotProduct(worldNearPlane, newParms.orientation.axis[0]);
	frustum[FRUSTUM_NEAR].dist = DotProduct(worldNearPlane, newParms.orientation.origin) - worldNearPlane[3];

	// calculate new znear for parallel split frustums in this view
	plane_t frustumPlanes[FRUSTUM_PLANES];
	for (int i = 0; i < FRUSTUM_PLANES; i++)
	{
		VectorCopy(frustum[i].normal, frustumPlanes[i].normal);
		frustumPlanes[i].dist = frustum[i].dist;
	}

	vec3_t nearCorners[4];
	R_CalcFrustumNearCorners(frustumPlanes, nearCorners);

	float zNear = DotProduct(nearCorners[0], nearCorners[0]);
	for (int i = 1; i < 4; i++)
	{
		zNear = std::min(DotProduct(nearCorners[i], nearCorners[i]), zNear);
	}

	zNear = sqrtf(zNear);
	zNear = std::max(zNear, r_znear->value);

	newParms.zNear = zNear;
}

/*
========================
R_MirrorViewBySurface

Returns true if another view has been rendered
========================
*/
bool R_MirrorViewBySurface(drawSurf_t *drawSurf)
{
	orientation_t surface, camera;
	screenRect_t  surfRect;

	// don't recursively mirror too much
	if ( tr.viewParms.portalLevel >= r_max_portal_levels->integer &&
	     tr.viewParms.portalLevel > 0 )
	{
		/* Having more than one mirror in a scene is not a bug,
		we just need to prevent infinite recursion.

		Log::Warn("recursive mirror/portal found"); */
		return false;
	}

	if (r_noportals->integer)
	{
		return false;
	}

	// trivially reject portal/mirror
	switch ( PortalOffScreenOrOutOfRange( drawSurf, surfRect ) )
	{
	case 0:
		break;

	case 1:
		// We still need to draw the surface itself when it's out of range, just not the portal view
		R_AddPreparePortalCmd( drawSurf );
		R_AddFinalisePortalCmd( drawSurf );
		DAEMON_FALLTHROUGH;

	case 2:
		return false;
	}

	viewParms_t newParms = tr.viewParms;

	bool foundPortal = R_GetPortalOrientations(
		drawSurf, &surface, &camera, newParms.pvsOrigin, &newParms.isMirror,
		&newParms.orientation.origin, newParms.orientation.axis );

	if ( !foundPortal )
	{
		return false; // bad portal, no portalentity
	}
	if ( newParms.isMirror ) {
		newParms.mirrorLevel++;
	}
	if ( newParms.mirrorLevel % 2 == 1 ) { // Mirrors flip the up vector
		VectorInverse( newParms.orientation.axis[2] );
	}

	// draw stencil mask
	R_AddPreparePortalCmd( drawSurf );

	// save old viewParms so we can return to it after the mirror view
	viewParms_t oldParms = tr.viewParms;

	oldParms.hasNestedViews = true;

	newParms.portalLevel++;

	// convert screen rectangle to scissor test
	newParms.scissorX = surfRect.coords[0];
	newParms.scissorY = surfRect.coords[1];
	newParms.scissorWidth = surfRect.coords[2] - surfRect.coords[0] + 1;
	newParms.scissorHeight = surfRect.coords[3] - surfRect.coords[1] + 1;

	// Scissor width/height must not be negative, so flip the coordinates if needed
	if ( newParms.scissorWidth < 0 ) {
		newParms.scissorX += newParms.scissorWidth;
		newParms.scissorWidth *= -1;
	}
	if ( newParms.scissorHeight < 0 ) {
		newParms.scissorY += newParms.scissorHeight;
		newParms.scissorHeight *= -1;
	}

	// restrict view frustum to screen rect of surface
	R_SetupPortalFrustum(oldParms, camera, newParms);

	// render the mirror view
	R_RenderView( &newParms );

	tr.viewParms = oldParms;

	R_AddFinalisePortalCmd( drawSurf );

	return true;
}

/*
=================
R_SpriteFogNum

See if a sprite is inside a fog volume
=================
*/
int R_SpriteFogNum( trRefEntity_t *ent )
{
	int   i, j;
	fog_t *fog;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL )
	{
		return 0;
	}

	for ( i = 1; i < tr.world->numFogs; i++ )
	{
		fog = &tr.world->fogs[ i ];

		for ( j = 0; j < 3; j++ )
		{
			if ( ent->e.origin[ j ] - ent->e.radius >= fog->bounds[ 1 ][ j ] )
			{
				break;
			}

			if ( ent->e.origin[ j ] + ent->e.radius <= fog->bounds[ 0 ][ j ] )
			{
				break;
			}
		}

		if ( j == 3 )
		{
			return i;
		}
	}

	return 0;
}

/*
=================
R_AddDrawSurf
=================
*/
int R_AddDrawSurf( surfaceType_t *surface, shader_t *shader, int lightmapNum, int fogNum, bool bspSurface, int portalNum )
{
	// instead of checking for overflow, we just mask the index
	// so it wraps around
	const int baseIndex = tr.refdef.numDrawSurfs & DRAWSURF_MASK;
	int index = baseIndex;

	drawSurf_t* drawSurf = &tr.refdef.drawSurfs[ index ];

	drawSurf->entity = tr.currentEntity;
	drawSurf->surface = surface;
	drawSurf->shader = shader;
	drawSurf->bspSurface = bspSurface;
	drawSurf->fog = fogNum;
	drawSurf->portalNum = portalNum;

	int entityNum;

	if ( tr.currentEntity == &tr.worldEntity )
	{
		entityNum = -1;
	}
	else
	{
		entityNum = tr.currentEntity - tr.refdef.entities;
	}

	if (shader->sort > Util::ordinal(shaderSort_t::SS_OPAQUE))
	{
		index = MAX_DRAWSURFS - index; // reverse the sorting (front:back -> back:front)
	}

	drawSurf->setSort( shader->sortedIndex, lightmapNum, entityNum, fogNum, index );

	tr.refdef.numDrawSurfs++;

	// Portal and sky surfaces are not handled by the material system at all
	if ( materialSystem.generatingWorldCommandBuffer && ( shader->isPortal || shader->isSky || shader->autoSpriteMode ) ) {
		if ( shader->isSky && std::find( materialSystem.skyShaders.begin(), materialSystem.skyShaders.end(), shader )
						   == materialSystem.skyShaders.end() ) {
			materialSystem.skyShaders.emplace_back( shader );
		}

		if ( shader->isPortal )
		{
			// R_AddWorldSurfaces guarantees not to add surfaces more than once
			ASSERT_EQ(
				std::find( materialSystem.portalSurfacesTmp.begin(), materialSystem.portalSurfacesTmp.end(), drawSurf ),
				materialSystem.portalSurfacesTmp.end() );
			materialSystem.portalSurfacesTmp.emplace_back( drawSurf );
		}

		if ( shader->autoSpriteMode ) {
			materialSystem.autospriteSurfaces.push_back( *drawSurf );
		}

		return baseIndex;
	}

	if ( shader->depthShader != nullptr ) {
		const int depthSurfIndex = R_AddDrawSurf( surface, shader->depthShader, 0, 0, bspSurface );
		drawSurf->depthSurface = &tr.refdef.drawSurfs[depthSurfIndex];
		drawSurf->depthSurface->materialSystemSkip = true;
	}

	if( !shader->noFog && fogNum >= 1 ) {
		const int fogSurfIndex = R_AddDrawSurf( surface, shader->fogShader, 0, fogNum, bspSurface );
		drawSurf->fogSurface = &tr.refdef.drawSurfs[fogSurfIndex];
		drawSurf->fogSurface->materialSystemSkip = true;
	}

	return baseIndex;
}

static uint32_t currentView = 0;

/*
=================
R_SortDrawSurfs
=================
*/
static void R_SortDrawSurfs()
{
	drawSurf_t   *drawSurf;
	shader_t     *shader;
	int          sort;

	// it is possible for some views to not have any surfaces
	if ( !glConfig2.usingMaterialSystem && tr.viewParms.numDrawSurfs < 1 )
	{
		// we still need to add it for hyperspace cases
		R_AddDrawViewCmd( false );
		return;
	}

	// if we overflowed MAX_DRAWSURFS, the drawsurfs
	// wrapped around in the buffer and we will be missing
	// the first surfaces, not the last ones
	if ( tr.viewParms.numDrawSurfs > MAX_DRAWSURFS )
	{
		tr.viewParms.numDrawSurfs = MAX_DRAWSURFS;
	}

	// if we overflowed MAX_INTERACTIONS, the interactions
	// wrapped around in the buffer and we will be missing
	// the first interactions, not the last ones
	if ( tr.viewParms.numInteractions > MAX_INTERACTIONS )
	{
		interaction_t *ia;

		tr.viewParms.numInteractions = MAX_INTERACTIONS;

		// reset last interaction's next pointer
		ia = &tr.viewParms.interactions[ tr.viewParms.numInteractions - 1 ];
		ia->next = nullptr;
	}

	std::sort( tr.viewParms.drawSurfs, tr.viewParms.drawSurfs + tr.viewParms.numDrawSurfs,
	           []( const drawSurf_t &a, const drawSurf_t &b ) {
	               return a.sort < b.sort;
	           } );

	// compute the offsets of the first surface of each SS_* type
	sort = Util::ordinal( shaderSort_t::SS_BAD ) - 1;
	for ( int i = 0; i < tr.viewParms.numDrawSurfs; i++ )
	{
		drawSurf = &tr.viewParms.drawSurfs[ i ];
		shader = drawSurf->shader;

		// no shader should ever have this sort type
		if ( shader->sort == Util::ordinal(shaderSort_t::SS_BAD) )
		{
			Sys::Drop( "Shader '%s'with sort == SS_BAD", shader->name );
		}

		while ( sort < Util::ordinal( shaderSort_t::SS_NUM_SORTS ) - 1 && shader->sort > sort ) {
			tr.viewParms.firstDrawSurf[ ++sort ] = i;
		}
	}
	while ( sort < Util::ordinal( shaderSort_t::SS_NUM_SORTS ) ) {
		tr.viewParms.firstDrawSurf[ ++sort ] = tr.viewParms.numDrawSurfs;
	}

	// tell renderer backend to render the depth for this view
	R_AddDrawViewCmd( true );

	// check for any pass through drawing, which
	// may cause another view to be rendered first
	// Material system does its own handling of portal surfaces
	if ( glConfig2.usingMaterialSystem && !r_materialSystemSkip.Get() ) {
		if ( tr.viewParms.portalLevel == 0 ) {
			materialSystem.AddPortalSurfaces();
			currentView = 0;
		} else {
			currentView++;
		}

		for ( uint32_t i = 0; i < portalStack[currentView].count; i++ ) {
			uint32_t viewID = portalStack[currentView].views[i];
			if ( viewID == 0 ) {
				break;
			}

			R_MirrorViewBySurface( portalStack[portalStack[currentView].views[i]].drawSurf );
		}
		currentView--;
	} else {
		for ( int i = tr.viewParms.firstDrawSurf[ Util::ordinal(shaderSort_t::SS_PORTAL) ];
			  i < tr.viewParms.firstDrawSurf[ Util::ordinal(shaderSort_t::SS_PORTAL) + 1 ]; i++ )
		{
			drawSurf = &tr.viewParms.drawSurfs[ i ];
			shader = drawSurf->shader;

			R_MirrorViewBySurface( drawSurf );
		}
	}

	// tell renderer backend to render this view
	R_AddDrawViewCmd( false );
}

/*
=============
R_AddEntitySurfaces
=============
*/
void R_AddEntitySurfaces()
{
	int           i;
	trRefEntity_t *ent;
	shader_t      *shader;

	if ( !r_drawentities->integer )
	{
		return;
	}

	for ( i = 0; i < tr.refdef.numEntities; i++ )
	{
		ent = tr.currentEntity = &tr.refdef.entities[ i ];

		//
		// the weapon model must be handled special --
		// we don't want the hacked weapon position showing in
		// mirrors, because the true body position will already be drawn
		//
		if ( ( ent->e.renderfx & RF_FIRST_PERSON ) &&
		     ( tr.viewParms.portalLevel > 0 || tr.viewParms.isMirror ) )
		{
			continue;
		}

		// simple generated models, like sprites and beams, are not culled
		switch ( ent->e.reType )
		{
			case refEntityType_t::RT_PORTALSURFACE:
				break; // don't draw anything

			case refEntityType_t::RT_SPRITE:

				// self blood sprites, talk balloons, etc should not be drawn in the primary
				// view.  We can't just do this check for all entities, because md3
				// entities may still want to cast shadows from them
				if ( ( ent->e.renderfx & RF_THIRD_PERSON ) &&
				     tr.viewParms.portalLevel == 0 )
				{
					continue;
				}

				shader = R_GetShaderByHandle( ent->e.customShader );
				R_AddDrawSurf( &entitySurface, shader, -1, R_SpriteFogNum( ent ) );
				break;

			case refEntityType_t::RT_MODEL:
				// we must set up parts of tr.or for model culling
				R_RotateEntityForViewParms( ent, &tr.viewParms, &tr.orientation );

				tr.currentModel = R_GetModelByHandle( ent->e.hModel );

				if ( !tr.currentModel )
				{
					R_AddDrawSurf( &entitySurface, tr.defaultShader, -1, 0 );
				}
				else
				{
					switch ( tr.currentModel->type )
					{
						case modtype_t::MOD_MESH:
							R_AddMDVSurfaces( ent );
							break;

						case modtype_t::MOD_MD5:
							R_AddMD5Surfaces( ent );
							break;

						case modtype_t::MOD_IQM:
							R_AddIQMSurfaces( ent );
							break;

						case modtype_t::MOD_BSP:
							R_AddBSPModelSurfaces( ent );
							break;

						case modtype_t::MOD_BAD: // null model axis
							if ( ( ent->e.renderfx & RF_THIRD_PERSON ) &&
							     tr.viewParms.portalLevel == 0 )
							{
								break;
							}

							VectorClear( ent->localBounds[ 0 ] );
							VectorClear( ent->localBounds[ 1 ] );
							VectorClear( ent->worldBounds[ 0 ] );
							VectorClear( ent->worldBounds[ 1 ] );
							shader = R_GetShaderByHandle( ent->e.customShader );
							R_AddDrawSurf( &entitySurface, tr.defaultShader, -1, 0 );
							break;

						default:
							Sys::Drop( "R_AddEntitySurfaces: Bad modeltype" );
					}
				}

				break;

			default:
				Sys::Drop( "R_AddEntitySurfaces: Bad reType" );
		}
	}
}

/*
=============
R_AddEntityInteractions
=============
*/
void R_AddEntityInteractions( trRefLight_t *light )
{
	int               i;
	trRefEntity_t     *ent;
	interactionType_t iaType;

	if ( !r_drawentities->integer )
	{
		return;
	}

	for ( i = 0; i < tr.refdef.numEntities; i++ )
	{
		iaType = IA_DEFAULT;

		if ( !glConfig2.shadowMapping || light->l.noShadows ) {
			iaType = (interactionType_t) (iaType & (~IA_SHADOW));
		}

		if ( light->restrictInteractionFirst >= 0 &&
		     ( i < light->restrictInteractionFirst ||
		       i > light->restrictInteractionLast ) ) {
			iaType = (interactionType_t) (iaType & (~IA_SHADOW));
		}

		if ( light->restrictInteractionFirst >= 0 &&
		     i >= light->restrictInteractionFirst &&
		     i <= light->restrictInteractionLast )
		{
			iaType = (interactionType_t) (iaType & ~IA_LIGHT);
		}

		ent = tr.currentEntity = &tr.refdef.entities[ i ];

		//
		// the weapon model must be handled special --
		// we don't want the hacked weapon position showing in
		// mirrors, because the true body position will already be drawn
		//
		if ( ( ent->e.renderfx & RF_FIRST_PERSON ) &&
		     ( tr.viewParms.portalLevel > 0 || tr.viewParms.isMirror ) )
		{
			continue;
		}

		// simple generated models, like sprites and beams, are not culled
		switch ( ent->e.reType )
		{
			case refEntityType_t::RT_PORTALSURFACE:
				break; // don't draw anything

			case refEntityType_t::RT_SPRITE:
				break;

			case refEntityType_t::RT_MODEL:
				tr.currentModel = R_GetModelByHandle( ent->e.hModel );

				if ( tr.currentModel )
				{
					switch ( tr.currentModel->type )
					{
						case modtype_t::MOD_MESH:
							R_AddMDVInteractions( ent, light, iaType );
							break;

						case modtype_t::MOD_MD5:
							R_AddMD5Interactions( ent, light, iaType );
							break;

						case modtype_t::MOD_IQM:
							R_AddIQMInteractions( ent, light, iaType );
							break;

						case modtype_t::MOD_BSP:
							R_AddBrushModelInteractions( ent, light, iaType );
							break;

						case modtype_t::MOD_BAD: // null model axis
							break;

						default:
							Sys::Drop( "R_AddEntityInteractions: Bad modeltype" );
					}
				}

				break;

			default:
				Sys::Drop( "R_AddEntityInteractions: Bad reType" );
		}
	}
}

/*
=============
R_TransformShadowLight

check if OMNI shadow light can be turned into PROJ for better shadow map quality
=============
*/
void R_TransformShadowLight( trRefLight_t *light ) {
	int    i;
	vec3_t mins, maxs, mids;
	vec3_t forward, right, up;
	float  radius;

	if( !light->l.inverseShadows || light->l.rlType != refLightType_t::RL_OMNI ||
	    light->restrictInteractionFirst < 0 )
		return;

	ClearBounds( mins, maxs );
	for( i = light->restrictInteractionFirst; i <= light->restrictInteractionLast; i++ ) {
		trRefEntity_t *ent = &tr.refdef.entities[ i ];

		BoundsAdd(ent->worldBounds[0], ent->worldBounds[1], mins, maxs);
	}

	// if light origin is outside BBox of shadow receivers, build
	// a projection light on the closest plane of the BBox
	VectorAdd( mins, maxs, mids );
	VectorScale( mids, 0.5f, mids );
	radius = Distance( mids, maxs );

	light->l.rlType = refLightType_t::RL_PROJ;
	VectorSubtract( mids, light->l.origin, forward );
	VectorNormalize( forward );
	PerpendicularVector( right, forward );
	CrossProduct( forward, right, up );

	VectorScale( right, 2.0f * radius, light->l.projRight );
	VectorScale( up, 2.0f * radius, light->l.projUp );
	VectorCopy( vec3_origin, light->l.projStart );
	VectorCopy( vec3_origin, light->l.projEnd );
	VectorScale( forward, light->l.radius, light->l.projTarget );
}

/*
=============
R_AddLightInteractions
=============
*/
void R_AddLightInteractions()
{
	int          i;
	trRefLight_t *light;

	realtimeLightingRenderer_t realtimeLightingRenderer = realtimeLightingRenderer_t( r_realtimeLightingRenderer.Get() );

	tr.refdef.numShaderLights = 0;

	for ( i = 0; i < tr.refdef.numLights; i++ )
	{
		light = tr.currentLight = &tr.refdef.lights[ i ];

		if ( !light->l.inverseShadows )
		{
			if ( realtimeLightingRenderer == realtimeLightingRenderer_t::TILED )
			{
				tr.refdef.numShaderLights++;
				tr.pc.c_dlights++;

				continue;
			}
		}

		R_TransformShadowLight( light );

		// we must set up parts of tr.or for light culling
		R_RotateLightForViewParms( light, &tr.viewParms, &tr.orientation );

		// calc local bounds for culling
		{
			// set up light transform matrix
			MatrixSetupTransformFromQuat( light->transformMatrix, light->l.rotation, light->l.origin );

			// set up light origin for lighting and shadowing
			R_SetupLightOrigin( light );

			// set up model to light view matrix
			R_SetupLightView( light );

			// set up projection
			R_SetupLightProjection( light );

			// calc local bounds for culling
			R_SetupLightLocalBounds( light );

			// look if we have to draw the light including its interactions
			switch ( R_CullLocalBox( light->localBounds ) )
			{
				case cullResult_t::CULL_IN:
				default:
					tr.pc.c_box_cull_light_in++;
					break;

				case cullResult_t::CULL_CLIP:
					tr.pc.c_box_cull_light_clip++;
					break;

				case cullResult_t::CULL_OUT:
					// light is not visible so skip other light setup stuff to save speed
					tr.pc.c_box_cull_light_out++;
					continue;
			}

			// setup world bounds for intersection tests
			R_SetupLightWorldBounds( light );

			// setup frustum planes for intersection tests
			R_SetupLightFrustum( light );

			// ignore if not in visible bounds
			if ( !BoundsIntersect
			     ( light->worldBounds[ 0 ], light->worldBounds[ 1 ], tr.viewParms.visBounds[ 0 ], tr.viewParms.visBounds[ 1 ] ) )
			{
				continue;
			}
		}

		// set up view dependent light scissor
		R_SetupLightScissor( light );

		// set up view dependent light Level of Detail
		R_SetupLightLOD( light );

		// look for proper attenuation shader
		R_SetupLightShader( light );

		// setup interactions
		light->firstInteraction = nullptr;
		light->lastInteraction = nullptr;

		light->numInteractions = 0;
		light->numShadowOnlyInteractions = 0;
		light->numLightOnlyInteractions = 0;
		light->noSort = false;

		R_AddWorldInteractions( light );
		R_AddEntityInteractions( light );

		if ( light->numInteractions && light->numInteractions != light->numShadowOnlyInteractions )
		{
			R_SortInteractions( light );

			tr.pc.c_dlights++;
		}
		else
		{
			// skip all interactions of this light because it caused only shadow volumes
			// but no lighting
			tr.refdef.numInteractions -= light->numInteractions;
		}
	}
}

static std::vector<char> botDebugDrawCommands;
void RE_SendBotDebugDrawCommands( std::vector<char> commands )
{
	botDebugDrawCommands = std::move(commands);
}
static void RunBotDebugDrawCommands()
{
	if ( botDebugDrawCommands.empty() )
	{
		return;
	}

	Util::Reader r;
	r.GetData() = botDebugDrawCommands;
	while ( true )
	{
		debugDrawMode_t mode;
		float size;
		bool state;
		Vec3 pos;
		unsigned int color;
		Vec2 uv;
		switch ( r.Read<debugDrawCommand_t>() )
		{
			case debugDrawCommand_t::BEGIN:
				mode = r.Read<debugDrawMode_t>();
				size = r.Read<float>();
				DebugDrawBegin( mode, size );
				continue;
			case debugDrawCommand_t::END:
				DebugDrawEnd();
				continue;
			case debugDrawCommand_t::DEPTHMASK:
				state = r.Read<bool>();
				DebugDrawDepthMask( state );
				continue;
			case debugDrawCommand_t::VERTEX:
				pos = r.Read<Vec3>();
				color = r.Read<unsigned int>();
				DebugDrawVertex(pos.Data(), color, nullptr);
				continue;
			case debugDrawCommand_t::VERTEX_UV:
				pos = r.Read<Vec3>();
				color = r.Read<unsigned int>();
				uv = r.Read<Vec2>();
				DebugDrawVertex(pos.Data(), color, uv.Data());
				continue;
			case debugDrawCommand_t::EOC:
				return;
		}
		Log::Warn("malformed bot debug draw commands");
		return;
	}
}
/*
====================
R_DebugGraphics

Visualization aid for movement clipping debugging
====================
*/
static void R_DebugGraphics()
{
	if ( r_debugSurface->integer )
	{
		// the render thread can't make callbacks to the main thread
		R_SyncRenderThread();

		GL_BindNullProgram();

		GL_Bind( tr.whiteImage );

		GL_Cull( cullType_t::CT_FRONT_SIDED );
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

		RunBotDebugDrawCommands();
	}
}

/*
================
R_RenderView

A view may be either the actual camera view,
or a mirror / remote location
================
*/
void R_RenderView( viewParms_t *parms )
{
	int      firstDrawSurf;
	int      firstInteraction;

	if ( parms->viewportWidth <= 0 || parms->viewportHeight <= 0 )
	{
		return;
	}

	tr.viewCount++;
	tr.viewCountNoReset++;

	if ( tr.viewCount >= MAX_VIEWS )
	{
		Log::Notice("MAX_VIEWS (%i) hit. Don't add more mirrors or portals. Skipping view ...", MAX_VIEWS );
		return;
	}

	tr.viewParms = *parms;
	tr.viewParms.frameSceneNum = tr.frameSceneNum;
	tr.viewParms.frameCount = tr.frameCount;
	tr.viewParms.viewCount = tr.viewCount; // % MAX_VIEWS;

	firstDrawSurf = tr.refdef.numDrawSurfs;
	firstInteraction = tr.refdef.numInteractions;

	// set viewParms.world
	R_RotateForViewer();

	// set the projection matrix with the far clip plane set at infinity
	// this required for the CHC++ algorithm
	R_SetupProjection( true );

	R_SetupFrustum();

	if ( glConfig2.usingMaterialSystem && !r_materialSystemSkip.Get() ) {
		tr.viewParms.viewID = tr.viewCount;
		materialSystem.QueueSurfaceCull( tr.viewCount, tr.viewParms.pvsOrigin, (frustum_t*) tr.viewParms.frustums[0] );
		materialSystem.AddAutospriteSurfaces();
	} else {
		R_AddWorldSurfaces();
	}

	R_AddPolygonSurfaces();

	// set the projection matrix now that we have the world bounded
	// this needs to be done before entities are
	// added, because they use the projection
	// matrix for lod calculation
	R_SetupProjection( false );

	R_SetupUnprojection();

	// set camera frustum planes in world space again, but this time including the far plane
	tr.orientation = tr.viewParms.world;

	// for parallel split shadow mapping
	R_SetupSplitFrustums();

	R_AddEntitySurfaces();

	R_AddLightInteractions();

	// Transform the blur vector in view space, FIXME for some we need reason invert its Z component
	MatrixTransformNormal2( tr.viewParms.world.viewMatrix, tr.refdef.blurVec );
	tr.refdef.blurVec[2] *= -1;

	tr.viewParms.drawSurfs = tr.refdef.drawSurfs + firstDrawSurf;
	tr.viewParms.numDrawSurfs = tr.refdef.numDrawSurfs - firstDrawSurf;

	tr.viewParms.interactions = tr.refdef.interactions + firstInteraction;
	tr.viewParms.numInteractions = tr.refdef.numInteractions - firstInteraction;

	R_SortDrawSurfs();

	// draw main system development information (surface outlines, etc)
	R_DebugGraphics();
}

/*
================
R_RenderPostProcess

Render Post Process effects that must only happen once all views for a scene are rendered
================
*/
void R_RenderPostProcess()
{
	R_AddPostProcessCmd();
}
