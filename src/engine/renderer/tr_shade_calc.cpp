/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2006-2008 Robert Beckebans <trebor_7@users.sourceforge.net>

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
// tr_shade_calc.c
#include "tr_local.h"

#define WAVEVALUE( table, base, amplitude, phase, freq ) (( base ) + table[ Q_ftol( ( ( ( phase ) + backEnd.refdef.floatTime * ( freq ) ) * FUNCTABLE_SIZE ) ) & FUNCTABLE_MASK ] * ( amplitude ))

static float   *TableForFunc( genFunc_t func )
{
	switch ( func )
	{
		case genFunc_t::GF_SIN:
			return tr.sinTable;

		case genFunc_t::GF_TRIANGLE:
			return tr.triangleTable;

		case genFunc_t::GF_SQUARE:
			return tr.squareTable;

		case genFunc_t::GF_SAWTOOTH:
			return tr.sawToothTable;

		case genFunc_t::GF_INVERSE_SAWTOOTH:
			return tr.inverseSawToothTable;

		case genFunc_t::GF_NONE:
		default:
			break;
	}

	return tr.sinTable;
}

/*
** EvalWaveForm
**
** Evaluates a given waveForm_t, referencing backEnd.refdef.time directly
*/
float RB_EvalWaveForm( const waveForm_t *wf )
{
	float *table;

	table = TableForFunc( wf->func );

	return WAVEVALUE( table, wf->base, wf->amplitude, wf->phase, wf->frequency );
}

float RB_EvalWaveFormClamped( const waveForm_t *wf )
{
	float glow = RB_EvalWaveForm( wf );

	if ( glow < 0 )
	{
		return 0;
	}

	if ( glow > 1 )
	{
		return 1;
	}

	return glow;
}

static float GetOpValue( const expOperation_t *op )
{
	float value;
	float inv255 = 1.0f / 255.0f;

	switch ( op->type )
	{
		case opcode_t::OP_NUM:
			value = op->value;
			break;

		case opcode_t::OP_TIME:
			value = backEnd.refdef.floatTime;
			break;

		case opcode_t::OP_PARM0:
			if ( backEnd.currentLight )
			{
				value = backEnd.currentLight->l.color[ 0 ];
				break;
			}
			else if ( backEnd.currentEntity )
			{
				value = backEnd.currentEntity->e.shaderRGBA.Red() * inv255;
			}
			else
			{
				value = 1.0;
			}

			break;

		case opcode_t::OP_PARM1:
			if ( backEnd.currentLight )
			{
				value = backEnd.currentLight->l.color[ 1 ];
				break;
			}
			else if ( backEnd.currentEntity )
			{
				value = backEnd.currentEntity->e.shaderRGBA.Green() * inv255;
			}
			else
			{
				value = 1.0;
			}

			break;

		case opcode_t::OP_PARM2:
			if ( backEnd.currentLight )
			{
				value = backEnd.currentLight->l.color[ 2 ];
				break;
			}
			else if ( backEnd.currentEntity )
			{
				value = backEnd.currentEntity->e.shaderRGBA.Blue() * inv255;
			}
			else
			{
				value = 1.0;
			}

			break;

		case opcode_t::OP_PARM3:
			if ( backEnd.currentLight )
			{
				value = 1.0;
				break;
			}
			else if ( backEnd.currentEntity )
			{
				value = backEnd.currentEntity->e.shaderRGBA.Alpha() * inv255;
			}
			else
			{
				value = 1.0;
			}

			break;

		case opcode_t::OP_PARM4:
			if ( backEnd.currentEntity )
			{
				value = -backEnd.currentEntity->e.shaderTime;
			}
			else
			{
				value = 0.0;
			}

			break;

		case opcode_t::OP_PARM5:
		case opcode_t::OP_PARM6:
		case opcode_t::OP_PARM7:
		case opcode_t::OP_PARM8:
		case opcode_t::OP_PARM9:
		case opcode_t::OP_PARM10:
		case opcode_t::OP_PARM11:
		case opcode_t::OP_GLOBAL0:
		case opcode_t::OP_GLOBAL1:
		case opcode_t::OP_GLOBAL2:
		case opcode_t::OP_GLOBAL3:
		case opcode_t::OP_GLOBAL4:
		case opcode_t::OP_GLOBAL5:
		case opcode_t::OP_GLOBAL6:
		case opcode_t::OP_GLOBAL7:
			value = 1.0;
			break;

		case opcode_t::OP_FRAGMENTSHADERS:
			value = 1.0;
			break;

		case opcode_t::OP_FRAMEBUFFEROBJECTS:
			value = 1.0f;
			break;

		case opcode_t::OP_SOUND:
			value = 0.5;
			break;

		case opcode_t::OP_DISTANCE:
			value = 0.0; // FIXME ?
			break;

		default:
			value = 0.0;
			break;
	}

	return value;
}

