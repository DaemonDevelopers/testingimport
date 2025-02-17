/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2006-2010 Robert Beckebans <trebor_7@users.sourceforge.net>

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
// tr_models.c -- model loading and caching
#include "tr_local.h"

#define LL(x) x = LittleLong(x)
#define LF(x) x = LittleFloat(x)

/*
=================
R_LoadMD3
=================
*/
bool R_LoadMD3( model_t *mod, int lod, const void *buffer, const char *modName )
{
	int            i, j, k; //, l;

	md3Header_t    *md3Model;
	md3Frame_t     *md3Frame;
	md3Surface_t   *md3Surf;
	md3Shader_t    *md3Shader;
	md3Triangle_t  *md3Tri;
	md3St_t        *md3st;
	md3XyzNormal_t *md3xyz;
	md3Tag_t       *md3Tag;

	mdvModel_t     *mdvModel;
	mdvFrame_t     *frame;
	mdvSurface_t   *surf; //, *surface;
	srfTriangle_t  *tri;
	mdvXyz_t       *v;
	mdvNormal_t    *n;
	mdvSt_t        *st;
	mdvTag_t       *tag;
	mdvTagName_t   *tagName;

	int            version;
	int            size;

	md3Model = ( md3Header_t * ) buffer;

	version = LittleLong( md3Model->version );

	if ( version != MD3_VERSION )
	{
		Log::Warn("R_LoadMD3: %s has wrong version (%i should be %i)", modName, version, MD3_VERSION );
		return false;
	}

	mod->type = modtype_t::MOD_MESH;
	size = LittleLong( md3Model->ofsEnd );
	mod->dataSize += size;
	mdvModel = mod->mdv[ lod ] = (mdvModel_t*) ri.Hunk_Alloc( sizeof( mdvModel_t ), ha_pref::h_low );

	LL( md3Model->ident );
	LL( md3Model->version );
	LL( md3Model->numFrames );
	LL( md3Model->numTags );
	LL( md3Model->numSurfaces );
	LL( md3Model->ofsFrames );
	LL( md3Model->ofsTags );
	LL( md3Model->ofsSurfaces );
	LL( md3Model->ofsEnd );

	if ( md3Model->numFrames < 1 )
	{
		Log::Warn("R_LoadMD3: %s has no frames", modName );
		return false;
	}

	// swap all the frames
	mdvModel->numFrames = md3Model->numFrames;
	mdvModel->frames = frame = (mdvFrame_t*) ri.Hunk_Alloc( sizeof( *frame ) * md3Model->numFrames, ha_pref::h_low );

	md3Frame = ( md3Frame_t * )( ( byte * ) md3Model + md3Model->ofsFrames );

	for ( i = 0; i < md3Model->numFrames; i++, frame++, md3Frame++ )
	{
		frame->radius = LittleFloat( md3Frame->radius );

		for ( j = 0; j < 3; j++ )
		{
			frame->bounds[ 0 ][ j ] = LittleFloat( md3Frame->bounds[ 0 ][ j ] );
			frame->bounds[ 1 ][ j ] = LittleFloat( md3Frame->bounds[ 1 ][ j ] );
			frame->localOrigin[ j ] = LittleFloat( md3Frame->localOrigin[ j ] );
		}
	}

	// swap all the tags
	mdvModel->numTags = md3Model->numTags;
	mdvModel->tags = tag = (mdvTag_t*) ri.Hunk_Alloc( sizeof( *tag ) * ( md3Model->numTags * md3Model->numFrames ), ha_pref::h_low );

	md3Tag = ( md3Tag_t * )( ( byte * ) md3Model + md3Model->ofsTags );

	for ( i = 0; i < md3Model->numTags * md3Model->numFrames; i++, tag++, md3Tag++ )
	{
		for ( j = 0; j < 3; j++ )
		{
			tag->origin[ j ] = LittleFloat( md3Tag->origin[ j ] );
			tag->axis[ 0 ][ j ] = LittleFloat( md3Tag->axis[ 0 ][ j ] );
			tag->axis[ 1 ][ j ] = LittleFloat( md3Tag->axis[ 1 ][ j ] );
			tag->axis[ 2 ][ j ] = LittleFloat( md3Tag->axis[ 2 ][ j ] );
		}
	}

	mdvModel->tagNames = tagName = (mdvTagName_t*) ri.Hunk_Alloc( sizeof( *tagName ) * ( md3Model->numTags ), ha_pref::h_low );

	md3Tag = ( md3Tag_t * )( ( byte * ) md3Model + md3Model->ofsTags );

	for ( i = 0; i < md3Model->numTags; i++, tagName++, md3Tag++ )
	{
		Q_strncpyz( tagName->name, md3Tag->name, sizeof( tagName->name ) );
	}

	// swap all the surfaces
	mdvModel->numSurfaces = md3Model->numSurfaces;
	mdvModel->surfaces = surf = (mdvSurface_t*) ri.Hunk_Alloc( sizeof( *surf ) * md3Model->numSurfaces, ha_pref::h_low );

	md3Surf = ( md3Surface_t * )( ( byte * ) md3Model + md3Model->ofsSurfaces );

	for ( i = 0; i < md3Model->numSurfaces; i++ )
	{
		LL( md3Surf->ident );
		LL( md3Surf->flags );
		LL( md3Surf->numFrames );
		LL( md3Surf->numShaders );
		LL( md3Surf->numTriangles );
		LL( md3Surf->ofsTriangles );
		LL( md3Surf->numVerts );
		LL( md3Surf->ofsShaders );
		LL( md3Surf->ofsSt );
		LL( md3Surf->ofsXyzNormals );
		LL( md3Surf->ofsEnd );

		if ( !r_vboModels.Get() && md3Surf->numVerts > SHADER_MAX_VERTEXES )
		{
			Sys::Drop( "R_LoadMD3: %s has more than %i verts on a surface (%i)",
			           modName, SHADER_MAX_VERTEXES, md3Surf->numVerts );
		}

		if ( !r_vboModels.Get() && md3Surf->numTriangles * 3 > SHADER_MAX_INDEXES )
		{
			Sys::Drop( "R_LoadMD3: %s has more than %i triangles on a surface (%i)",
			           modName, SHADER_MAX_INDEXES / 3, md3Surf->numTriangles );
		}

		// change to surface identifier
		surf->surfaceType = surfaceType_t::SF_MDV;

		// give pointer to model for Tess_SurfaceMDV
		surf->model = mdvModel;

		// copy surface name
		Q_strncpyz( surf->name, md3Surf->name, sizeof( surf->name ) );

		// lowercase the surface name so skin compares are faster
		Q_strlwr( surf->name );

		// strip off a trailing _1 or _2
		// this is a crutch for q3data being a mess
		j = strlen( surf->name );

		if ( j > 2 && surf->name[ j - 2 ] == '_' )
		{
			surf->name[ j - 2 ] = 0;
		}

		// only consider the first shader
		md3Shader = ( md3Shader_t * )( ( byte * ) md3Surf + md3Surf->ofsShaders );
		surf->shader = R_FindShader( md3Shader->name, shaderType_t::SHADER_3D_DYNAMIC, RSF_DEFAULT );

		// swap all the triangles
		surf->numTriangles = md3Surf->numTriangles;
		surf->triangles = tri = (srfTriangle_t*) ri.Hunk_Alloc( sizeof( *tri ) * md3Surf->numTriangles, ha_pref::h_low );

		md3Tri = ( md3Triangle_t * )( ( byte * ) md3Surf + md3Surf->ofsTriangles );

		for ( j = 0; j < md3Surf->numTriangles; j++, tri++, md3Tri++ )
		{
			tri->indexes[ 0 ] = LittleLong( md3Tri->indexes[ 0 ] );
			tri->indexes[ 1 ] = LittleLong( md3Tri->indexes[ 1 ] );
			tri->indexes[ 2 ] = LittleLong( md3Tri->indexes[ 2 ] );
		}

		// swap all the XyzNormals
		surf->numVerts = md3Surf->numVerts;
		surf->verts = v = (mdvXyz_t*) ri.Hunk_Alloc( sizeof( *v ) * ( md3Surf->numVerts * md3Surf->numFrames ), ha_pref::h_low );
		surf->normals = n = (mdvNormal_t *) ri.Hunk_Alloc( sizeof( *n ) * ( md3Surf->numVerts * md3Surf->numFrames ), ha_pref::h_low );

		md3xyz = ( md3XyzNormal_t * )( ( byte * ) md3Surf + md3Surf->ofsXyzNormals );

		for ( j = 0; j < md3Surf->numVerts * md3Surf->numFrames; j++, md3xyz++, v++, n++ )
		{
			unsigned short latLng = LittleShort( md3xyz->normal );
			float lat, lng;

			v->xyz[ 0 ] = LittleShort( md3xyz->xyz[ 0 ] ) * MD3_XYZ_SCALE;
			v->xyz[ 1 ] = LittleShort( md3xyz->xyz[ 1 ] ) * MD3_XYZ_SCALE;
			v->xyz[ 2 ] = LittleShort( md3xyz->xyz[ 2 ] ) * MD3_XYZ_SCALE;

			lat = (latLng >> 8) * (Math::mul2_pi_f / 255.0f);
			lng = (latLng & 255) * (Math::mul2_pi_f / 255.0f);
			n->normal[ 0 ] = cosf ( lat ) * sinf ( lng );
			n->normal[ 1 ] = sinf ( lat ) * sinf ( lng );
			n->normal[ 2 ] = cosf ( lng );
		}

		// swap all the ST
		surf->st = st = (mdvSt_t*) ri.Hunk_Alloc( sizeof( *st ) * md3Surf->numVerts, ha_pref::h_low );

		md3st = ( md3St_t * )( ( byte * ) md3Surf + md3Surf->ofsSt );

		for ( j = 0; j < md3Surf->numVerts; j++, md3st++, st++ )
		{
			st->st[ 0 ] = LittleFloat( md3st->st[ 0 ] );
			st->st[ 1 ] = LittleFloat( md3st->st[ 1 ] );
		}

		// find the next surface
		md3Surf = ( md3Surface_t * )( ( byte * ) md3Surf + md3Surf->ofsEnd );
		surf++;
	}

	// create VBO surfaces from md3 surfaces
	{
		srfVBOMDVMesh_t *vboSurf;
		glIndex_t       *indexes;

		int             f;

		std::vector<srfVBOMDVMesh_t *> vboSurfaces;
		vboSurfaces.reserve( 10 );

		for ( i = 0, surf = mdvModel->surfaces; i < mdvModel->numSurfaces; i++, surf++ )
		{
			//allocate temp memory for vertex data
			vec3_t *scaledPosition = (vec3_t *)ri.Hunk_AllocateTempMemory( sizeof( vec3_t ) * mdvModel->numFrames * surf->numVerts );
			i16vec4_t *qtangents = (i16vec4_t *)ri.Hunk_AllocateTempMemory( sizeof( i16vec4_t ) * mdvModel->numFrames * surf->numVerts );

			for ( int r = surf->numVerts * mdvModel->numFrames; r--; )
			{
				VectorScale( surf->verts[ r ].xyz, 1.0f / 512.0f, scaledPosition[ r ] );
			}

			// calc and feed tangent spaces
			{
				const float *v0, *v1, *v2;
				const float *t0, *t1, *t2;
				vec3_t      tangent, *tangents;
				vec3_t      binormal, *binormals;

				tangents = (vec3_t *)ri.Hunk_AllocateTempMemory( surf->numVerts * sizeof( vec3_t ) );
				binormals = (vec3_t *)ri.Hunk_AllocateTempMemory( surf->numVerts * sizeof( vec3_t ) );

				for ( f = 0; f < mdvModel->numFrames; f++ )
				{
					for ( j = 0; j < ( surf->numVerts ); j++ )
					{
						VectorClear( tangents[ j ] );
						VectorClear( binormals[ j ] );
					}

					for ( j = 0, tri = surf->triangles; j < surf->numTriangles; j++, tri++ )
					{
						v0 = surf->verts[ surf->numVerts * f + tri->indexes[ 0 ] ].xyz;
						v1 = surf->verts[ surf->numVerts * f + tri->indexes[ 1 ] ].xyz;
						v2 = surf->verts[ surf->numVerts * f + tri->indexes[ 2 ] ].xyz;

						t0 = surf->st[ tri->indexes[ 0 ] ].st;
						t1 = surf->st[ tri->indexes[ 1 ] ].st;
						t2 = surf->st[ tri->indexes[ 2 ] ].st;

						R_CalcTangents( tangent, binormal, v0, v1, v2, t0, t1, t2 );

						for ( k = 0; k < 3; k++ )
						{
							float *w;

							w = tangents[ tri->indexes[ k ] ];
							VectorAdd( w, tangent, w );

							w = binormals[ tri->indexes[ k ] ];
							VectorAdd( w, binormal, w );
						}
					}

					for ( j = 0; j < surf->numVerts; j++ )
					{
						VectorNormalize( tangents[ j ] );
						VectorNormalize( binormals[ j ] );
						R_TBNtoQtangents(
							tangents[ j ], binormals[ j ],
							surf->normals[ f * surf->numVerts + j ].normal,
							qtangents[ f * surf->numVerts + j ] );
					}
				}

				ri.Hunk_FreeTempMemory( binormals );
				ri.Hunk_FreeTempMemory( tangents );
			}

			// create surface

			vboSurf = (srfVBOMDVMesh_t*) ri.Hunk_Alloc( sizeof( *vboSurf ), ha_pref::h_low );
			vboSurfaces.push_back( vboSurf );

			vboSurf->surfaceType = surfaceType_t::SF_VBO_MDVMESH;
			vboSurf->mdvModel = mdvModel;
			vboSurf->mdvSurface = surf;
			vboSurf->numIndexes = surf->numTriangles * 3;
			vboSurf->numVerts = surf->numVerts;

			// MD3 does not have color, but shaders always require the color vertex attribute, so we have
			// to provide this 0 color.
			const byte dummyColor[ 4 ]{};

			vertexAttributeSpec_t attributes[] {
				{ ATTR_INDEX_TEXCOORD, GL_FLOAT, GL_HALF_FLOAT, surf->st, 2, sizeof(mdvSt_t), 0 },
				{ ATTR_INDEX_COLOR, GL_UNSIGNED_BYTE, GL_UNSIGNED_BYTE, dummyColor, 4, 0, ATTR_OPTION_NORMALIZE },
				{ ATTR_INDEX_QTANGENT, GL_SHORT, GL_SHORT, qtangents, 4, sizeof(i16vec4_t), ATTR_OPTION_NORMALIZE | ATTR_OPTION_HAS_FRAMES },
				{ ATTR_INDEX_POSITION, GL_FLOAT, GL_SHORT, scaledPosition, 3, sizeof(vec3_t), ATTR_OPTION_NORMALIZE | ATTR_OPTION_HAS_FRAMES },
			};
			std::string name = Str::Format( "%s '%s'", modName , surf->name );
			vboSurf->vbo = R_CreateStaticVBO( "MD3 surface VBO " + name,
			                                  std::begin( attributes ), std::end( attributes ),
			                                  surf->numVerts, mdvModel->numFrames );

			// HACK: the shader binding system needs duplicate definitions of some vertex attributes
			vboSurf->vbo->attribBits |= ATTR_POSITION2 | ATTR_QTANGENT2;
			vboSurf->vbo->attribs[ ATTR_INDEX_POSITION2 ] = vboSurf->vbo->attribs[ ATTR_INDEX_POSITION ];
			vboSurf->vbo->attribs[ ATTR_INDEX_QTANGENT2 ] = vboSurf->vbo->attribs[ ATTR_INDEX_QTANGENT ];
			
			ri.Hunk_FreeTempMemory( qtangents );
			ri.Hunk_FreeTempMemory( scaledPosition );

			indexes = (glIndex_t *)ri.Hunk_AllocateTempMemory( 3 * surf->numTriangles * sizeof( glIndex_t ) );
			for ( f = j = 0; j < surf->numTriangles; j++ ) {
				for( k = 0; k < 3; k++ ) {
					indexes[ f++ ] = surf->triangles[ j ].indexes[ k ];
				}
			}
			vboSurf->ibo = R_CreateStaticIBO2( ( "MD3 surface IBO " + name ).c_str(), surf->numTriangles, indexes );

			ri.Hunk_FreeTempMemory(indexes);
		}

		// move VBO surfaces list to hunk
		mdvModel->numVBOSurfaces = vboSurfaces.size();
		size_t allocSize = vboSurfaces.size() * sizeof( vboSurfaces[ 0 ] );
		mdvModel->vboSurfaces = (srfVBOMDVMesh_t**) ri.Hunk_Alloc( allocSize, ha_pref::h_low );
		std::copy( vboSurfaces.begin(), vboSurfaces.end(), mdvModel->vboSurfaces );
	}

	return true;
}