const char* GetOpName(opcode_t type);

float RB_EvalExpression( const expression_t *exp, float defaultValue )
{
	ASSERT( exp );

	if ( !exp->numOps )
	{
		return defaultValue;
	}

	expOperation_t ops[ MAX_EXPRESSION_OPS ];

	size_t numOps = 0;
	float value = 0.0;
	float value1 = 0.0;
	float value2 = 0.0;

	// http://www.qiksearch.com/articles/cs/postfix-evaluation/
	// http://www.kyz.uklinux.net/evaluate/

	for ( size_t i = 0; i < exp->numOps; i++ )
	{
		expOperation_t op = exp->ops[ i ];

		switch ( op.type )
		{
			case opcode_t::OP_BAD:
				return defaultValue;

			case opcode_t::OP_NEG:
				{
					if ( numOps < 1 )
					{
						Log::Warn("shader %s has numOps < 1 for unary - operator", tess.surfaceShader->name );
						return defaultValue;
					}

					value1 = GetOpValue( &ops[ numOps - 1 ] );
					numOps--;

					value = -value1;

					// push result
					op.type = opcode_t::OP_NUM;
					op.value = value;
					ops[ numOps++ ] = op;
					break;
				}

			case opcode_t::OP_NUM:
			case opcode_t::OP_TIME:
			case opcode_t::OP_PARM0:
			case opcode_t::OP_PARM1:
			case opcode_t::OP_PARM2:
			case opcode_t::OP_PARM3:
			case opcode_t::OP_PARM4:
			case opcode_t::OP_PARM5:
			case opcode_t::OP_PARM6:
			case opcode_t::OP_PARM7:
			case opcode_t::OP_PARM8:
			case opcode_t::OP_PARM9:
			case opcode_t::OP_PARM10:
			case opcode_t::OP_PARM11:
			case opcode_t::OP_GLOBAL0:
			case opcode_t::OP_GLOBAL1:
			case opcode_t::OP_GLOBAL2:
			case opcode_t::OP_GLOBAL3:
			case opcode_t::OP_GLOBAL4:
			case opcode_t::OP_GLOBAL5:
			case opcode_t::OP_GLOBAL6:
			case opcode_t::OP_GLOBAL7:
			case opcode_t::OP_FRAGMENTSHADERS:
			case opcode_t::OP_FRAMEBUFFEROBJECTS:
			case opcode_t::OP_SOUND:
			case opcode_t::OP_DISTANCE:
				ops[ numOps++ ] = op;
				break;

			case opcode_t::OP_TABLE:
				{
					shaderTable_t *table;
					int           numValues;
					float         index;
					float         lerp;
					int           oldIndex;
					int           newIndex;

					if ( numOps < 1 )
					{
						Log::Warn("shader %s has numOps < 1 for table operator", tess.surfaceShader->name );
						return defaultValue;
					}

					value1 = GetOpValue( &ops[ numOps - 1 ] );
					numOps--;

					table = tr.shaderTables[( int ) op.value ];

					numValues = table->numValues;

					index = value1 * numValues; // float index into the table?s elements
					lerp = index - floor( index );  // being inbetween two elements of the table

					oldIndex = ( int ) index;
					newIndex = ( int ) index + 1;

					if ( table->clamp )
					{
						// clamp indices to table-range
						oldIndex = Math::Clamp( oldIndex, 0, numValues - 1 );
						newIndex = Math::Clamp( newIndex, 0, numValues - 1 );
					}
					else
					{
						// wrap around indices
						oldIndex %= numValues;
						newIndex %= numValues;
					}

					if ( table->snap )
					{
						// use fixed value
						value = table->values[ oldIndex ];
					}
					else
					{
						// lerp value
						value = table->values[ oldIndex ] + ( ( table->values[ newIndex ] - table->values[ oldIndex ] ) * lerp );
					}

					//Log::Notice("%s: %i %i %f", table->name, oldIndex, newIndex, value);

					// push result
					op.type = opcode_t::OP_NUM;
					op.value = value;
					ops[ numOps++ ] = op;
					break;
				}

			default:
				{
					if ( numOps < 2 )
					{
						Log::Warn("shader %s has numOps < 2 for binary operator %s", tess.surfaceShader->name,
						           GetOpName( op.type ) );
						return defaultValue;
					}

					value2 = GetOpValue( &ops[ numOps - 1 ] );
					numOps--;

					value1 = GetOpValue( &ops[ numOps - 1 ] );
					numOps--;

					switch ( op.type )
					{
						case opcode_t::OP_LAND:
							value = value1 && value2;
							break;

						case opcode_t::OP_LOR:
							value = value1 || value2;
							break;

						case opcode_t::OP_GE:
							value = value1 >= value2;
							break;

						case opcode_t::OP_LE:
							value = value1 <= value2;
							break;

						case opcode_t::OP_LEQ:
							value = value1 == value2;
							break;

						case opcode_t::OP_LNE:
							value = value1 != value2;
							break;

						case opcode_t::OP_ADD:
							value = value1 + value2;
							break;

						case opcode_t::OP_SUB:
							value = value1 - value2;
							break;

						case opcode_t::OP_DIV:
							if ( value2 == 0 )
							{
								// don't divide by zero
								value = value1;
							}
							else
							{
								value = value1 / value2;
							}

							break;

						case opcode_t::OP_MOD:
							value = ( float )( ( int ) value1 % ( int ) value2 );
							break;

						case opcode_t::OP_MUL:
							value = value1 * value2;
							break;

						case opcode_t::OP_LT:
							value = value1 < value2;
							break;

						case opcode_t::OP_GT:
							value = value1 > value2;
							break;

						default:
							value = value1 = value2 = 0;
							break;
					}

					// push result
					op.type = opcode_t::OP_NUM;
					op.value = value;
					ops[ numOps++ ] = op;
					break;
				}
		}
	}

	return GetOpValue( &ops[ 0 ] );
}

/*
====================================================================

DEFORMATIONS

====================================================================
*/

static void GlobalVectorToLocal( const vec3_t in, vec3_t out )
{
	out[ 0 ] = DotProduct( in, backEnd.orientation.axis[ 0 ] );
	out[ 1 ] = DotProduct( in, backEnd.orientation.axis[ 1 ] );
	out[ 2 ] = DotProduct( in, backEnd.orientation.axis[ 2 ] );
}

/*
=====================
AutospriteDeform

Assuming all the triangles for this shader are independent
quads, rebuild them as forward facing sprites
They face toward the *view direction* like autosprite2 style 0. We could implement style
1 (toward viewer) here as well, but the difference seems less noticeable.
=====================
*/
static void AutospriteDeform( uint32_t numVertexes )
{
	vec3_t leftDir, upDir;

	if ( backEnd.currentEntity != &tr.worldEntity )
	{
		GlobalVectorToLocal( backEnd.viewParms.orientation.axis[ 1 ], leftDir );
		GlobalVectorToLocal( backEnd.viewParms.orientation.axis[ 2 ], upDir );
	}
	else
	{
		VectorCopy( backEnd.viewParms.orientation.axis[ 1 ], leftDir );
		VectorCopy( backEnd.viewParms.orientation.axis[ 2 ], upDir );
	}

	float scale = Math::inv_sqrt2_f;

	if ( backEnd.currentEntity->e.nonNormalizedAxes )
	{
		float axisLength = VectorLength( backEnd.currentEntity->e.axis[ 0 ] );

		if ( axisLength )
		{
			scale /= axisLength;
		}
	}

	for ( uint32_t i = 0; i < numVertexes; i += 4 )
	{
		const shaderVertex_t *v = tess.vertsBuffer + i;

		// find the midpoint
		vec3_t center;
		VectorAdd( v[ 0 ].xyz, v[ 1 ].xyz, center );
		VectorAdd( center, v[ 2 ].xyz, center );
		VectorAdd( center, v[ 3 ].xyz, center );
		VectorScale( center, 0.25f, center );

		vec3_t delta;
		VectorSubtract( v[ 0 ].xyz, center, delta );
		float radius = VectorLength( delta ) * scale;

		vec3_t left, up;
		VectorScale( leftDir, radius, left );
		VectorScale( upDir, radius, up );

		if ( backEnd.viewParms.mirrorLevel & 1 )
		{
			VectorNegate( left, left );
		}

		Tess_AddQuadStamp( center, left, up, v->color );
	}
}

/*
=====================
Autosprite2Deform

Autosprite2 will pivot a rectangular quad along the center of its long axis
=====================
*/
// Style 0 is what Tremulous did but style 1 generally looks better, even with Tremulous assets.
// Style 0 looks stupid because you can see the sprite rotating if you stand still and move the
// mouse. Style 1 does a better job for making something look cylindrical, like the "pillar of flame"
// suggested in the Q3 manual. Either one will look bad beyond the ends of the long axis.
static Cvar::Range<Cvar::Cvar<int>> r_autosprite2Style(
	"r_autosprite2Style", "display autosprite2 surfaces facing (0) in view direction or (1) toward viewer",
	Cvar::NONE, 1, 0, 1);
static void Autosprite2Deform( uint32_t numVertexes )
{
	tess.numVertexes = numVertexes;
	tess.numIndexes = ( numVertexes >> 2 ) * 6;
	std::copy_n( tess.indexesBuffer, tess.numIndexes, tess.indexes );

	// this is a lot of work for two triangles...
	// we could precalculate a lot of it is an issue, but it would mess up
	// the shader abstraction
	for ( uint32_t i = 0, indexes = 0; i < tess.numVertexes; i += 4, indexes += 6 )
	{
		struct TriSide {
			vec3_t firstVert;
			float lengthSq;
			vec3_t vector; // second point minus first point
		};

		TriSide sides[ 3 ];
		VectorCopy( tess.vertsBuffer[ tess.indexesBuffer[ indexes + 0 ] ].xyz, sides[ 0 ].firstVert );
		VectorCopy( tess.vertsBuffer[ tess.indexesBuffer[ indexes + 1 ] ].xyz, sides[ 1 ].firstVert );
		VectorCopy( tess.vertsBuffer[ tess.indexesBuffer[ indexes + 2 ] ].xyz, sides[ 2 ].firstVert );

		for ( int j = 0; j < 3; j++ )
		{
			VectorSubtract( sides[ (j + 1) % 3 ].firstVert, sides[ j ].firstVert, sides[ j ].vector );
			sides[ j ].lengthSq = VectorLengthSquared( sides[ j ].vector );
		}

		std::sort( std::begin( sides ), std::end( sides ),
		           []( TriSide &a, TriSide &b ) { return a.lengthSq < b.lengthSq; } );
		// Now sides[ 0 ] should be a short side of the rectangle, sides[ 1 ] a long side,
		// and sides[ 2 ] a diagonal

		vec3_t forward;
		if ( backEnd.currentEntity != &tr.worldEntity )
		{
			// FIXME: implement style 1 here
			GlobalVectorToLocal( backEnd.viewParms.orientation.axis[ 0 ], forward );
		}
		else if ( r_autosprite2Style.Get() == 0 )
		{
			VectorCopy( backEnd.viewParms.orientation.axis[ 0 ], forward );
		}
		else
		{
			vec3_t quadCenter;
			VectorMA( sides[ 2 ].firstVert, 0.5f, sides[ 2 ].vector, quadCenter );
			VectorSubtract( quadCenter, backEnd.viewParms.orientation.origin, forward );
			VectorNormalize( forward );
		}

		vec3_t newMinorAxis;
		CrossProduct( sides[ 1 ].vector, forward, newMinorAxis);
		VectorNormalize( newMinorAxis );
		plane_t projection;
		VectorNormalize2( sides[ 0 ].vector, projection.normal );
		projection.dist = DotProduct( sides[ 0 ].firstVert, projection.normal )
		                  + 0.5f * sqrtf( sides[ 0 ].lengthSq );
		vec3_t minorAxisReplace;
		VectorSubtract( newMinorAxis, projection.normal, minorAxisReplace );

		if ( tess.skipTangents )
		{
			for ( uint32_t j = i; j <= i + 4; j++ )
			{
				shaderVertex_t v = tess.vertsBuffer[ j ];
				float d = DotProduct( projection.normal, v.xyz ) - projection.dist;
				VectorMA( v.xyz, d, minorAxisReplace, v.xyz );
				tess.verts[ j ] = v;
			}
		}
		else
		{
			i16vec4_t qtangents;
			vec3_t normal;
			CrossProduct( newMinorAxis, sides[ 1 ].vector, normal );
			if ( DotProduct( normal, forward ) > 0 )
			{
				VectorNegate( normal, normal );
			}
			VectorNormalize( normal );
			// What the fuck are tangent and binormal even for?
			// I'll just put in zeroes and let R_TBNtoQtangents make some up for me.
			R_TBNtoQtangents( vec3_origin, vec3_origin, normal, qtangents );

			for ( uint32_t j = i; j <= i + 4; j++ )
			{
				shaderVertex_t v = tess.vertsBuffer[ j ];
				float d = DotProduct( projection.normal, v.xyz ) - projection.dist;
				VectorMA( v.xyz, d, minorAxisReplace, v.xyz );
				Vector4Copy( qtangents, v.qtangents );
				tess.verts[ j ] = v;
			}
		}
	}
}

/*
=====================
Tess_AutospriteDeform

=====================
*/
void Tess_AutospriteDeform( int mode )
{
	if ( tess.verts != tess.vertsBuffer )
	{
		Log::Warn( "Tess_AutospriteDeform: CPU vertex buffer not active" );
		return;
	}

	uint32_t numVertexes = tess.numVertexes;
	uint32_t numIndexes = tess.numIndexes;

	// Tess_MapVBOs( true ) should have been called previously. Now we take the original verts from
	// the CPU-only buffer and write the rotated verts to the shared GPU buffer. (If the GPU buffer
	// is not supported, the source and dest buffers are the same.)
	Tess_Clear();
	Tess_MapVBOs( false );

	if ( numVertexes & 3 )
	{
		Log::Warn( "Autosprite shader %s had odd vertex count", tess.surfaceShader->name );
		return; // drop vertexes
	}

	if ( numIndexes != ( numVertexes >> 2 ) * 6 )
	{
		Log::Warn( "Autosprite shader %s had odd index count", tess.surfaceShader->name );
		return; // drop vertexes
	}

	switch( mode ) {
	case 1:
		AutospriteDeform( numVertexes );
		break;
	case 2:
		Autosprite2Deform( numVertexes );
		break;
	default:
		ASSERT_UNREACHABLE();
	}
}

/*
====================================================================

TEX COORDS

====================================================================
*/

/*
===============
RB_CalcTexMatrix
===============
*/
void RB_CalcTexMatrix( const textureBundle_t *bundle, matrix_t matrix )
{

	MatrixIdentity( matrix );

	texModInfo_t *texMod = bundle->texMods;
	texModInfo_t *lastTexMod = texMod + bundle->numTexMods;

	for ( ; texMod < lastTexMod; texMod++ )
	{
		switch ( texMod->type )
		{
			case texMod_t::TMOD_NONE:
				texMod = lastTexMod; // break out of for loop
				break;

			case texMod_t::TMOD_TURBULENT:
				{
					waveForm_t *wf = &texMod->wave;

					float x = ( 1.0f / 4.0f );
					float y = ( wf->phase + backEnd.refdef.floatTime * wf->frequency );

					MatrixMultiplyScale( matrix, 1.0f + ( wf->amplitude * sinf( y ) + wf->base ) * x,
					                     1.0f + ( wf->amplitude * sinf( y + 0.25f ) + wf->base ) * x, 0.0 );
					break;
				}

			case texMod_t::TMOD_ENTITY_TRANSLATE:
				{
					float x = backEnd.currentEntity->e.shaderTexCoord[ 0 ] * backEnd.refdef.floatTime;
					float y = backEnd.currentEntity->e.shaderTexCoord[ 1 ] * backEnd.refdef.floatTime;

					// clamp so coordinates don't continuously get larger, causing problems
					// with hardware limits
					x = x - floor( x );
					y = y - floor( y );

					MatrixMultiplyTranslation( matrix, x, y, 0.0 );
					break;
				}

			case texMod_t::TMOD_SCROLL:
				{
					float x = texMod->scroll[ 0 ] * backEnd.refdef.floatTime;
					float y = texMod->scroll[ 1 ] * backEnd.refdef.floatTime;

					// clamp so coordinates don't continuously get larger, causing problems
					// with hardware limits
					x = x - floor( x );
					y = y - floor( y );

					MatrixMultiplyTranslation( matrix, x, y, 0.0 );
					break;
				}

			case texMod_t::TMOD_SCALE:
				{
					float x = texMod->scale[ 0 ];
					float y = texMod->scale[ 1 ];

					MatrixMultiplyScale( matrix, x, y, 0.0 );
					break;
				}

			case texMod_t::TMOD_STRETCH:
				{
					float p = 1.0f / RB_EvalWaveForm( &texMod->wave );

					MatrixMultiplyTranslation( matrix, 0.5, 0.5, 0.0 );
					MatrixMultiplyScale( matrix, p, p, 0.0 );
					MatrixMultiplyTranslation( matrix, -0.5, -0.5, 0.0 );
					break;
				}

			case texMod_t::TMOD_TRANSFORM:
				{
					MatrixMultiply2( matrix, texMod->matrix );
					break;
				}

			case texMod_t::TMOD_ROTATE:
				{
					float x = -texMod->rotateSpeed * backEnd.refdef.floatTime;

					MatrixMultiplyTranslation( matrix, 0.5, 0.5, 0.0 );
					MatrixMultiplyZRotation( matrix, x );
					MatrixMultiplyTranslation( matrix, -0.5, -0.5, 0.0 );
					break;
				}

			case texMod_t::TMOD_SCROLL2:
				{
					float x = RB_EvalExpression( &texMod->sExp, 0 );
					float y = RB_EvalExpression( &texMod->tExp, 0 );

					// clamp so coordinates don't continuously get larger, causing problems
					// with hardware limits
					x = x - floor( x );
					y = y - floor( y );

					MatrixMultiplyTranslation( matrix, x, y, 0.0 );
					break;
				}

			case texMod_t::TMOD_SCALE2:
				{
					float x = RB_EvalExpression( &texMod->sExp, 0 );
					float y = RB_EvalExpression( &texMod->tExp, 0 );

					MatrixMultiplyScale( matrix, x, y, 0.0 );
					break;
				}

			case texMod_t::TMOD_CENTERSCALE:
				{
					float x = RB_EvalExpression( &texMod->sExp, 0 );
					float y = RB_EvalExpression( &texMod->tExp, 0 );

					MatrixMultiplyTranslation( matrix, 0.5, 0.5, 0.0 );
					MatrixMultiplyScale( matrix, x, y, 0.0 );
					MatrixMultiplyTranslation( matrix, -0.5, -0.5, 0.0 );
					break;
				}

			case texMod_t::TMOD_SHEAR:
				{
					float x = RB_EvalExpression( &texMod->sExp, 0 );
					float y = RB_EvalExpression( &texMod->tExp, 0 );

					MatrixMultiplyTranslation( matrix, 0.5, 0.5, 0.0 );
					MatrixMultiplyShear( matrix, x, y );
					MatrixMultiplyTranslation( matrix, -0.5, -0.5, 0.0 );
					break;
				}

			case texMod_t::TMOD_ROTATE2:
				{
					float x = RB_EvalExpression( &texMod->rExp, 0 );

					MatrixMultiplyTranslation( matrix, 0.5, 0.5, 0.0 );
					MatrixMultiplyZRotation( matrix, x );
					MatrixMultiplyTranslation( matrix, -0.5, -0.5, 0.0 );
					break;
				}

			default:
				break;
		}
	}
}
