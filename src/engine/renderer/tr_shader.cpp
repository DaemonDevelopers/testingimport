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
// tr_shader.c -- this file deals with the parsing and definition of shaders
#include "tr_local.h"
#include "gl_shader.h"
#include "framework/CvarSystem.h"
#include "Material.h"
#include <iomanip>

static const int MAX_SHADERTABLE_HASH = 1024;
static shaderTable_t *shaderTableHashTable[ MAX_SHADERTABLE_HASH ];

static const int FILE_HASH_SIZE       = 1024;
static shader_t      *shaderHashTable[ FILE_HASH_SIZE ];

static const int MAX_SHADERTEXT_HASH  = 2048;
static const char          **shaderTextHashTable[ MAX_SHADERTEXT_HASH ];

static char          *s_shaderText;

// the shader is parsed into these global variables, then copied into
// dynamically allocated memory if it is valid.
static shaderTable_t table;
static std::array<shaderStage_t, MAX_SHADER_STAGES> stages;
static size_t numStages;
static shader_t      shader;
static texModInfo_t  texMods[ MAX_SHADER_STAGES ][ TR_MAX_TEXMODS ];

// ydnar: these are here because they are only referenced while parsing a shader
static char          implicitMap[ MAX_QPATH ];
static unsigned      implicitStateBits;
static cullType_t    implicitCullType;

static char          whenTokens[ MAX_STRING_CHARS ];

/* This parser does not assume any default normal map format.

Syntax variations must define a default normal map format.

Stage keywords like normalMap, bumpMap and normalHeightMap set
normal format to +X -Y +Z like DirectX, to keep those keywords
compatible with other idTech3 engines using those keywords.

XreaL implemented normalMap like Doom3 which is an OpenGL engine
using DirectX normal format convention. Then other renderers like
Daemon, ioquake3 renderer2, OpenJK, ET:Legacy, etc. did like XreaL.

It's possible to extend this parser with other stage keywords
from other engines to load normal map with their respective format.

The normalFormat stage keyword can be used in materials to set an
arbitrary format. */
static const int8_t glNormalFormat[ 3 ] = { 1,  1, 1 };
static const int8_t dxNormalFormat[ 3 ] = { 1, -1, 1 };

// DarkPlaces material compatibility
static Cvar::Cvar<bool> r_dpMaterial("r_dpMaterial", "Enable DarkPlaces material compatibility", Cvar::NONE, false);
Cvar::Cvar<bool> r_dpBlend("r_dpBlend", "Enable DarkPlaces blend compatibility, process GT0 as GE128", Cvar::NONE, false);

/* Quake3 used 256 as default portal range. */
static Cvar::Cvar<float> r_portalDefaultRange(
	"r_portalDefaultRange", "Default portal range", Cvar::NONE, 1024);

// This can be turned off to debug problems with depth shaders.
// Almost everything can be rendered correctly without them, except real-time lights
// (since light tiles are populated using the depth buffer).
static Cvar::Cvar<bool> r_depthShaders(
	"r_depthShaders", "use depth pre-pass shaders", Cvar::CHEAT, true);

/*
================
return a hash value for the filename
================
*/
static unsigned int generateHashValue( const char *fname, const int size )
{
	int  i;

	unsigned hash;
	char letter;

	hash = 0;
	i = 0;

	while ( fname[ i ] != '\0' )
	{
		letter = Str::ctolower( fname[ i ] );

		if ( letter == '.' )
		{
			break; // don't include extension
		}

		if ( letter == '\\' )
		{
			letter = '/'; // damn path names
		}

		if ( letter == PATH_SEP )
		{
			letter = '/'; // damn path names
		}

		hash += ( unsigned )( letter ) * ( i + 119 );
		i++;
	}

	hash = ( hash ^ ( hash >> 10 ) ^ ( hash >> 20 ) );
	hash &= ( size - 1 );
	return hash;
}

void R_RemapShader( const char *shaderName, const char *newShaderName, const char* )
{
	char      strippedName[ MAX_QPATH ];
	int       hash;
	shader_t  *sh, *sh2;
	qhandle_t h;

	sh = R_FindShaderByName( shaderName );

	if ( sh == nullptr || sh == tr.defaultShader )
	{
		h = RE_RegisterShader( shaderName, RSF_DEFAULT );
		sh = R_GetShaderByHandle( h );
	}

	if ( sh == nullptr || sh == tr.defaultShader )
	{
		Log::Warn("R_RemapShader: shader %s not found", shaderName );
		return;
	}

	sh2 = R_FindShaderByName( newShaderName );

	if ( sh2 == nullptr || sh2 == tr.defaultShader )
	{
		h = RE_RegisterShader( newShaderName, RSF_DEFAULT );
		sh2 = R_GetShaderByHandle( h );
	}

	if ( sh2 == nullptr || sh2 == tr.defaultShader )
	{
		Log::Warn("R_RemapShader: new shader %s not found", newShaderName );
		return;
	}

	if ( sh->autoSpriteMode != sh2->autoSpriteMode ) {
		Log::Warn("R_RemapShader: shaders %s and %s have different autoSprite modes", shaderName, newShaderName );
		return;
	}

	// remap all the shaders with the given name
	// even tho they might have different lightmaps
	COM_StripExtension3( shaderName, strippedName, sizeof( strippedName ) );
	hash = generateHashValue( strippedName, FILE_HASH_SIZE );

	for ( sh = shaderHashTable[ hash ]; sh; sh = sh->next )
	{
		if ( Q_stricmp( sh->name, strippedName ) == 0 )
		{
			if ( sh != sh2 )
			{
				sh->remappedShader = sh2;
			}
			else
			{
				sh->remappedShader = nullptr;
			}
		}
	}
}

/*
===============
ParseVector
===============
*/
static bool ParseVector( const char **text, int count, float *v )
{
	const char *token;
	int  i;

	token = COM_ParseExt2( text, false );

	if ( strcmp( token, "(" ) )
	{
		Log::Warn("missing parenthesis in shader '%s'", shader.name );
		return false;
	}

	for ( i = 0; i < count; i++ )
	{
		token = COM_ParseExt2( text, false );

		if ( !token[ 0 ] )
		{
			Log::Warn("missing vector element in shader '%s'", shader.name );
			return false;
		}

		v[ i ] = atof( token );
	}

	token = COM_ParseExt2( text, false );

	if ( strcmp( token, ")" ) )
	{
		Log::Warn("missing parenthesis in shader '%s'", shader.name );
		return false;
	}

	return true;
}

const opstring_t opStrings[] = {
	{"bad",                opcode_t::OP_BAD},

	{"&&",                 opcode_t::OP_LAND},
	{"||",                 opcode_t::OP_LOR},
	{">=",                 opcode_t::OP_GE},
	{"<=",                 opcode_t::OP_LE},
	{"==",                 opcode_t::OP_LEQ},
	{"!=",                 opcode_t::OP_LNE},

	{"+",                  opcode_t::OP_ADD},
	{"-",                  opcode_t::OP_SUB},
	{"/",                  opcode_t::OP_DIV},
	{"%",                  opcode_t::OP_MOD},
	{"*",                  opcode_t::OP_MUL},
	{"neg",                opcode_t::OP_NEG},

	{"<",                  opcode_t::OP_LT},
	{">",                  opcode_t::OP_GT},

	{"(",                  opcode_t::OP_LPAREN},
	{")",                  opcode_t::OP_RPAREN},
	{"[",                  opcode_t::OP_LBRACKET},
	{"]",                  opcode_t::OP_RBRACKET},

	{"c",                  opcode_t::OP_NUM},
	{"time",               opcode_t::OP_TIME},
	{"parm0",              opcode_t::OP_PARM0},
	{"parm1",              opcode_t::OP_PARM1},
	{"parm2",              opcode_t::OP_PARM2},
	{"parm3",              opcode_t::OP_PARM3},
	{"parm4",              opcode_t::OP_PARM4},
	{"parm5",              opcode_t::OP_PARM5},
	{"parm6",              opcode_t::OP_PARM6},
	{"parm7",              opcode_t::OP_PARM7},
	{"parm8",              opcode_t::OP_PARM8},
	{"parm9",              opcode_t::OP_PARM9},
	{"parm10",             opcode_t::OP_PARM10},
	{"parm11",             opcode_t::OP_PARM11},
	{"global0",            opcode_t::OP_GLOBAL0},
	{"global1",            opcode_t::OP_GLOBAL1},
	{"global2",            opcode_t::OP_GLOBAL2},
	{"global3",            opcode_t::OP_GLOBAL3},
	{"global4",            opcode_t::OP_GLOBAL4},
	{"global5",            opcode_t::OP_GLOBAL5},
	{"global6",            opcode_t::OP_GLOBAL6},
	{"global7",            opcode_t::OP_GLOBAL7},
	{"fragmentShaders",    opcode_t::OP_FRAGMENTSHADERS},
	{"frameBufferObjects", opcode_t::OP_FRAMEBUFFEROBJECTS},
	{"sound",              opcode_t::OP_SOUND},
	{"distance",           opcode_t::OP_DISTANCE},

	{"table",              opcode_t::OP_TABLE},

	{nullptr,              opcode_t::OP_BAD},
};

const char* GetOpName(opcode_t type)
{
	return opStrings[ Util::ordinal(type) ].s;
}

static void GetOpType( char *token, expOperation_t *op )
{
	const opstring_t *opString;
	char          tableName[ MAX_QPATH ];
	int           hash;
	shaderTable_t *tb;

	if ( ( token[ 0 ] >= '0' && token[ 0 ] <= '9' ) ||
	     //(token[0] == '-' && token[1] >= '0' && token[1] <= '9')   ||
	     //(token[0] == '+' && token[1] >= '0' && token[1] <= '9')   ||
	     ( token[ 0 ] == '.' && token[ 1 ] >= '0' && token[ 1 ] <= '9' ) )
	{
		op->type = opcode_t::OP_NUM;
		return;
	}

	Q_strncpyz( tableName, token, sizeof( tableName ) );
	hash = generateHashValue( tableName, MAX_SHADERTABLE_HASH );

	for ( tb = shaderTableHashTable[ hash ]; tb; tb = tb->next )
	{
		if ( Q_stricmp( tb->name, tableName ) == 0 )
		{
			// match found
			op->type = opcode_t::OP_TABLE;
			op->value = tb->index;
			return;
		}
	}

	for ( opString = opStrings; opString->s; opString++ )
	{
		if ( !Q_stricmp( token, opString->s ) )
		{
			op->type = opString->type;
			return;
		}
	}

	op->type = opcode_t::OP_BAD;
}

static bool IsOperand( opcode_t oc )
{
	switch ( oc )
	{
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
			return true;

		default:
			return false;
	}
}

static bool IsOperator( opcode_t oc )
{
	switch ( oc )
	{
		case opcode_t::OP_LAND:
		case opcode_t::OP_LOR:
		case opcode_t::OP_GE:
		case opcode_t::OP_LE:
		case opcode_t::OP_LEQ:
		case opcode_t::OP_LNE:
		case opcode_t::OP_ADD:
		case opcode_t::OP_SUB:
		case opcode_t::OP_DIV:
		case opcode_t::OP_MOD:
		case opcode_t::OP_MUL:
		case opcode_t::OP_NEG:
		case opcode_t::OP_LT:
		case opcode_t::OP_GT:
		case opcode_t::OP_TABLE:
			return true;

		default:
			return false;
	}
}

static int GetOpPrecedence( opcode_t oc )
{
	switch ( oc )
	{
		case opcode_t::OP_LOR:
			return 1;

		case opcode_t::OP_LAND:
			return 2;

		case opcode_t::OP_LEQ:
		case opcode_t::OP_LNE:
			return 3;

		case opcode_t::OP_GE:
		case opcode_t::OP_LE:
		case opcode_t::OP_LT:
		case opcode_t::OP_GT:
			return 4;

		case opcode_t::OP_ADD:
		case opcode_t::OP_SUB:
			return 5;

		case opcode_t::OP_DIV:
		case opcode_t::OP_MOD:
		case opcode_t::OP_MUL:
			return 6;

		case opcode_t::OP_NEG:
			return 7;

		case opcode_t::OP_TABLE:
			return 8;

		default:
			return 0;
	}
}

static char    *ParseExpressionElement( const char **data_p )
{
	int         c = 0, len;
	const char        *data;
	const char  *const *punc;
	static char token[ MAX_TOKEN_CHARS ];

	// multiple character punctuation tokens
	static const char *const punctuation[] =
	{
		"&&", "||", "<=", ">=", "==", "!=", nullptr
	};

	if ( !data_p )
	{
		Sys::Error( "ParseExpressionElement: NULL data_p" );
	}

	data = *data_p;
	len = 0;
	token[ 0 ] = 0;

	// make sure incoming data is valid
	if ( !data )
	{
		*data_p = nullptr;
		return token;
	}

	// skip whitespace
	while ( true )
	{
		// skip whitespace
		while ( ( c = *data ) <= ' ' )
		{
			if ( !c )
			{
				*data_p = nullptr;
				return token;
			}
			else if ( c == '\n' )
			{
				data++;
				*data_p = data;
				return token;
			}
			else
			{
				data++;
			}
		}

		c = *data;

		// skip double slash comments
		if ( c == '/' && data[ 1 ] == '/' )
		{
			data += 2;

			while ( *data && *data != '\n' )
			{
				data++;
			}
		}
		// skip /* */ comments
		else if ( c == '/' && data[ 1 ] == '*' )
		{
			data += 2;

			while ( *data && ( *data != '*' || data[ 1 ] != '/' ) )
			{
				data++;
			}

			if ( *data )
			{
				data += 2;
			}
		}
		else
		{
			// a real token to parse
			break;
		}
	}

	// handle quoted strings
	if ( c == '\"' )
	{
		data++;

		while ( true )
		{
			c = *data++;

			if ( ( c == '\\' ) && ( *data == '\"' ) )
			{
				// allow quoted strings to use \" to indicate the " character
				data++;
			}
			else if ( c == '\"' || !c )
			{
				token[ len ] = 0;
				*data_p = ( char * ) data;
				return token;
			}

			if ( len < MAX_TOKEN_CHARS - 1 )
			{
				token[ len ] = c;
				len++;
			}
		}
	}

	// check for a number
	if ( ( c >= '0' && c <= '9' ) ||
	     ( c == '.' && data[ 1 ] >= '0' && data[ 1 ] <= '9' ) )
	{
		do
		{
			if ( len < MAX_TOKEN_CHARS - 1 )
			{
				token[ len ] = c;
				len++;
			}

			data++;

			c = *data;
		}
		while ( ( c >= '0' && c <= '9' ) || c == '.' );

		// parse the exponent
		if ( c == 'e' || c == 'E' )
		{
			if ( len < MAX_TOKEN_CHARS - 1 )
			{
				token[ len ] = c;
				len++;
			}

			data++;
			c = *data;

			if ( c == '-' || c == '+' )
			{
				if ( len < MAX_TOKEN_CHARS - 1 )
				{
					token[ len ] = c;
					len++;
				}

				data++;
				c = *data;
			}

			do
			{
				if ( len < MAX_TOKEN_CHARS - 1 )
				{
					token[ len ] = c;
					len++;
				}

				data++;

				c = *data;
			}
			while ( c >= '0' && c <= '9' );
		}

		if ( len == MAX_TOKEN_CHARS )
		{
			len = 0;
		}

		token[ len ] = 0;

		*data_p = ( char * ) data;
		return token;
	}

	// check for a regular word
	if ( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c == '_' ) )
	{
		do
		{
			if ( len < MAX_TOKEN_CHARS - 1 )
			{
				token[ len ] = c;
				len++;
			}

			data++;

			c = *data;
		}
		while ( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c == '_' ) || ( c >= '0' && c <= '9' ) );

		if ( len == MAX_TOKEN_CHARS )
		{
			len = 0;
		}

		token[ len ] = 0;

		*data_p = ( char * ) data;
		return token;
	}

	// check for multi-character punctuation token
	for ( punc = punctuation; *punc; punc++ )
	{
		int l;
		int j;

		l = strlen( *punc );

		for ( j = 0; j < l; j++ )
		{
			if ( data[ j ] != ( *punc ) [ j ] )
			{
				break;
			}
		}

		if ( j == l )
		{
			// a valid multi-character punctuation
			memcpy( token, *punc, l );
			token[ l ] = 0;
			data += l;
			*data_p = ( char * ) data;
			return token;
		}
	}

	// single character punctuation
	token[ 0 ] = *data;
	token[ 1 ] = 0;
	data++;
	*data_p = ( char * ) data;

	return token;
}

/*
===============
ParseExpression
===============
*/
static void ParseExpression( const char **text, expression_t *exp )
{
	expOperation_t op, op2;

	expOperation_t inFixOps[ MAX_EXPRESSION_OPS ];
	size_t numInFixOps = 0;

	// convert stack
	expOperation_t tmpOps[ MAX_EXPRESSION_OPS ];
	size_t numTmpOps = 0;

	// A ext->numOps equals to 0 means empty or invalid expression.
	exp->numOps = 0;

	// The numOps will only be written to exp->numOps if there is no parsing error.
	size_t numOps = 0;

	// push left parenthesis on the stack
	op.type = opcode_t::OP_LPAREN;
	op.value = 0;
	inFixOps[ numInFixOps++ ] = op;

	while ( true )
	{
		char *token = ParseExpressionElement( text );

		if ( token[ 0 ] == 0 || token[ 0 ] == ',' )
		{
			break;
		}

		if ( numInFixOps == MAX_EXPRESSION_OPS )
		{
			Log::Warn("too many arithmetic expression operations in shader '%s'", shader.name );
			SkipRestOfLine( text );
			return;
		}

		GetOpType( token, &op );

		switch ( op.type )
		{
			case opcode_t::OP_BAD:
				Log::Warn("unknown token '%s' for arithmetic expression in shader '%s'", token,
				           shader.name );
				break;

			case opcode_t::OP_LBRACKET:
				inFixOps[ numInFixOps++ ] = op;

				// add extra (
				op2.type = opcode_t::OP_LPAREN;
				op2.value = 0;
				inFixOps[ numInFixOps++ ] = op2;
				break;

			case opcode_t::OP_RBRACKET:
				// add extra )
				op2.type = opcode_t::OP_RPAREN;
				op2.value = 0;
				inFixOps[ numInFixOps++ ] = op2;

				inFixOps[ numInFixOps++ ] = op;
				break;

			case opcode_t::OP_NUM:
				op.value = atof( token );
				inFixOps[ numInFixOps++ ] = op;
				break;

			case opcode_t::OP_TABLE:
				// value already set by GetOpType
				inFixOps[ numInFixOps++ ] = op;
				break;

			default:
				op.value = 0;
				inFixOps[ numInFixOps++ ] = op;
				break;
		}
	}

	// push right parenthesis on the stack
	op.type = opcode_t::OP_RPAREN;
	op.value = 0;
	inFixOps[ numInFixOps++ ] = op;

	for ( size_t i = 0; i < ( numInFixOps - 1 ); i++ )
	{
		op = inFixOps[ i ];
		op2 = inFixOps[ i + 1 ];

		// convert OP_SUBs that should be unary into OP_NEG
		if ( op2.type == opcode_t::OP_SUB && op.type != opcode_t::OP_RPAREN && op.type != opcode_t::OP_TABLE && !IsOperand( op.type ) )
		{
			inFixOps[ i + 1 ].type = opcode_t::OP_NEG;
		}
	}

	// http://cis.stvincent.edu/swd/stl/stacks/stacks.html
	// http://www.qiksearch.com/articles/cs/infix-postfix/
	// http://www.experts-exchange.com/Programming/Programming_Languages/C/Q_20394130.html

	//
	// convert infix representation to postfix
	//

	for ( size_t i = 0; i < numInFixOps; i++ )
	{
		op = inFixOps[ i ];

		// if current operator in infix is digit
		if ( IsOperand( op.type ) )
		{
			exp->ops[ numOps++ ] = op;
		}
		// if current operator in infix is left parenthesis
		else if ( op.type == opcode_t::OP_LPAREN )
		{
			tmpOps[ numTmpOps++ ] = op;
		}
		// if current operator in infix is operator
		else if ( IsOperator( op.type ) )
		{
			while ( true )
			{
				if ( !numTmpOps )
				{
					Log::Warn("invalid infix expression in shader '%s'", shader.name );
					return;
				}
				else
				{
					// get top element
					op2 = tmpOps[ numTmpOps - 1 ];

					if ( IsOperator( op2.type ) )
					{
						if ( GetOpPrecedence( op2.type ) >= GetOpPrecedence( op.type ) )
						{
							exp->ops[ numOps++ ] = op2;
							numTmpOps--;
						}
						else
						{
							break;
						}
					}
					else
					{
						break;
					}
				}
			}

			tmpOps[ numTmpOps++ ] = op;
		}
		// if current operator in infix is right parenthesis
		else if ( op.type == opcode_t::OP_RPAREN )
		{
			while ( true )
			{
				if ( !numTmpOps )
				{
					Log::Warn("invalid infix expression in shader '%s'", shader.name );
					return;
				}
				else
				{
					// get top element
					op2 = tmpOps[ numTmpOps - 1 ];

					if ( op2.type != opcode_t::OP_LPAREN )
					{
						exp->ops[ numOps++ ] = op2;
						numTmpOps--;
					}
					else
					{
						numTmpOps--;
						break;
					}
				}
			}
		}
	}

	// everything went ok
	exp->numOps = numOps;
}

/*
===============
NameToAFunc
===============
*/
static unsigned NameToAFunc( const char *funcname )
{
	if ( !Q_stricmp( funcname, "GT0" ) )
	{
		if ( r_dpBlend.Get() )
		{
			// DarkPlaces only supports one alphaFunc operation: GE128
			Log::Warn("alphaFunc 'GT0' will be replaced by 'GE128' in shader '%s' because r_dpBlend compatibility layer is enabled", shader.name );
			return GLS_ATEST_GE_128;
		}
		else
		{
			if ( r_dpMaterial.Get() )
			{
				Log::Warn("alphaFunc 'GT0' will not be replaced by 'GE128' in shader '%s' because r_dpBlend compatibility layer is disabled", shader.name );
			}
			return GLS_ATEST_GT_0;
		}
	}
	else if ( !Q_stricmp( funcname, "LT128" ) )
	{
		return GLS_ATEST_LT_128;
	}
	else if ( !Q_stricmp( funcname, "GE128" ) )
	{
		return GLS_ATEST_GE_128;
	}
	else if ( !Q_stricmp( funcname, "LTENT" ) )
	{
		return GLS_ATEST_LT_ENT;
	}
	else if ( !Q_stricmp( funcname, "GTENT" ) )
	{
		return GLS_ATEST_GT_ENT;
	}

	Log::Warn("invalid alphaFunc name '%s' in shader '%s'", funcname, shader.name );
	return 0;
}

/*
===============
NameToSrcBlendMode
===============
*/
static int NameToSrcBlendMode( const char *name )
{
	if ( !Q_stricmp( name, "GL_ONE" ) )
	{
		return GLS_SRCBLEND_ONE;
	}
	else if ( !Q_stricmp( name, "GL_ZERO" ) )
	{
		return GLS_SRCBLEND_ZERO;
	}
	else if ( !Q_stricmp( name, "GL_DST_COLOR" ) )
	{
		return GLS_SRCBLEND_DST_COLOR;
	}
	else if ( !Q_stricmp( name, "GL_ONE_MINUS_DST_COLOR" ) )
	{
		return GLS_SRCBLEND_ONE_MINUS_DST_COLOR;
	}
	else if ( !Q_stricmp( name, "GL_SRC_ALPHA" ) )
	{
		return GLS_SRCBLEND_SRC_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_ONE_MINUS_SRC_ALPHA" ) )
	{
		return GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_DST_ALPHA" ) )
	{
		return GLS_SRCBLEND_DST_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_ONE_MINUS_DST_ALPHA" ) )
	{
		return GLS_SRCBLEND_ONE_MINUS_DST_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_SRC_ALPHA_SATURATE" ) )
	{
		return GLS_SRCBLEND_ALPHA_SATURATE;
	}

	Log::Warn("unknown blend mode '%s' in shader '%s', substituting GL_ONE", name, shader.name );
	return GLS_SRCBLEND_ONE;
}

/*
===============
NameToDstBlendMode
===============
*/
static int NameToDstBlendMode( const char *name )
{
	if ( !Q_stricmp( name, "GL_ONE" ) )
	{
		return GLS_DSTBLEND_ONE;
	}
	else if ( !Q_stricmp( name, "GL_ZERO" ) )
	{
		return GLS_DSTBLEND_ZERO;
	}
	else if ( !Q_stricmp( name, "GL_SRC_ALPHA" ) )
	{
		return GLS_DSTBLEND_SRC_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_ONE_MINUS_SRC_ALPHA" ) )
	{
		return GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_DST_ALPHA" ) )
	{
		return GLS_DSTBLEND_DST_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_ONE_MINUS_DST_ALPHA" ) )
	{
		return GLS_DSTBLEND_ONE_MINUS_DST_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_SRC_COLOR" ) )
	{
		return GLS_DSTBLEND_SRC_COLOR;
	}
	else if ( !Q_stricmp( name, "GL_ONE_MINUS_SRC_COLOR" ) )
	{
		return GLS_DSTBLEND_ONE_MINUS_SRC_COLOR;
	}

	Log::Warn("unknown blend mode '%s' in shader '%s', substituting GL_ONE", name, shader.name );
	return GLS_DSTBLEND_ONE;
}

/*
===============
NameToGenFunc
===============
*/
static genFunc_t NameToGenFunc( const char *funcname )
{
	if ( !Q_stricmp( funcname, "sin" ) )
	{
		return genFunc_t::GF_SIN;
	}
	else if ( !Q_stricmp( funcname, "square" ) )
	{
		return genFunc_t::GF_SQUARE;
	}
	else if ( !Q_stricmp( funcname, "triangle" ) )
	{
		return genFunc_t::GF_TRIANGLE;
	}
	else if ( !Q_stricmp( funcname, "sawtooth" ) )
	{
		return genFunc_t::GF_SAWTOOTH;
	}
	else if ( !Q_stricmp( funcname, "inversesawtooth" ) )
	{
		return genFunc_t::GF_INVERSE_SAWTOOTH;
	}
	else if ( !Q_stricmp( funcname, "noise" ) )
	{
		return genFunc_t::GF_NOISE;
	}

	Log::Warn("invalid genfunc name '%s' in shader '%s'", funcname, shader.name );
	return genFunc_t::GF_SIN;
}

/*
===================
ParseWaveForm
===================
*/
static void ParseWaveForm( const char **text, waveForm_t *wave )
{
	const char *token;

	token = COM_ParseExt2( text, false );

	if ( token[ 0 ] == 0 )
	{
		Log::Warn("missing waveform parm in shader '%s'", shader.name );
		return;
	}

	wave->func = NameToGenFunc( token );

	// BASE, AMP, PHASE, FREQ
	token = COM_ParseExt2( text, false );

	if ( token[ 0 ] == 0 )
	{
		Log::Warn("missing waveform parm in shader '%s'", shader.name );
		return;
	}

	wave->base = atof( token );

	token = COM_ParseExt2( text, false );

	if ( token[ 0 ] == 0 )
	{
		Log::Warn("missing waveform parm in shader '%s'", shader.name );
		return;
	}

	wave->amplitude = atof( token );

	token = COM_ParseExt2( text, false );

	if ( token[ 0 ] == 0 )
	{
		Log::Warn("missing waveform parm in shader '%s'", shader.name );
		return;
	}

	wave->phase = atof( token );

	token = COM_ParseExt2( text, false );

	if ( token[ 0 ] == 0 )
	{
		Log::Warn("missing waveform parm in shader '%s'", shader.name );
		return;
	}

	wave->frequency = atof( token );
}

/*
===================
ParseTexMod
===================
*/
static bool ParseTexMod( const char **text, shaderStage_t *stage )
{
	const char   *token;
	texModInfo_t *tmi;

	if ( stage->bundle[ 0 ].numTexMods == TR_MAX_TEXMODS )
	{
		Sys::Drop( "ERROR: too many tcMod stages in shader '%s'", shader.name );
	}

	tmi = &stage->bundle[ 0 ].texMods[ stage->bundle[ 0 ].numTexMods ];
	stage->bundle[ 0 ].numTexMods++;

	token = COM_ParseExt2( text, false );

	// turb
	if ( !Q_stricmp( token, "turb" ) )
	{
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing tcMod turb parms in shader '%s'", shader.name );
			return false;
		}

		tmi->wave.base = atof( token );
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing tcMod turb in shader '%s'", shader.name );
			return false;
		}

		tmi->wave.amplitude = atof( token );
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing tcMod turb in shader '%s'", shader.name );
			return false;
		}

		tmi->wave.phase = atof( token );
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing tcMod turb in shader '%s'", shader.name );
			return false;
		}

		tmi->wave.frequency = atof( token );

		tmi->type = texMod_t::TMOD_TURBULENT;
	}
	// scale
	else if ( !Q_stricmp( token, "scale" ) )
	{
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing scale parms in shader '%s'", shader.name );
			return false;
		}

		tmi->scale[ 0 ] = atof( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing scale parms in shader '%s'", shader.name );
			return false;
		}

		tmi->scale[ 1 ] = atof( token );
		tmi->type = texMod_t::TMOD_SCALE;
	}
	// scroll
	else if ( !Q_stricmp( token, "scroll" ) )
	{
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing scale scroll parms in shader '%s'", shader.name );
			return false;
		}

		tmi->scroll[ 0 ] = atof( token );
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing scale scroll parms in shader '%s'", shader.name );
			return false;
		}

		tmi->scroll[ 1 ] = atof( token );
		tmi->type = texMod_t::TMOD_SCROLL;
	}
	// stretch
	else if ( !Q_stricmp( token, "stretch" ) )
	{
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing stretch parms in shader '%s'", shader.name );
			return false;
		}

		tmi->wave.func = NameToGenFunc( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing stretch parms in shader '%s'", shader.name );
			return false;
		}

		tmi->wave.base = atof( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing stretch parms in shader '%s'", shader.name );
			return false;
		}

		tmi->wave.amplitude = atof( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing stretch parms in shader '%s'", shader.name );
			return false;
		}

		tmi->wave.phase = atof( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing stretch parms in shader '%s'", shader.name );
			return false;
		}

		tmi->wave.frequency = atof( token );

		tmi->type = texMod_t::TMOD_STRETCH;
	}
	// transform
	else if ( !Q_stricmp( token, "transform" ) )
	{
		MatrixIdentity( tmi->matrix );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing transform parms in shader '%s'", shader.name );
			return false;
		}

		tmi->matrix[ 0 ] = atof( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing transform parms in shader '%s'", shader.name );
			return false;
		}

		tmi->matrix[ 1 ] = atof( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing transform parms in shader '%s'", shader.name );
			return false;
		}

		tmi->matrix[ 4 ] = atof( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing transform parms in shader '%s'", shader.name );
			return false;
		}

		tmi->matrix[ 5 ] = atof( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing transform parms in shader '%s'", shader.name );
			return false;
		}

		tmi->matrix[ 12 ] = atof( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing transform parms in shader '%s'", shader.name );
			return false;
		}

		tmi->matrix[ 13 ] = atof( token );

		tmi->type = texMod_t::TMOD_TRANSFORM;
	}
	// rotate
	else if ( !Q_stricmp( token, "rotate" ) )
	{
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing tcMod rotate parms in shader '%s'", shader.name );
			return false;
		}

		tmi->rotateSpeed = atof( token );
		tmi->type = texMod_t::TMOD_ROTATE;
	}
	// entityTranslate
	else if ( !Q_stricmp( token, "entityTranslate" ) )
	{
		tmi->type = texMod_t::TMOD_ENTITY_TRANSLATE;
	}
	else
	{
		Log::Warn("unknown tcMod '%s' in shader '%s'", token, shader.name );
		return false;
	}

	return true;
}

static bool ParseMap( const char **text, char *buffer, int bufferSize )
{
	int  len;

	// example
	// map textures/caves/tembrick1crum_local.tga

	while ( true )
	{
		const char *token = COM_ParseExt2( text, false );

		if ( !token[ 0 ] )
		{
			// end of line
			break;
		}

		Q_strcat( buffer, bufferSize, token );
		Q_strcat( buffer, bufferSize, " " );
	}

	if ( !buffer[ 0 ] )
	{
		Log::Warn("'map' missing parameter in shader '%s'", shader.name );
		return false;
	}

	len = strlen( buffer );
	buffer[ len - 1 ] = 0; // replace last ' ' with tailing zero

	return true;
}

static bool LoadMap( shaderStage_t *stage, const char *buffer, stageType_t type, const int bundleIndex = TB_COLORMAP )
{
	const char         *buffer_p = &buffer[ 0 ];

	if ( !buffer || !buffer[ 0 ] )
	{
		Log::Warn("NULL parameter for LoadMap in shader '%s'", shader.name );
		return false;
	}

	const char *token = COM_ParseExt2( &buffer_p, false );

	// NOTE: Normal map can ship height map in alpha channel.
	if ( ( type == stageType_t::ST_NORMALMAP && !glConfig2.normalMapping && !glConfig2.reliefMapping )
		|| ( type == stageType_t::ST_HEIGHTMAP && !glConfig2.reliefMapping )
		|| ( type == stageType_t::ST_SPECULARMAP && !glConfig2.specularMapping )
		|| ( type == stageType_t::ST_PHYSICALMAP && !glConfig2.physicalMapping )
		|| ( type == stageType_t::ST_GLOWMAP && !r_glowMapping->integer )
		|| ( type == stageType_t::ST_REFLECTIONMAP && !glConfig2.reflectionMappingAvailable ) )
	{
		return true;
	}

	if ( !Q_stricmp( token, "$whiteimage" ) || !Q_stricmp( token, "$white" ) || !Q_stricmp( token, "_white" ) ||
	     !Q_stricmp( token, "*white" ) )
	{
		stage->bundle[ bundleIndex ].image[ 0 ] = tr.whiteImage;
		return true;
	}
	else if ( !Q_stricmp( token, "$blackimage" ) || !Q_stricmp( token, "$black" ) || !Q_stricmp( token, "_black" ) ||
	          !Q_stricmp( token, "*black" ) )
	{
		stage->bundle[ bundleIndex ].image[ 0 ] = tr.blackImage;
		return true;
	}
	else if ( !Q_stricmp( token, "$flatimage" ) || !Q_stricmp( token, "$flat" ) || !Q_stricmp( token, "_flat" ) ||
	          !Q_stricmp( token, "*flat" ) )
	{
		stage->bundle[ bundleIndex ].image[ 0 ] = tr.flatImage;
		return true;
	}

	else if ( !Q_stricmp( token, "$lightmap" ) )
	{
		stage->type = stageType_t::ST_LIGHTMAP;
		return true;
	}

	/* Light styles are only compatible with light mapping as light styles
	are just extra light map stages with special effects.

	So we don't load extra light maps when light styles are not supported.

	The disablement of the stage is done in the ParseShader() function. */
	if ( ( tr.lightMode != lightMode_t::MAP || r_lightStyles->integer == 0 )
		&& stage->tcGen_Lightmap )
	{
		/* We don't return false because we properly parsed the stage
		and the shader is still valid. The stage will be disabled later.
		We set a dummy image to avoid this error message:
		> Shader xxx has a colormap stage with no image. */
		stage->bundle[ bundleIndex ].image[ 0 ] = tr.blackImage;
		return true;
	}

	imageParams_t imageParams = {};
	imageParams.minDimension = shader.imageMinDimension;
	imageParams.maxDimension = shader.imageMaxDimension;

	// determine image options
	if ( stage->overrideNoPicMip || shader.noPicMip || stage->highQuality || stage->forceHighQuality )
	{
		imageParams.bits |= IF_NOPICMIP;
	}

	if ( stage->fitScreen || shader.fitScreen )
	{
		imageParams.bits |= IF_FITSCREEN;
	}

	switch ( type )
	{
		case stageType_t::ST_NORMALMAP:
		case stageType_t::ST_HEATHAZEMAP:
		case stageType_t::ST_LIQUIDMAP:
			imageParams.bits |= IF_NORMALMAP;
			break;
		default:
			// silence warning for other types, we don't have to take care of them:
			//    warning: enumeration value ‘ST_GLOWMAP’ not handled in switch [-Wswitch]
			break;
	}

	if ( stage->stateBits & ( GLS_ATEST_BITS ) )
	{
		imageParams.bits |= IF_ALPHATEST; // FIXME: this is unused
	}

	if ( stage->overrideFilterType )
	{
		imageParams.filterType = stage->filterType;
	}
	else
	{
		imageParams.filterType = shader.filterType;
	}

	imageParams.wrapType = stage->overrideWrapType ? stage->wrapType : shader.wrapType;

	// try to load the image
	if ( stage->isCubeMap )
	{
		stage->bundle[ bundleIndex ].image[ 0 ] = R_FindCubeImage( buffer, imageParams );

		if ( !stage->bundle[ bundleIndex ].image[ 0 ] )
		{
			Log::Warn("R_FindCubeImage could not find image '%s' in shader '%s'", buffer, shader.name );
			return false;
		}
	}
	else
	{
		stage->bundle[ bundleIndex ].image[ 0 ] = R_FindImageFile( buffer, imageParams );

		if ( !stage->bundle[ bundleIndex ].image[ 0 ] )
		{
			Log::Warn("R_FindImageFile could not find image '%s' in shader '%s'", buffer, shader.name );
			return false;
		}
	}

	return true;
}

/*
===================
ParseClampType
===================
*/
static bool ParseClampType( const char *token, wrapType_t *clamp )
{
	bool s = true, t = true;
	wrapTypeEnum_t type;

	// handle prefixing with 'S' or 'T'
	switch ( token[ 0 ] & 0xDF )
	{
	case 'S': t = false; ++token; break;
	case 'T': s = false; ++token; break;
	}

	// get the clamp type
	if      ( !Q_stricmp( token, "clamp"          ) ) { type = wrapTypeEnum_t::WT_CLAMP; }
	else if ( !Q_stricmp( token, "edgeClamp"      ) ) { type = wrapTypeEnum_t::WT_EDGE_CLAMP; }
	else if ( !Q_stricmp( token, "zeroClamp"      ) ) { type = wrapTypeEnum_t::WT_ZERO_CLAMP; }
	else if ( !Q_stricmp( token, "alphaZeroClamp" ) ) { type = wrapTypeEnum_t::WT_ALPHA_ZERO_CLAMP; }
	else if ( !Q_stricmp( token, "noClamp"        ) ) { type = wrapTypeEnum_t::WT_REPEAT; }
	else // not recognised
	{
		return false;
	}

	if (s) { clamp->s = type; }
	if (t) { clamp->t = type; }

	return true;
}

/*
===================
ParseDifuseMap
and others
===================
*/
static void ParseDiffuseMap( shaderStage_t *stage, const char **text, const int bundleIndex = TB_DIFFUSEMAP )
{
	char buffer[ 1024 ] = "";

	if ( ParseMap( text, buffer, sizeof( buffer ) ) )
	{
		LoadMap( stage, buffer, stageType_t::ST_DIFFUSEMAP, bundleIndex );
	}
}

static void ParseLegacyDiffuseStage( shaderStage_t *stage, const char **text )
{
	stage->active = true;
	stage->type = stageType_t::ST_DIFFUSEMAP;
	stage->rgbGen = colorGen_t::CGEN_IDENTITY;
	stage->stateBits = GLS_DEFAULT;

	ParseDiffuseMap( stage, text, TB_COLORMAP );
}

static void ParseNormalMap( shaderStage_t *stage, const char **text, const int bundleIndex = TB_NORMALMAP )
{
	char buffer[ 1024 ] = "";

	// because of collapsing, this affects other textures (diffuse, specular…) too
	if ( r_highQualityNormalMapping->integer )
	{
		stage->overrideFilterType = true;
		stage->filterType = filterType_t::FT_LINEAR;

		stage->overrideNoPicMip = true;
	}

	if ( ParseMap( text, buffer, sizeof( buffer ) ) )
	{
		LoadMap( stage, buffer, stageType_t::ST_NORMALMAP, bundleIndex );
	}
}

static void ParseLegacyNormalStage( shaderStage_t *stage, const char **text )
{
	stage->active = true;
	stage->type = stageType_t::ST_NORMALMAP;
	stage->rgbGen = colorGen_t::CGEN_IDENTITY;
	stage->stateBits = GLS_DEFAULT;

	ParseNormalMap( stage, text, TB_COLORMAP );
}

static void ParseNormalMapDetectHeightMap( shaderStage_t *stage, const char **text, const int bundleIndex = TB_NORMALMAP )
{
	/* Always call this function on assets known to never use RGTC format
	or the engine running on hardware with driver not implementing the
	GL_ARB_texture_compression_rgtc extension will wrongly assume a normal
	component stored in alpha channel is an heightmap and the renderer will
	select the wrong GLSL code for it, resulting in serious graphical issues.

	See https://github.com/DaemonEngine/Daemon/issues/375

	This code is meant to be used as compatibility code for games known
	to not use RGTC format and to requires alpha channel detection,
	like Xonotic/Darkplaces.

	Please do not implement material keyword for this function, never. */

	const char* initialText = *text;

	ParseNormalMap( stage, text, bundleIndex );

	/* Tell renderer to enable relief mapping since an heightmap is found,
	also tell renderer to not abuse normalmap alpha channel because it's an heightmap.

	See https://github.com/DaemonEngine/Daemon/issues/183#issuecomment-473691252 */

	if ( stage->bundle[ bundleIndex ].image[ 0 ]
		&& stage->bundle[ bundleIndex ].image[ 0 ]->bits & IF_NORMALMAP
		&& stage->bundle[ bundleIndex ].image[ 0 ]->bits & IF_ALPHA )
	{
		Log::defaultLogger.DoDebugCode([&] {
			char buffer[ 1024 ];
			buffer[ 0 ] = '\0';
			if ( !ParseMap( &initialText, buffer, sizeof( buffer ) ) )
			{
				ASSERT( false );
			}
			Log::Debug("Found heightmap embedded in normalmap '%s'", buffer);
		});

		stage->hasHeightMapInNormalMap = true;
	}
}

// There is no ParseLegacyNormalStageDetectHeightMap.

static void ParseHeightMap( shaderStage_t *stage, const char **text, const int bundleIndex = TB_HEIGHTMAP )
{
	char buffer[ 1024 ] = "";

	if ( ParseMap( text, buffer, sizeof( buffer ) ) )
	{
		LoadMap( stage, buffer, stageType_t::ST_HEIGHTMAP, bundleIndex );
	}
}

// There is no ParseLegacyHeightStage.

static void ParseSpecularMap( shaderStage_t *stage, const char **text, const int bundleIndex = TB_SPECULARMAP )
{
	char buffer[ 1024 ] = "";

	if ( ParseMap( text, buffer, sizeof( buffer ) ) )
	{
		LoadMap( stage, buffer, stageType_t::ST_SPECULARMAP, bundleIndex );
	}
}

static void ParseLegacySpecularStage( shaderStage_t *stage, const char **text )
{
	stage->active = true;
	stage->type = stageType_t::ST_SPECULARMAP;
	stage->rgbGen = colorGen_t::CGEN_IDENTITY;
	stage->stateBits = GLS_DEFAULT;

	ParseSpecularMap( stage, text, TB_COLORMAP );
}

static void ParsePhysicalMap( shaderStage_t *stage, const char **text, const int bundleIndex = TB_PHYSICALMAP )
{
	char buffer[ 1024 ] = "";

	stage->active = true;
	stage->rgbGen = colorGen_t::CGEN_IDENTITY;
	stage->stateBits = GLS_DEFAULT;

	if ( ParseMap( text, buffer, sizeof( buffer ) ) )
	{
		LoadMap( stage, buffer, stageType_t::ST_PHYSICALMAP, bundleIndex );
	}
}

// There is no ParseLegacyPhysicalStage.

static void ParseGlowMap( shaderStage_t *stage, const char **text, const int bundleIndex = TB_GLOWMAP )
{
	char buffer[ 1024 ] = "";

	if ( ParseMap( text, buffer, sizeof( buffer ) ) )
	{
		LoadMap( stage, buffer, stageType_t::ST_GLOWMAP, bundleIndex );
	}
}

static void ParseLegacyGlowStage( shaderStage_t *stage, const char **text )
{
	stage->active = true;
	stage->type = stageType_t::ST_GLOWMAP;
	stage->rgbGen = colorGen_t::CGEN_IDENTITY;
	stage->stateBits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE; // blend add

	ParseGlowMap( stage, text, TB_COLORMAP );
}

static void ParseReflectionMap( shaderStage_t *stage, const char **text, const int bundleIndex = TB_REFLECTIONMAP )
{
	char buffer[ 1024 ] = "";

	stage->overrideWrapType = true;
	stage->wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;

	if ( ParseMap( text, buffer, sizeof( buffer ) ) )
	{
		stage->isCubeMap = true;
		LoadMap( stage, buffer, stageType_t::ST_REFLECTIONMAP, bundleIndex );
	}
}

// This is not legacy.
static void ParseReflectionStage( shaderStage_t *stage, const char **text )
{
	stage->active = true;
	stage->type = stageType_t::ST_REFLECTIONMAP;
	stage->rgbGen = colorGen_t::CGEN_IDENTITY;
	stage->stateBits = GLS_DEFAULT;

	ParseReflectionMap( stage, text, TB_COLORMAP );
}

static void ParseReflectionStageBlended( shaderStage_t *stage, const char **text )
{
	stage->active = true;
	stage->type = stageType_t::ST_REFLECTIONMAP;
	stage->rgbGen = colorGen_t::CGEN_IDENTITY;
	stage->stateBits = GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE;

	ParseReflectionMap( stage, text, TB_COLORMAP );
}

static void ParseLightFalloffImage( shaderStage_t *stage, const char **text )
{
	char buffer[ 1024 ] = "";

	stage->active = true;
	stage->type = stageType_t::ST_ATTENUATIONMAP_Z;
	stage->rgbGen = colorGen_t::CGEN_IDENTITY;
	stage->stateBits = GLS_DEFAULT;
	stage->overrideWrapType = true;
	stage->wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;

	if ( ParseMap( text, buffer, sizeof( buffer ) ) )
	{
		LoadMap( stage, buffer, stageType_t::ST_COLORMAP );
	}
}

static bool HasNormalFormat( shaderStage_t *stage )
{
	return stage->normalFormat[ 0 ];
}

/* SetNormalFormat: set normal format for given stage if normal format
is not already set or it must be overwritten.

Set force option to true to overwrite existing normalFormat.

The idea is to call SetNormalFormat with force set to true when called by normalFormat keyword
so existing per-keyword format is overwritten, but keyword like normalMap does not set force
to true so their per-keyword format is only set if there is no normal format already set.

This ensures those two materials are rendered the same:

```
textures/castle/brick
{
		diffuseMap textures/castle/brick_d
		// set normalMap-specific normal format
		normalMap textures/castle/brick_n
		// overwrite with custom normal format
		normalFormat X Y Z
}
```

```
textures/castle/brick
{
		diffuseMap textures/castle/brick_d
		// set custom normal format
		normalFormat X Y Z
		// do not set normalMap-specific normal format
		//keep existing custom one
		normalMap textures/castle/brick_n
}
```
*/

static void SetNormalFormat( shaderStage_t *stage, const int8_t normalFormat[ 3 ], bool force = false )
{
	if ( force || !HasNormalFormat( stage ) )
	{
		VectorCopy( normalFormat, stage->normalFormat );
	}
}

struct extraMapParser_t
{
	const char *suffix;
	const char *description;
	void ( *parser ) ( shaderStage_t*, const char**, int );
	int bundleIndex;
};

static const extraMapParser_t dpExtraMapParsers[] =
{
	{ "_norm",    "DarkPlaces normal map",     ParseNormalMapDetectHeightMap,   TB_NORMALMAP, },
	{ "_bump",    "DarkPlaces height map",     ParseHeightMap,     TB_HEIGHTMAP, },
	{ "_gloss",   "DarkPlaces specular map",   ParseSpecularMap,   TB_SPECULARMAP, },
	{ "_glow",    "DarkPlaces glow map",       ParseGlowMap,       TB_GLOWMAP, },
	// DarkPlaces implemented _luma for Tenebrae compatibility, the
	// probability of finding this suffix with Xonotic maps is very low.
	{ "_luma",    "Tenebrae glow map",         ParseGlowMap,       TB_GLOWMAP, },
};

/*
=================
LoadExtraMaps

Look for implicit extra maps (normal, specular…) for the given image path
=================
*/
void LoadExtraMaps( shaderStage_t *stage, const char *colorMapName )
{
	if ( r_dpMaterial.Get() )
	{
		/* DarkPlaces material compatibility

		note that the DarkPlaces renderer code is very different than Dæmon one
		so that code is just trying to produce the same results but it shares
		no one code at all

		note that dp* keywords will be processed elsewhere

		look if the stage to be parsed has a map keyword without a stage keyword,
		if there is look for extra maps based on their suffixes,
		if they exist load them,
		and tell the stage to be parsed is a diffuseMap one just before parsing it

		basically it will turn this:

		```
			textures/map_solarium/water4
			{
				qer_editorimage textures/map_solarium/water4/water4.tga
				qer_trans 20
				surfaceparm nomarks
				surfaceparm trans
				surfaceparm water
				surfaceparm nolightmap
				q3map_globaltexture
				tessSize 256
				cull none
				{
					map textures/map_solarium/water4/water4.tga
					tcmod scale  0.3  0.4
					tcMod scroll 0.05 0.05
					blendfunc add
					alphaGen  vertex
				}
				dpreflectcube cubemaps/default/sky
				{
					map $lightmap
					blendfunc add
					tcGen lightmap
				}
				dp_water 0.1 1.2  1.4 0.7  1 1 1  1 1 1  0.1
			}
		```

		into this:

		```
			textures/map_solarium/water4
			{
				qer_editorimage textures/map_solarium/water4/water4
				qer_trans 20
				surfaceparm nomarks
				surfaceparm trans
				surfaceparm water
				surfaceparm nolightmap
				q3map_globaltexture
				tessSize 256
				cull none
				{
					diffuseMap  textures/map_solarium/water4/water4
					normalMap   textures/map_solarium/water4/water4_norm
					specularMap textures/map_solarium/water4/water4_gloss
					normalFormat +X +Y +Z
					tcmod scale  0.3  0.4
					tcMod scroll 0.05 0.05
					blendfunc add
					alphaGen  vertex
				}
				dpreflectcube cubemaps/default/sky
				dp_water 0.1 1.2  1.4 0.7  1 1 1  1 1 1  0.1
			}
		```

		*/

		// do not look for extramap for $whiteimage etc.
		switch ( colorMapName[0] )
		{
			case '$':
			case '_':
			case '*':
				return;
			default:
				break;
		}

		char colorMapBaseName[ MAX_QPATH ];
		bool foundExtraMap = false;

		Log::Debug( "looking for DarkPlaces extra maps for color map: '%s'", colorMapName );

		COM_StripExtension3( colorMapName, colorMapBaseName, sizeof( colorMapBaseName ) );

		for ( const extraMapParser_t parser: dpExtraMapParsers )
		{
			std::string extraMapName = Str::Format( "%s%s", colorMapBaseName, parser.suffix );
			if ( R_HasImageLoader( extraMapName.c_str() ) )
			{
				foundExtraMap = true;
				Log::Debug( "found extra %s '%s'", parser.description, extraMapName.c_str() );

				const char *name = extraMapName.c_str();
				parser.parser( stage, &name, parser.bundleIndex );

				if ( parser.bundleIndex == TB_NORMALMAP )
				{
					// Xonotic uses +X +Y +Z (OpenGL)
					SetNormalFormat( stage, glNormalFormat );
				}
			}
		}

		if ( foundExtraMap )
		{
			// We don't know yet if there is a light map.
			stage->type = stageType_t::ST_COLLAPSE_COLORMAP;
			stage->collapseType = collapseType_t::COLLAPSE_PHONG;
			stage->dpMaterial = true;
		}
	}
}

/*
===================
ParseStage
===================
*/
static bool ParseStage( shaderStage_t *stage, const char **text )
{
	const char *token;
	int          colorMaskBits = 0;
	int depthMaskBits = GLS_DEPTHMASK_TRUE, blendSrcBits = 0, blendDstBits = 0, atestBits = 0, depthFuncBits = 0, polyModeBits = 0;
	bool     depthMaskExplicit = false;
	int          imageBits = 0;
	filterType_t filterType;
	char         buffer[ 1024 ] = "";
	bool     loadMap = false;

	while ( true )
	{
		token = COM_ParseExt2( text, true );

		if ( !token[ 0 ] )
		{
			Log::Warn("no matching '}' found" );
			return false;
		}

		if ( token[ 0 ] == '}' )
		{
			break;
		}
		// if(<condition>)
		else if ( !Q_stricmp( token, "if" ) )
		{
			ParseExpression( text, &stage->ifExp );
		}
		// map <name>
		else if ( !Q_stricmp( token, "map" ) )
		{
			if ( !ParseMap( text, buffer, sizeof( buffer ) ) )
			{
				return false;
			}
			else
			{
				LoadExtraMaps( stage, buffer );

				loadMap = true;
			}
		}
		// collapsed diffuse stage enables implicit lightmap
		else if ( !Q_stricmp( token, "diffuseMap" ) )
		{
			if ( stage->collapseType == collapseType_t::COLLAPSE_none )
			{
				stage->collapseType = collapseType_t::COLLAPSE_generic;
			}

			stage->type = stageType_t::ST_COLLAPSE_DIFFUSEMAP;
			ParseDiffuseMap( stage, text );
		}
		else if ( !Q_stricmp( token, "normalMap" ) )
		{
			if ( stage->collapseType == collapseType_t::COLLAPSE_none )
			{
				stage->collapseType = collapseType_t::COLLAPSE_generic;
			}

			ParseNormalMap( stage, text );
			SetNormalFormat( stage, dxNormalFormat );
		}
		else if ( !Q_stricmp( token, "normalHeightMap" ) )
		{
			if ( stage->collapseType == collapseType_t::COLLAPSE_none )
			{
				stage->collapseType = collapseType_t::COLLAPSE_generic;
			}

			stage->hasHeightMapInNormalMap = true;
			ParseNormalMap( stage, text );
			SetNormalFormat( stage, dxNormalFormat );
		}
		else if ( !Q_stricmp( token, "heightMap" ) )
		{
			if ( stage->collapseType == collapseType_t::COLLAPSE_none )
			{
				stage->collapseType = collapseType_t::COLLAPSE_generic;
			}

			ParseHeightMap( stage, text );
		}
		else if ( !Q_stricmp( token, "specularMap" ) )
		{
			if ( stage->collapseType == collapseType_t::COLLAPSE_REFLECTIONMAP )
			{
				Log::Warn("Supposedly you shouldn't have a specularMap after a reflectionMap (in shader '%s')?", shader.name);
				// keep REFLECTION instead
			}
			else if ( stage->collapseType == collapseType_t::COLLAPSE_PBR )
			{
				Log::Warn("Supposedly you shouldn't have a specularMap after a reflectionMap (in shader '%s')?", shader.name);
				// keep PBR instead
			}
			else
			{
				stage->collapseType = collapseType_t::COLLAPSE_PHONG;
				ParseSpecularMap( stage, text );
			}
		}
		else if ( !Q_stricmp( token, "physicalMap" ) )
		{
			if ( stage->collapseType == collapseType_t::COLLAPSE_REFLECTIONMAP )
			{
				Log::Warn("Supposedly you shouldn't have a physicalMap after a reflectionMap (in shader '%s')?", shader.name);
				// keep REFLECTION instead
			}
			else if ( stage->collapseType == collapseType_t::COLLAPSE_PHONG )
			{
				Log::Warn("Supposedly you shouldn't have a physicalMap after a specularMap (in shader '%s')?", shader.name);
				// keep PHONG instead
			}
			else
			{
				// Daemon PBR packing defaults to ORM like glTF 2.0
				stage->collapseType = collapseType_t::COLLAPSE_PBR;
				ParsePhysicalMap( stage, text );
			}
		}
		else if ( !Q_stricmp( token, "glowMap" ) )
		{
			if ( stage->collapseType == collapseType_t::COLLAPSE_none )
			{
				stage->collapseType = collapseType_t::COLLAPSE_generic;
			}

			ParseGlowMap( stage, text );
		}
		/* not handled yet:
		else if ( !Q_stricmp( token, "reflectionMap" ) )
		else if ( !Q_stricmp( token, "reflectionMapBlended" ) )
		else if ( !Q_stricmp( token, "skyboxMap" ) )
		else if ( !Q_stricmp( token, "screenMap" ) )
		else if ( !Q_stricmp( token, "portalMap" ) )
		else if ( !Q_stricmp( token, "heathazeMap" ) )
		else if ( !Q_stricmp( token, "liquidMap" ) )
		else if ( !Q_stricmp( token, "attenuationMapXY" ) )
		else if ( !Q_stricmp( token, "attenuationMapZ" ) )
		*/
		// lightmap <name>
		// What is the use case for this?
		else if ( !Q_stricmp( token, "lightmap" ) )
		{
			if ( !ParseMap( text, buffer, sizeof( buffer ) ) )
			{
				return false;
			}
			else
			{
				loadMap = true;
			}
		}
		// clampmap <name>
		else if ( !Q_stricmp( token, "clampmap" ) )
		{
			if ( stage->collapseType != collapseType_t::COLLAPSE_none )
			{
				Log::Warn("keyword '%s' cannot be used in collapsed shader '%s'", token, shader.name );
			}

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parameter for 'clampmap' keyword in shader '%s'", shader.name );
				return false;
			}

			imageBits = 0;

			if ( stage->overrideNoPicMip || shader.noPicMip )
			{
				imageBits |= IF_NOPICMIP;
			}

			if ( stage->fitScreen || shader.fitScreen )
			{
				imageBits |= IF_FITSCREEN;
			}

			if ( stage->overrideFilterType )
			{
				filterType = stage->filterType;
			}
			else
			{
				filterType = shader.filterType;
			}

			imageParams_t imageParams = {};
			imageParams.bits = imageBits;
			imageParams.filterType = filterType;
			imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;
			imageParams.minDimension = shader.imageMinDimension;
			imageParams.maxDimension = shader.imageMaxDimension;

			stage->bundle[ 0 ].image[ 0 ] = R_FindImageFile( token, imageParams );

			if ( !stage->bundle[ 0 ].image[ 0 ] )
			{
				Log::Warn("R_FindImageFile could not find '%s' in shader '%s'", token, shader.name );
				return false;
			}
		}
		// animMap <frequency> <image1> .... <imageN>
		else if ( !Q_stricmp( token, "animMap" ) )
		{
			if ( stage->collapseType != collapseType_t::COLLAPSE_none )
			{
				Log::Warn("keyword '%s' cannot be used in collapsed shader '%s'", token, shader.name );
			}

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parameter for 'animMap' keyword in shader '%s'", shader.name );
				return false;
			}

			stage->bundle[ 0 ].imageAnimationSpeed = atof( token );

			// parse up to MAX_IMAGE_ANIMATIONS animations
			while ( true )
			{
				int num;

				token = COM_ParseExt2( text, false );

				if ( !token[ 0 ] )
				{
					break;
				}

				num = stage->bundle[ 0 ].numImages;

				if ( num < MAX_IMAGE_ANIMATIONS )
				{
					imageParams_t imageParams = {};
					imageParams.bits = IF_NONE;
					imageParams.filterType = filterType_t::FT_DEFAULT;
					imageParams.wrapType = wrapTypeEnum_t::WT_REPEAT;
					imageParams.minDimension = shader.imageMinDimension;
					imageParams.maxDimension = shader.imageMaxDimension;

					stage->bundle[ 0 ].image[ num ] = R_FindImageFile( token, imageParams );

					if ( !stage->bundle[ 0 ].image[ num ] )
					{
						Log::Warn("R_FindImageFile could not find '%s' in shader '%s'", token,
						           shader.name );
						return false;
					}

					stage->bundle[ 0 ].numImages++;
				}
			}
		}
		else if ( !Q_stricmp( token, "videoMap" ) )
		{
			if ( stage->collapseType != collapseType_t::COLLAPSE_none )
			{
				Log::Warn( "keyword '%s' cannot be used in collapsed shader '%s'",
					token, shader.name );
			}

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn( "missing parameter for 'videoMap' keyword in shader '%s'", shader.name );
				return false;
			}

			stage->bundle[ 0 ].videoMapHandle = CIN_PlayCinematic( token );
			stage->bundle[ 0 ].isVideoMap = true;
			stage->bundle[ 0 ].image[ 0 ] = tr.cinematicImage[ stage->bundle[ 0 ].videoMapHandle ];
		}
		// cubeMap <map>
		else if ( !Q_stricmp( token, "cubeMap" ) || !Q_stricmp( token, "cameraCubeMap" ) )
		{
			if ( stage->collapseType != collapseType_t::COLLAPSE_none )
			{
				Log::Warn("keyword '%s' cannot be used in collapsed shader '%s'", token, shader.name );
			}

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parameter for 'cubeMap' keyword in shader '%s'", shader.name );
				return false;
			}

			imageBits = 0;

			if ( stage->overrideNoPicMip || shader.noPicMip )
			{
				imageBits |= IF_NOPICMIP;
			}

			if ( stage->fitScreen || shader.fitScreen )
			{
				imageBits |= IF_FITSCREEN;
			}

			if ( stage->overrideFilterType )
			{
				filterType = stage->filterType;
			}
			else
			{
				filterType = shader.filterType;
			}

			imageParams_t imageParams = {};
			imageParams.bits = imageBits;
			imageParams.filterType = filterType;
			imageParams.wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;
			imageParams.minDimension = shader.imageMinDimension;
			imageParams.maxDimension = shader.imageMaxDimension;

			stage->bundle[ 0 ].image[ 0 ] = R_FindCubeImage( token, imageParams );

			if ( !stage->bundle[ 0 ].image[ 0 ] )
			{
				Log::Warn("R_FindCubeImage could not find '%s' in shader '%s'", token, shader.name );
				return false;
			}
		}
		// alphafunc <func>
		else if ( !Q_stricmp( token, "alphaFunc" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parameter for 'alphaFunc' keyword in shader '%s'", shader.name );
				return false;
			}

			atestBits = NameToAFunc( token );
		}
		// alphaTest <exp>
		else if ( !Q_stricmp( token, "alphaTest" ) )
		{
			atestBits = GLS_ATEST_GE_128;
			ParseExpression( text, &stage->alphaTestExp );
		}
		// depthFunc <func>
		else if ( !Q_stricmp( token, "depthfunc" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parameter for 'depthfunc' keyword in shader '%s'", shader.name );
				return false;
			}

			if ( !Q_stricmp( token, "lequal" ) )
			{
				depthFuncBits = 0;
			}
			else if ( !Q_stricmp( token, "equal" ) )
			{
				depthFuncBits = GLS_DEPTHFUNC_EQUAL;
			}
			else
			{
				Log::Warn("unknown depthfunc '%s' in shader '%s'", token, shader.name );
				continue;
			}
		}
		// ignoreAlphaTest
		else if ( !Q_stricmp( token, "ignoreAlphaTest" ) )
		{
			depthFuncBits = 0;
		}
		// nearest
		else if ( !Q_stricmp( token, "nearest" ) )
		{
			stage->overrideFilterType = true;
			stage->filterType = filterType_t::FT_NEAREST;
		}
		// linear
		else if ( !Q_stricmp( token, "linear" ) )
		{
			stage->overrideFilterType = true;
			stage->filterType = filterType_t::FT_LINEAR;

			stage->overrideNoPicMip = true;
		}
		// noPicMip
		else if ( !Q_stricmp( token, "noPicMip" ) )
		{
			stage->overrideNoPicMip = true;
		}
		else if ( !Q_stricmp( token, "fitScreen" ) )
		{
			stage->fitScreen = true;
		}
		// clamp, edgeClamp etc.
		else if ( ParseClampType( token, &stage->wrapType ) )
		{
			stage->overrideWrapType = true;
		}
		// uncompressed (removed option)
		else if ( !Q_stricmp( token, "uncompressed" ) )
		{
		}
		// highQuality
		else if ( !Q_stricmp( token, "highQuality" ) )
		{
			stage->highQuality = true;
			stage->overrideNoPicMip = true;
		}
		// forceHighQuality
		else if ( !Q_stricmp( token, "forceHighQuality" ) )
		{
			stage->forceHighQuality = true;
			stage->overrideNoPicMip = true;
		}
		// ET fog
		else if ( !Q_stricmp( token, "fog" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] == 0 )
			{
				Log::Warn("missing parm for fog in shader '%s'", shader.name );
				continue;
			}

			// This isn't actually used because we render fog based on the shader, not the stage
			if ( !Q_stricmp( token, "on" ) )
			{
				stage->noFog = false;
			}
			else
			{
				stage->noFog = true;
			}
		}
		else if ( !Q_stricmp( token, "depthFade" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parameter for 'depthFade' keyword in shader '%s'", shader.name );
				return false;
			}

			stage->hasDepthFade = true;
			stage->depthFadeValue = atof( token );

			if ( stage->depthFadeValue <= 0.0f )
			{
				Log::Warn("depthFade parameter <= 0 in '%s'", shader.name );
				stage->depthFadeValue = 1.0f;
			}

		}
		//    blend[func] <srcFactor> [,] <dstFactor>
		// or blend[func] <add | filter | blend>
		// or blend[func] <diffusemap | bumpmap | specularmap>
		else if ( !Q_stricmp( token, "blendfunc" ) ||
			  !Q_stricmp( token, "blend" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] == 0 )
			{
				Log::Warn("missing parm for blend in shader '%s'", shader.name );
				continue;
			}

			// check for "simple" blends first
			if ( !Q_stricmp( token, "add" ) )
			{
				blendSrcBits = GLS_SRCBLEND_ONE;
				blendDstBits = GLS_DSTBLEND_ONE;
			}
			else if ( !Q_stricmp( token, "filter" ) )
			{
				blendSrcBits = GLS_SRCBLEND_DST_COLOR;
				blendDstBits = GLS_DSTBLEND_ZERO;
			}
			else if ( !Q_stricmp( token, "modulate" ) )
			{
				blendSrcBits = GLS_SRCBLEND_DST_COLOR;
				blendDstBits = GLS_DSTBLEND_ZERO;
			}
			else if ( !Q_stricmp( token, "blend" ) )
			{
				blendSrcBits = GLS_SRCBLEND_SRC_ALPHA;
				blendDstBits = GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
			}
			else if ( !Q_stricmp( token, "none" ) )
			{
				blendSrcBits = GLS_SRCBLEND_ZERO;
				blendDstBits = GLS_DSTBLEND_ONE;
			}
			// check for other semantic meanings
			else if ( !Q_stricmp( token, "diffuseMap" ) )
			{
				Log::Warn("deprecated idTech4 blend parameter '%s' in shader '%s', better use it in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_DIFFUSEMAP;
			}
			else if ( !Q_stricmp( token, "normalMap" ) )
			{
				Log::Warn("deprecated idTech4 blend parameter '%s' in shader '%s', better use it in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_NORMALMAP;
				SetNormalFormat( stage, dxNormalFormat );
			}
			else if ( !Q_stricmp( token, "bumpMap" ) )
			{
				Log::Warn("deprecated idTech4 blend parameter '%s' in shader '%s', better use 'normalMap' keyword in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_NORMALMAP;
				SetNormalFormat( stage, dxNormalFormat );
			}
			else if ( !Q_stricmp( token, "specularMap" ) )
			{
				Log::Warn("deprecated idTech4 blend parameter '%s' in shader '%s', better use it in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_SPECULARMAP;
			}
			else if ( !Q_stricmp( token, "physicalMap" ) )
			{
				Log::Warn("deprecated idTech4 blend parameter '%s' in shader '%s', better use it in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_PHYSICALMAP;
			}
			else if ( !Q_stricmp( token, "glowMap" ) )
			{
				Log::Warn("deprecated idTech4 blend parameter '%s' in shader '%s', better use it in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_GLOWMAP;
			}
			else
			{
				// complex double blends
				blendSrcBits = NameToSrcBlendMode( token );

				token = COM_ParseExt2( text, false );

				if ( !Q_stricmp(token, "," ) ) {
					token = COM_ParseExt2( text, false );
				}

				if ( token[ 0 ] == 0 )
				{
					Log::Warn("missing parm for blend in shader '%s'", shader.name );
					continue;
				}

				blendDstBits = NameToDstBlendMode( token );
			}

			// clear depth mask for blended surfaces
			if ( !depthMaskExplicit &&
			     (stage->type == stageType_t::ST_COLORMAP ||
			      stage->type == stageType_t::ST_COLLAPSE_COLORMAP ||
			      stage->type == stageType_t::ST_DIFFUSEMAP ||
			      stage->type == stageType_t::ST_COLLAPSE_DIFFUSEMAP ||
			      stage->collapseType != collapseType_t::COLLAPSE_none ) &&
			     blendSrcBits != 0 && blendDstBits != 0
			     && !(blendSrcBits == GLS_SRCBLEND_ONE &&
				  blendDstBits == GLS_DSTBLEND_ZERO) )
			{
				depthMaskBits = 0;
			}
		}
		// stage <type>
		else if ( !Q_stricmp( token, "stage" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] == 0 )
			{
				Log::Warn("missing parameters for stage in shader '%s'", shader.name );
				continue;
			}

			if ( !Q_stricmp( token, "diffuseMap" ) )
			{
				Log::Warn("deprecated XreaL stage parameter '%s' in shader '%s', better use it in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_DIFFUSEMAP;
			}
			else if ( !Q_stricmp( token, "normalMap" ) )
			{
				Log::Warn("deprecated XreaL stage parameter '%s' in shader '%s', better use it in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_NORMALMAP;
				SetNormalFormat( stage, dxNormalFormat );
			}
			else if ( !Q_stricmp( token, "bumpMap" ) )
			{
				Log::Warn("deprecated XreaL stage parameter '%s' in shader '%s', better use 'normalMap' keyword in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_NORMALMAP;
				SetNormalFormat( stage, dxNormalFormat );
			}
			else if ( !Q_stricmp( token, "specularMap" ) )
			{
				Log::Warn("deprecated XreaL stage parameter '%s' in shader '%s', better use it in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_SPECULARMAP;
			}
			else if ( !Q_stricmp( token, "physicalMap" ) )
			{
				Log::Warn("deprecated XreaL stage parameter '%s' in shader '%s', better use it in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_PHYSICALMAP;
			}
			else if ( !Q_stricmp( token, "glowMap" ) )
			{
				Log::Warn("deprecated XreaL stage parameter '%s' in shader '%s', better use it in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_GLOWMAP;
			}
			else if ( !Q_stricmp( token, "reflectionMap" ) )
			{
				Log::Warn("deprecated XreaL stage parameter '%s' in shader '%s', better use it in place of 'map' keyword and pack related textures within the same stage", token, shader.name );
				stage->type = stageType_t::ST_REFLECTIONMAP;
			}
			else if ( !Q_stricmp( token, "skyboxMap" ) )
			{
				stage->type = stageType_t::ST_SKYBOXMAP;
			}
			else if ( !Q_stricmp( token, "screenMap" ) )
			{
				stage->type = stageType_t::ST_SCREENMAP;
			}
			else if ( !Q_stricmp( token, "portalMap" ) )
			{
				stage->type = stageType_t::ST_PORTALMAP;
			}
			else if ( !Q_stricmp( token, "heathazeMap" ) )
			{
				stage->type = stageType_t::ST_HEATHAZEMAP;
				SetNormalFormat( stage, dxNormalFormat );
			}
			else if ( !Q_stricmp( token, "liquidMap" ) )
			{
				stage->type = stageType_t::ST_LIQUIDMAP;
				SetNormalFormat( stage, dxNormalFormat );
			}
			else if ( !Q_stricmp( token, "attenuationMapXY" ) )
			{
				stage->type = stageType_t::ST_ATTENUATIONMAP_XY;
			}
			else if ( !Q_stricmp( token, "attenuationMapZ" ) )
			{
				stage->type = stageType_t::ST_ATTENUATIONMAP_Z;
			}
			else
			{
				Log::Warn("unknown stage parameter '%s' in shader '%s'", token, shader.name );
				continue;
			}
		}
		// rgbGen
		else if ( !Q_stricmp( token, "rgbGen" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] == 0 )
			{
				Log::Warn("missing parameters for rgbGen in shader '%s'", shader.name );
				continue;
			}

			if ( !Q_stricmp( token, "wave" ) )
			{
				ParseWaveForm( text, &stage->rgbWave );
				stage->rgbGen = colorGen_t::CGEN_WAVEFORM;
			}
			else if ( !Q_stricmp( token, "const" ) )
			{
				vec3_t color;

				ParseVector( text, 3, color );
				stage->constantColor.SetRed( 255 * color[0] );
				stage->constantColor.SetGreen( 255 * color[1] );
				stage->constantColor.SetBlue( 255 * color[2] );

				stage->rgbGen = colorGen_t::CGEN_CONST;
			}
			else if ( !Q_stricmp( token, "identity" ) )
			{
				stage->rgbGen = colorGen_t::CGEN_IDENTITY;
			}
			else if ( !Q_stricmp( token, "identityLighting" ) )
			{
				stage->rgbGen = colorGen_t::CGEN_IDENTITY_LIGHTING;
			}
			else if ( !Q_stricmp( token, "entity" ) )
			{
				stage->rgbGen = colorGen_t::CGEN_ENTITY;
			}
			else if ( !Q_stricmp( token, "oneMinusEntity" ) )
			{
				stage->rgbGen = colorGen_t::CGEN_ONE_MINUS_ENTITY;
			}
			else if ( !Q_stricmp( token, "vertex" ) )
			{
				stage->rgbGen = colorGen_t::CGEN_VERTEX;

				if ( stage->alphaGen == alphaGen_t::AGEN_IDENTITY )
				{
					stage->alphaGen = alphaGen_t::AGEN_VERTEX;
				}
			}
			else if ( !Q_stricmp( token, "exactVertex" ) )
			{
				stage->rgbGen = colorGen_t::CGEN_VERTEX;
			}
			else if ( !Q_stricmp( token, "lightingDiffuse" ) )
			{
				//Log::Warn("obsolete rgbGen lightingDiffuse keyword not supported in shader '%s'", shader.name);
				stage->type = stageType_t::ST_DIFFUSEMAP;
				stage->rgbGen = colorGen_t::CGEN_IDENTITY_LIGHTING;
			}
			else if ( !Q_stricmp( token, "oneMinusVertex" ) )
			{
				stage->rgbGen = colorGen_t::CGEN_ONE_MINUS_VERTEX;
			}
			else
			{
				Log::Warn("unknown rgbGen parameter '%s' in shader '%s'", token, shader.name );
				continue;
			}
		}
		// rgb <arithmetic expression>
		else if ( !Q_stricmp( token, "rgb" ) )
		{
			stage->rgbGen = colorGen_t::CGEN_CUSTOM_RGB;
			ParseExpression( text, &stage->rgbExp );
		}
		// red <arithmetic expression>
		else if ( !Q_stricmp( token, "red" ) )
		{
			stage->rgbGen = colorGen_t::CGEN_CUSTOM_RGBs;
			ParseExpression( text, &stage->redExp );
		}
		// green <arithmetic expression>
		else if ( !Q_stricmp( token, "green" ) )
		{
			stage->rgbGen = colorGen_t::CGEN_CUSTOM_RGBs;
			ParseExpression( text, &stage->greenExp );
		}
		// blue <arithmetic expression>
		else if ( !Q_stricmp( token, "blue" ) )
		{
			stage->rgbGen = colorGen_t::CGEN_CUSTOM_RGBs;
			ParseExpression( text, &stage->blueExp );
		}
		// colored
		else if ( !Q_stricmp( token, "colored" ) )
		{
			stage->rgbGen = colorGen_t::CGEN_ENTITY;
			stage->alphaGen = alphaGen_t::AGEN_ENTITY;
		}
		// vertexColor
		else if ( !Q_stricmp( token, "vertexColor" ) )
		{
			stage->rgbGen = colorGen_t::CGEN_VERTEX;

			if ( stage->alphaGen == alphaGen_t::AGEN_IDENTITY )
			{
				stage->alphaGen = alphaGen_t::AGEN_VERTEX;
			}
		}
		// inverseVertexColor
		else if ( !Q_stricmp( token, "inverseVertexColor" ) )
		{
			stage->rgbGen = colorGen_t::CGEN_ONE_MINUS_VERTEX;

			if ( stage->alphaGen == alphaGen_t::AGEN_IDENTITY )
			{
				stage->alphaGen = alphaGen_t::AGEN_ONE_MINUS_VERTEX;
			}
		}
		// alphaGen
		else if ( !Q_stricmp( token, "alphaGen" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] == 0 )
			{
				Log::Warn("missing parameters for alphaGen in shader '%s'", shader.name );
				continue;
			}

			if ( !Q_stricmp( token, "wave" ) )
			{
				ParseWaveForm( text, &stage->alphaWave );
				stage->alphaGen = alphaGen_t::AGEN_WAVEFORM;
			}
			else if ( !Q_stricmp( token, "const" ) )
			{
				token = COM_ParseExt2( text, false );
				stage->constantColor.SetAlpha( 255 * atof( token ) );
				stage->alphaGen = alphaGen_t::AGEN_CONST;
			}
			else if ( !Q_stricmp( token, "identity" ) )
			{
				stage->alphaGen = alphaGen_t::AGEN_IDENTITY;
			}
			else if ( !Q_stricmp( token, "entity" ) )
			{
				stage->alphaGen = alphaGen_t::AGEN_ENTITY;
			}
			else if ( !Q_stricmp( token, "oneMinusEntity" ) )
			{
				stage->alphaGen = alphaGen_t::AGEN_ONE_MINUS_ENTITY;
			}
			else if ( !Q_stricmp( token, "vertex" ) )
			{
				stage->alphaGen = alphaGen_t::AGEN_VERTEX;
			}
			else if ( !Q_stricmp( token, "lightingSpecular" ) )
			{
				Log::Warn("alphaGen lightingSpecular keyword not supported in shader '%s'", shader.name );
			}
			else if ( !Q_stricmp( token, "oneMinusVertex" ) )
			{
				stage->alphaGen = alphaGen_t::AGEN_ONE_MINUS_VERTEX;
			}
			else if ( !Q_stricmp( token, "portal" ) )
			{
				stage->type = stageType_t::ST_PORTALMAP;
				stage->alphaGen = alphaGen_t::AGEN_PORTAL;

				token = COM_ParseExt2( text, false );

				/* Until we implement “alphaGen portal” to blend textures on them
				we are still rendering portals so we need to parse the range. */
				if ( token[ 0 ] )
				{
					shader.portalRange = atof( token );
				}

				/* Quake3 shaders made the portalRange a requirement, we choose
				to make it optional and fallback later on the default value or the
				value that is previously set as “portal <range>” at shader level. */
			}
			else
			{
				Log::Warn("unknown alphaGen parameter '%s' in shader '%s'", token, shader.name );
				continue;
			}
		}
		// alpha <arithmetic expression>
		else if ( !Q_stricmp( token, "alpha" ) )
		{
			stage->alphaGen = alphaGen_t::AGEN_CUSTOM;
			ParseExpression( text, &stage->alphaExp );
		}
		// color <exp>, <exp>, <exp>, <exp>
		else if ( !Q_stricmp( token, "color" ) )
		{
			stage->rgbGen = colorGen_t::CGEN_CUSTOM_RGBs;
			stage->alphaGen = alphaGen_t::AGEN_CUSTOM;
			ParseExpression( text, &stage->redExp );
			ParseExpression( text, &stage->greenExp );
			ParseExpression( text, &stage->blueExp );
			ParseExpression( text, &stage->alphaExp );
		}
		// tcGen <function>
		else if ( !Q_stricmp( token, "texGen" ) || !Q_stricmp( token, "tcGen" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] == 0 )
			{
				Log::Warn("missing texGen parm in shader '%s'", shader.name );
				continue;
			}

			if ( !Q_stricmp( token, "environment" ) )
			{
				//Log::Warn("texGen environment keyword not supported in shader '%s'", shader.name);
				stage->tcGen_Environment = true;
				stage->tcGen_Lightmap = false;
			}
			else if ( !Q_stricmp( token, "lightmap" ) )
			{
				imageBits |= IF_NOPICMIP;
				stage->tcGen_Lightmap = true;
				stage->tcGen_Environment = false;
			}
			else if ( !Q_stricmp( token, "texture" ) || !Q_stricmp( token, "base" ) )
			{
			}
			// FIXME: this is a deprecated way of doing things
			else if ( !Q_stricmp( token, "reflect" ) )
			{
				stage->type = stageType_t::ST_REFLECTIONMAP;
			}
			else if ( !Q_stricmp( token, "skybox" ) )
			{
				stage->type = stageType_t::ST_SKYBOXMAP;
			}
			else
			{
				Log::Warn("unknown tcGen keyword '%s' not supported in shader '%s'", token, shader.name );
				SkipRestOfLine( text );
			}
		}
		// tcMod <type> <...>
		else if ( !Q_stricmp( token, "tcMod" ) )
		{
			if ( !ParseTexMod( text, stage ) )
			{
				return false;
			}
		}
		// scroll
		else if ( !Q_stricmp( token, "scroll" ) || !Q_stricmp( token, "translate" ) )
		{
			texModInfo_t *tmi;

			if ( stage->bundle[ 0 ].numTexMods == TR_MAX_TEXMODS )
			{
				Sys::Drop( "ERROR: too many tcMod stages in shader '%s'", shader.name );
			}

			tmi = &stage->bundle[ 0 ].texMods[ stage->bundle[ 0 ].numTexMods ];
			stage->bundle[ 0 ].numTexMods++;

			ParseExpression( text, &tmi->sExp );
			ParseExpression( text, &tmi->tExp );

			tmi->type = texMod_t::TMOD_SCROLL2;
		}
		// scale
		else if ( !Q_stricmp( token, "scale" ) )
		{
			texModInfo_t *tmi;

			if ( stage->bundle[ 0 ].numTexMods == TR_MAX_TEXMODS )
			{
				Sys::Drop( "ERROR: too many tcMod stages in shader '%s'", shader.name );
			}

			tmi = &stage->bundle[ 0 ].texMods[ stage->bundle[ 0 ].numTexMods ];
			stage->bundle[ 0 ].numTexMods++;

			ParseExpression( text, &tmi->sExp );
			ParseExpression( text, &tmi->tExp );

			tmi->type = texMod_t::TMOD_SCALE2;
		}
		// centerScale
		else if ( !Q_stricmp( token, "centerScale" ) )
		{
			texModInfo_t *tmi;

			if ( stage->bundle[ 0 ].numTexMods == TR_MAX_TEXMODS )
			{
				Sys::Drop( "ERROR: too many tcMod stages in shader '%s'", shader.name );
			}

			tmi = &stage->bundle[ 0 ].texMods[ stage->bundle[ 0 ].numTexMods ];
			stage->bundle[ 0 ].numTexMods++;

			ParseExpression( text, &tmi->sExp );
			ParseExpression( text, &tmi->tExp );

			tmi->type = texMod_t::TMOD_CENTERSCALE;
		}
		// shear
		else if ( !Q_stricmp( token, "shear" ) )
		{
			texModInfo_t *tmi;

			if ( stage->bundle[ 0 ].numTexMods == TR_MAX_TEXMODS )
			{
				Sys::Drop( "ERROR: too many tcMod stages in shader '%s'", shader.name );
			}

			tmi = &stage->bundle[ 0 ].texMods[ stage->bundle[ 0 ].numTexMods ];
			stage->bundle[ 0 ].numTexMods++;

			ParseExpression( text, &tmi->sExp );
			ParseExpression( text, &tmi->tExp );

			tmi->type = texMod_t::TMOD_SHEAR;
		}
		// rotate
		else if ( !Q_stricmp( token, "rotate" ) )
		{
			texModInfo_t *tmi;

			if ( stage->bundle[ 0 ].numTexMods == TR_MAX_TEXMODS )
			{
				Sys::Drop( "ERROR: too many tcMod stages in shader '%s'", shader.name );
			}

			tmi = &stage->bundle[ 0 ].texMods[ stage->bundle[ 0 ].numTexMods ];
			stage->bundle[ 0 ].numTexMods++;

			ParseExpression( text, &tmi->rExp );

			tmi->type = texMod_t::TMOD_ROTATE2;
		}
		// depthwrite
		else if ( !Q_stricmp( token, "depthwrite" ) )
		{
			depthMaskBits = GLS_DEPTHMASK_TRUE;
			depthMaskExplicit = true;
			continue;
		}
		// maskRed
		else if ( !Q_stricmp( token, "maskRed" ) )
		{
			colorMaskBits |= GLS_REDMASK_FALSE;
		}
		// maskGreen
		else if ( !Q_stricmp( token, "maskGreen" ) )
		{
			colorMaskBits |= GLS_GREENMASK_FALSE;
		}
		// maskBlue
		else if ( !Q_stricmp( token, "maskBlue" ) )
		{
			colorMaskBits |= GLS_BLUEMASK_FALSE;
		}
		// maskAlpha
		else if ( !Q_stricmp( token, "maskAlpha" ) )
		{
			colorMaskBits |= GLS_ALPHAMASK_FALSE;
		}
		// maskColor
		else if ( !Q_stricmp( token, "maskColor" ) )
		{
			colorMaskBits |= GLS_REDMASK_FALSE | GLS_GREENMASK_FALSE | GLS_BLUEMASK_FALSE;
		}
		// maskColorAlpha
		else if ( !Q_stricmp( token, "maskColorAlpha" ) )
		{
			colorMaskBits |= GLS_REDMASK_FALSE | GLS_GREENMASK_FALSE | GLS_BLUEMASK_FALSE | GLS_ALPHAMASK_FALSE;
		}
		// maskDepth
		else if ( !Q_stricmp( token, "maskDepth" ) )
		{
			depthMaskBits &= ~GLS_DEPTHMASK_TRUE;
			depthMaskExplicit = false;
		}
		// wireFrame
		else if ( !Q_stricmp( token, "wireFrame" ) )
		{
			polyModeBits |= GLS_POLYMODE_LINE;
		}
		else if ( !Q_stricmp( token, "specularExponentMin" ) )
		{
			ParseExpression( text, &stage->specularExponentMin );
		}
		else if ( !Q_stricmp( token, "specularExponentMax" ) )
		{
			ParseExpression( text, &stage->specularExponentMax );
		}
		// refractionIndex <arithmetic expression>
		else if ( !Q_stricmp( token, "refractionIndex" ) )
		{
			ParseExpression( text, &stage->refractionIndexExp );
		}
		// fresnelPower <arithmetic expression>
		else if ( !Q_stricmp( token, "fresnelPower" ) )
		{
			ParseExpression( text, &stage->fresnelPowerExp );
		}
		// fresnelScale <arithmetic expression>
		else if ( !Q_stricmp( token, "fresnelScale" ) )
		{
			ParseExpression( text, &stage->fresnelScaleExp );
		}
		// fresnelBias <arithmetic expression>
		else if ( !Q_stricmp( token, "fresnelBias" ) )
		{
			ParseExpression( text, &stage->fresnelBiasExp );
		}
		/* normalFormat <[-]X> <[-]Y> <[-]Z>

		Describes the normal map format for the the given normal map file.

		Since this parser assumes DirectX format with normalMap keyword
		to keep compatibility with materials created for ioquake3

		To load normal maps in OpenGL format using normalMap keyword, use:
		normalFormat X Y Z

		OpenGL format is described there:
		https://github.com/KhronosGroup/glTF/tree/2.0/specification/2.0#materialnormaltexture

		> The normal vector uses OpenGL conventions where +X is right, +Y is up, and +Z points toward the viewer.

		examples of formats:

		 X  Y  Z OpenGL format
		 X -Y  Z DirectX format with reverted green channel (reverted Y component)
		-X  Y  Z weird other format with reverted red channel (reverted X component)

		Unlike normalScale that can be used to revert channels by setting them
		negative, the normalFormat keyword is meant to be engine agnostic:
		engines implementing either OpenGL or DirectX format internally
		are expected to do the required normal channel flip themselves.
		*/
		else if ( !Q_stricmp( token, "normalFormat" ) )
		{
			const char* components[3] = { "X", "Y", "Z" };
			int8_t normalFormat[ 3 ];

			for ( int i = 0; i < 3; i++ )
			{
				token = COM_ParseExt( text, false );

				if ( !Q_stricmp( token, components[ i ] ) )
				{
					normalFormat[ i ] = 1;
				}
				else if ( token[ 0 ] == '-' && !Q_stricmp( token + 1, components[ i ] ) )
				{
					normalFormat[ i ] = -1;
				}
				else
				{
					if ( token[ 0 ] == '\0' )
					{
						Log::Warn("missing normalFormat parm in shader '%s'", shader.name );
					}
					else
					{
						Log::Warn("unknown normalFormat parm in shader '%s': '%s'", shader.name, token );
					}

					break;
				}

				if ( i == 2 )
				{
					// If all three components are read properly,
					// set normal format, replace existing one if exists.
					SetNormalFormat( stage, normalFormat, true );
				}
			}

			SkipRestOfLine( text );
			continue;
		}
		/* normalScale <float> <float> <float>

		Scales normal map channels.

		For compatibility purpose, ioquake3 renderer2 syntax is supported:
		normalScale <float> <float>

		In this case it will be read like this:
		normalScale <float> <float> 1.0

		Using 1.0 as multiplier will have no effect on Z channel.

		The normalScale keyword can also be used to flip normal map channels
		by using negative values but this is strongly discouraged,
		use normalFormat keyword instead.
		*/
		else if ( !Q_stricmp( token, "normalScale" ) )
		{
			for ( int i = 0; i < 3; i++ )
			{
				token = COM_ParseExt2( text, false );

				if ( token[ 0 ] == '\0' )
				{
					if ( i == 2 )
					{
						/* Since ioquake3 renderer2 materials only tell X and Y
						components, we set the missing Z component to 1.0.
						It will have no effect.
						*/
						stage->normalScale[ 2 ] = 1.0;
					}
					else
					{
						Log::Warn("missing normalScale parm in shader '%s'", shader.name );
						continue;
					}
				}

				float j = atof( token );

				if ( j < 0.0f )
				{
					Log::Warn("not recommended negative normalScale parm in shader '%s': '%f', better use 'normalFormat' to swap normal map channel direction", shader.name, j );
				}

				stage->normalScale[ i ] = j;
			}

			SkipRestOfLine( text );
			continue;
		}
		// normalIntensity <arithmetic expression>
		else if ( !Q_stricmp( token, "normalIntensity" ) )
		{
			ParseExpression( text, &stage->normalIntensityExp );
		}
		// fogDensity <arithmetic expression>
		else if ( !Q_stricmp( token, "fogDensity" ) )
		{
			ParseExpression( text, &stage->fogDensityExp );
		}
		// depthScale <arithmetic expression>
		else if ( !Q_stricmp( token, "depthScale" ) )
		{
			ParseExpression( text, &stage->depthScaleExp );
		}
		// deformMagnitude <arithmetic expression>
		else if ( !Q_stricmp( token, "deformMagnitude" ) )
		{
			ParseExpression( text, &stage->deformMagnitudeExp );
		}
		else
		{
			Log::Warn("unknown shader stage parameter '%s' in shader '%s'", token, shader.name );
			SkipRestOfLine( text );
			continue;
		}
	}

	// parsing succeeded
	stage->active = true;

	if ( stage->type == stageType_t::ST_COLORMAP &&
		( stage->collapseType == collapseType_t::COLLAPSE_PHONG
		|| stage->collapseType == collapseType_t::COLLAPSE_PBR ) )
	{
		stage->type = stageType_t::ST_COLLAPSE_COLORMAP;
	}

	else if ( stage->collapseType == collapseType_t::COLLAPSE_REFLECTIONMAP )
	{
		stage->type = stageType_t::ST_COLLAPSE_REFLECTIONMAP;
	}

	// if cgen isn't explicitly specified, use either identity or identitylighting
	if ( stage->rgbGen == colorGen_t::CGEN_BAD )
	{
		if ( blendSrcBits == 0 || blendSrcBits == GLS_SRCBLEND_ONE || blendSrcBits == GLS_SRCBLEND_SRC_ALPHA )
		{
			stage->rgbGen = colorGen_t::CGEN_IDENTITY_LIGHTING;
		}
		else
		{
			stage->rgbGen = colorGen_t::CGEN_IDENTITY;
		}
	}

	// implicitly assume that a GL_ONE GL_ZERO blend mask disables blending
	if ( ( blendSrcBits == GLS_SRCBLEND_ONE ) && ( blendDstBits == GLS_DSTBLEND_ZERO ) )
	{
		blendDstBits = blendSrcBits = 0;
		depthMaskBits = GLS_DEPTHMASK_TRUE;
	}

	// tell shader if this stage has an alpha test
	if ( atestBits & GLS_ATEST_BITS )
	{
		shader.alphaTest = true;
	}

	// check that depthFade and depthWrite are mutually exclusive
	if ( depthMaskBits && stage->hasDepthFade ) {
		Log::Warn( "depth fade conflicts with depth mask in shader '%s'", shader.name );
		stage->hasDepthFade = false;
	}

	// compute state bits
	stage->stateBits = colorMaskBits | depthMaskBits | blendSrcBits | blendDstBits | atestBits | depthFuncBits | polyModeBits;

	// load image
	if ( loadMap && !LoadMap( stage, buffer, stage->type ) )
	{
		return false;
	}

	return true;
}

/*
===============
ParseDeform

deformVertexes wave <spread> <waveform> <base> <amplitude> <phase> <frequency>
deformVertexes normal <frequency> <amplitude>
deformVertexes move <vector> <waveform> <base> <amplitude> <phase> <frequency>
deformVertexes bulge <bulgeWidth> <bulgeHeight> <bulgeSpeed>
deformVertexes rotgrow <growSpeed> <rotSlices> <rotSpeed>
deformVertexes autoSprite
deformVertexes autoSprite2
===============
*/
static void ParseDeform( const char **text )
{
	const char *token;
	deformStage_t *ds;

	token = COM_ParseExt2( text, false );

	if ( token[ 0 ] == 0 )
	{
		Log::Warn("missing deform parm in shader '%s'", shader.name );
		return;
	}

	if ( shader.numDeforms == MAX_SHADER_DEFORMS )
	{
		Log::Warn("MAX_SHADER_DEFORMS in '%s'", shader.name );
		return;
	}

	ds = &shader.deforms[ shader.numDeforms ];
	shader.numDeforms++;

	if ( !Q_stricmp( token, "autosprite" ) )
	{
		shader.autoSpriteMode = 1;
		shader.numDeforms--;
		return;
	}

	if ( !Q_stricmp( token, "autosprite2" ) )
	{
		shader.autoSpriteMode = 2;
		shader.numDeforms--;
		return;
	}

	if ( !Q_stricmp( token, "bulge" ) )
	{
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing deformVertexes bulge parm in shader '%s'", shader.name );
			return;
		}

		ds->bulgeWidth = atof( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing deformVertexes bulge parm in shader '%s'", shader.name );
			return;
		}

		ds->bulgeHeight = atof( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing deformVertexes bulge parm in shader '%s'", shader.name );
			return;
		}

		ds->bulgeSpeed = atof( token );

		ds->deformation = deform_t::DEFORM_BULGE;
		return;
	}

	if ( !Q_stricmp( token, "wave" ) )
	{
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing deformVertexes parm in shader '%s'", shader.name );
			return;
		}

		if ( atof( token ) != 0 )
		{
			ds->deformationSpread = 1.0f / atof( token );
		}
		else
		{
			ds->deformationSpread = 100.0f;
			Log::Warn("illegal div value of 0 in deformVertexes command for shader '%s'", shader.name );
		}

		ParseWaveForm( text, &ds->deformationWave );
		ds->deformation = deform_t::DEFORM_WAVE;
		return;
	}

	if ( !Q_stricmp( token, "normal" ) )
	{
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing deformVertexes parm in shader '%s'", shader.name );
			return;
		}

		ds->deformationWave.amplitude = atof( token );

		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing deformVertexes parm in shader '%s'", shader.name );
			return;
		}

		ds->deformationWave.frequency = atof( token );

		ds->deformation = deform_t::DEFORM_NORMALS;
		return;
	}

	if ( !Q_stricmp( token, "move" ) )
	{
		int i;

		for ( i = 0; i < 3; i++ )
		{
			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] == 0 )
			{
				Log::Warn("missing deformVertexes parm in shader '%s'", shader.name );
				return;
			}

			ds->moveVector[ i ] = atof( token );
		}

		ParseWaveForm( text, &ds->deformationWave );
		ds->deformation = deform_t::DEFORM_MOVE;
		return;
	}

	if ( !Q_stricmp( token, "rotgrow" ) )
	{
		int i;

		for ( i = 0; i < 3; i++ )
		{
			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] == 0 )
			{
				Log::Warn("missing deformVertexes parm in shader '%s'", shader.name );
				return;
			}

			ds->moveVector[ i ] = atof( token );
		}

		ds->deformation = deform_t::DEFORM_ROTGROW;
		return;
	}

	if ( !Q_stricmp( token, "sprite" ) )
	{
		shader.autoSpriteMode = 1;
		shader.numDeforms--;
		return;
	}

	if ( !Q_stricmp( token, "flare" ) )
	{
		token = COM_ParseExt2( text, false );

		if ( token[ 0 ] == 0 )
		{
			Log::Warn("missing deformVertexes flare parm in shader '%s'", shader.name );
			return;
		}

		ds->flareSize = atof( token );
		return;
	}

	Log::Warn("unknown deformVertexes subtype '%s' found in shader '%s'", token, shader.name );
}

/*
===============
ParseSkyParms

skyParms <outerbox> <cloudheight> <innerbox>
===============
*/
static void ParseSkyParms( const char **text )
{
	const char *token;
	char prefix[ MAX_QPATH ];

	// outerbox
	token = COM_ParseExt2( text, false );

	if ( token[ 0 ] == 0 )
	{
		Log::Warn("'skyParms' missing parameter in shader '%s'", shader.name );
		return;
	}

	if ( strcmp( token, "-" ) )
	{
		Q_strncpyz( prefix, token, sizeof( prefix ) );

		imageParams_t imageParams = {};
		imageParams.bits = IF_NONE;
		imageParams.filterType = filterType_t::FT_DEFAULT;
		imageParams.wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;
		imageParams.minDimension = shader.imageMinDimension;
		imageParams.maxDimension = shader.imageMaxDimension;

		shader.sky.outerbox = R_FindCubeImage( prefix, imageParams );

		if ( !shader.sky.outerbox )
		{
			Log::Warn("could not find cubemap '%s' for outer skybox in shader '%s'", prefix, shader.name );
			shader.sky.outerbox = tr.blackCubeImage;
		}
	}

	// cloudheight
	token = COM_ParseExt2( text, false );

	if ( token[ 0 ] == 0 )
	{
		Log::Warn("'skyParms' missing parameter in shader '%s'", shader.name );
		return;
	}

	shader.sky.cloudHeight = atof( token );

	if ( !shader.sky.cloudHeight )
	{
		shader.sky.cloudHeight = 512;
	}

	// innerbox
	token = COM_ParseExt2( text, false );

	if ( token[ 0 ] == 0 )
	{
		Log::Warn("'skyParms' missing parameter in shader '%s'", shader.name );
		return;
	}

	if ( strcmp( token, "-" ) )
	{
		Q_strncpyz( prefix, token, sizeof( prefix ) );

		imageParams_t imageParams = {};
		imageParams.bits = IF_NONE;
		imageParams.filterType = filterType_t::FT_DEFAULT;
		imageParams.wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;
		imageParams.minDimension = shader.imageMinDimension;
		imageParams.maxDimension = shader.imageMaxDimension;

		shader.sky.innerbox = R_FindCubeImage( prefix, imageParams );

		if ( !shader.sky.innerbox )
		{
			Log::Warn("could not find cubemap '%s' for inner skybox in shader '%s'", prefix, shader.name );
			shader.sky.innerbox = tr.blackCubeImage;
		}
	}

	tr.hasSkybox = true;
	shader.isSky = true;
}

/*
=================
ParseSort
=================
*/
static void ParseSort( const char **text )
{
	const char *token;

	token = COM_ParseExt2( text, false );

	if ( token[ 0 ] == 0 )
	{
		Log::Warn("missing sort parameter in shader '%s'", shader.name );
		return;
	}

	if ( !Q_stricmp( token, "portal" ) || !Q_stricmp( token, "subview" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_PORTAL);
	}
	else if ( !Q_stricmp( token, "sky" ) || !Q_stricmp( token, "environment" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_ENVIRONMENT_FOG);
	}
	else if ( !Q_stricmp( token, "opaque" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_OPAQUE);
	}
	else if ( !Q_stricmp( token, "decal" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_DECAL);
	}
	else if ( !Q_stricmp( token, "seeThrough" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_SEE_THROUGH);
	}
	else if ( !Q_stricmp( token, "banner" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_BANNER);
	}
	else if ( !Q_stricmp( token, "underwater" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_UNDERWATER);
	}
	else if ( !Q_stricmp( token, "far" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_FAR);
	}
	else if ( !Q_stricmp( token, "medium" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_MEDIUM);
	}
	else if ( !Q_stricmp( token, "close" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_CLOSE);
	}
	else if ( !Q_stricmp( token, "additive" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_BLEND1);
	}
	else if ( !Q_stricmp( token, "almostNearest" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_ALMOST_NEAREST);
	}
	else if ( !Q_stricmp( token, "nearest" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_NEAREST);
	}
	else if ( !Q_stricmp( token, "postProcess" ) )
	{
		shader.sort = Util::ordinal(shaderSort_t::SS_POST_PROCESS);
	}
	else
	{
		shader.sort = atof( token );
	}
}

// this table is also present in q3map

struct infoParm_t
{
	const char *name;
	int  clearSolid, surfaceFlags, contents;
};

// *INDENT-OFF*
static const infoParm_t infoParms[] =
{
	// server relevant contents

	{ "clipmissile",        1,                         0,                      CONTENTS_MISSILECLIP                   }, // impact only specific weapons (rl, gl)
	{ "water",              1,                         0,                      CONTENTS_WATER                         },
	{ "slag",               1,                         0,                      CONTENTS_SLIME                         }, // uses the CONTENTS_SLIME flag, but the shader reference is changed to 'slag'
	// to identify that this doesn't work the same as 'slime' did.

	{ "lava",               1,                         0,                      CONTENTS_LAVA                          }, // very damaging
	{ "playerclip",         1,                         0,                      CONTENTS_PLAYERCLIP                    },
	{ "monsterclip",        1,                         0,                      CONTENTS_MONSTERCLIP                   },
	{ "nodrop",             1,                         0,                      int(CONTENTS_NODROP)                   }, // don't drop items or leave bodies (death fog, lava, etc)
	{ "nonsolid",           1,                         SURF_NONSOLID,          0                                      }, // clears the solid flag

	{ "blood",              1,                         0,                      CONTENTS_WATER                         },

	// utility relevant attributes
	{ "origin",             1,                         0,                      CONTENTS_ORIGIN                        }, // center of rotating brushes

	{ "trans",              0,                         0,                      CONTENTS_TRANSLUCENT                   }, // don't eat contained surfaces
	{ "translucent",        0,                         0,                      CONTENTS_TRANSLUCENT                   }, // don't eat contained surfaces

	{ "detail",             0,                         0,                      CONTENTS_DETAIL                        }, // don't include in structural bsp
	{ "structural",         0,                         0,                      CONTENTS_STRUCTURAL                    }, // force into structural bsp even if trnas
	{ "areaportal",         1,                         0,                      CONTENTS_AREAPORTAL                    }, // divides areas
	{ "clusterportal",      1,                         0,                      CONTENTS_CLUSTERPORTAL                 }, // for bots
	{ "donotenter",         1,                         0,                      CONTENTS_DONOTENTER                    }, // for bots

	{ "donotenterlarge",    1,                         0,                      CONTENTS_DONOTENTER_LARGE              }, // for larger bots
	{ "fog",                1,                         0,                      CONTENTS_FOG                           }, // carves surfaces entering
	{ "sky",                0,                         SURF_SKY,               0                                      }, // emit light from an environment map
	{ "lightfilter",        0,                         SURF_LIGHTFILTER,       0                                      }, // filter light going through it
	{ "alphashadow",        0,                         SURF_ALPHASHADOW,       0                                      }, // test light on a per-pixel basis
	{ "hint",               0,                         SURF_HINT,              0                                      }, // use as a primary splitter

	// server attributes
	{ "slick",              0,                         SURF_SLICK,             0                                      },

	{ "noimpact",           0,                         SURF_NOIMPACT,          0                                      }, // don't make impact explosions or marks

	{ "nomarks",            0,                         SURF_NOMARKS,           0                                      }, // don't make impact marks, but still explode
	{ "nooverlays",         0,                         SURF_NOMARKS,           0                                      }, // don't make impact marks, but still explode

	{ "ladder",             0,                         SURF_LADDER,            0                                      },

	{ "nodamage",           0,                         SURF_NODAMAGE,          0                                      },

	{ "monsterslick",       0,                         SURF_MONSTERSLICK,      0                                      }, // surf only slick for monsters

	{ "glass",              0,                         SURF_GLASS,             0                                      }, //----(SA) added
	{ "splash",             0,                         SURF_SPLASH,            0                                      }, //----(SA) added

	// steps
	{ "metal",              0,                         SURF_METAL,             0                                      },
	{ "metalsteps",         0,                         SURF_METAL,             0                                      },

	{ "nosteps",            0,                         SURF_NOSTEPS,           0                                      },
	{ "woodsteps",          0,                         SURF_WOOD,              0                                      },
	{ "grasssteps",         0,                         SURF_GRASS,             0                                      },
	{ "gravelsteps",        0,                         SURF_GRAVEL,            0                                      },
	{ "carpetsteps",        0,                         SURF_CARPET,            0                                      },
	{ "snowsteps",          0,                         SURF_SNOW,              0                                      },
	{ "roofsteps",          0,                         SURF_ROOF,              0                                      }, // tile roof

	{ "rubble",             0,                         SURF_RUBBLE,            0                                      },

	// drawsurf attributes
	{ "nodraw",             0,                         SURF_NODRAW,            0                                      }, // don't generate a drawsurface (or a lightmap)
	{ "pointlight",         0,                         SURF_POINTLIGHT,        0                                      }, // sample lighting at vertexes
	{ "nolightmap",         0,                         SURF_NOLIGHTMAP,        0                                      }, // don't generate a lightmap
	{ "nodlight",           0,                         0,                      0                                      }, // OBSOLETE: don't ever add dynamic lights

	// monster ai
	{ "monsterslicknorth",  0,                         SURF_MONSLICK_N,        0                                      },
	{ "monsterslickeast",   0,                         SURF_MONSLICK_E,        0                                      },
	{ "monsterslicksouth",  0,                         SURF_MONSLICK_S,        0                                      },
	{ "monsterslickwest",   0,                         SURF_MONSLICK_W,        0                                      },

	// unsupported Doom3 surface types for sound effects and blood splats
	{ "metal",              0,                         SURF_METAL,             0                                      },

	{ "stone",              0,                         0,                      0                                      },
	{ "wood",               0,                         SURF_WOOD,              0                                      },
	{ "cardboard",          0,                         0,                      0                                      },
	{ "liquid",             0,                         0,                      0                                      },
	{ "glass",              0,                         0,                      0                                      },
	{ "plastic",            0,                         0,                      0                                      },
	{ "ricochet",           0,                         0,                      0                                      },
	{ "surftype10",         0,                         0,                      0                                      },
	{ "surftype11",         0,                         0,                      0                                      },
	{ "surftype12",         0,                         0,                      0                                      },
	{ "surftype13",         0,                         0,                      0                                      },
	{ "surftype14",         0,                         0,                      0                                      },
	{ "surftype15",         0,                         0,                      0                                      },

	// other unsupported Doom3 surface types
	{ "trigger",            0,                         0,                      0                                      },
	{ "flashlight_trigger", 0,                         0,                      0                                      },
	{ "aassolid",           0,                         0,                      0                                      },
	{ "aasobstacle",        0,                         0,                      0                                      },
	{ "nullNormal",         0,                         0,                      0                                      },
	{ "discrete",           0,                         0,                      0                                      },
};
// *INDENT-ON*

/*
===============
ParseSurfaceParm

surfaceparm <name>
===============
*/
static bool SurfaceParm( const char *token )
{
	int numInfoParms = ARRAY_LEN( infoParms );
	int i;

	for ( i = 0; i < numInfoParms; i++ )
	{
		if ( !Q_stricmp( token, infoParms[ i ].name ) )
		{
			shader.surfaceFlags |= infoParms[ i ].surfaceFlags;
			shader.contentFlags |= infoParms[ i ].contents;
			return true;
		}
	}

	return false;
}

static void ParseSurfaceParm( const char **text )
{
	const char *token = COM_ParseExt2( text, false );
	SurfaceParm( token );
}

/*
=================
ParseShader

The current text pointer is at the explicit text definition of the
shader.  Parse it into the global shader variable.  Later functions
will optimize it.
=================
*/
static bool ParseShader( const char *_text )
{
	const char **text;
	const char *token;
	int  s;

	s = 0;
	text = &_text;

	token = COM_ParseExt2( text, true );

	if ( token[ 0 ] != '{' )
	{
		Log::Warn("no preceding '}' in shader %s", shader.name );
		return false;
	}

	while ( true )
	{
		token = COM_ParseExt2( text, true );

		if ( !token[ 0 ] )
		{
			Log::Warn("no concluding '}' in shader %s", shader.name );
			return false;
		}

		// end of shader definition
		if ( token[ 0 ] == '}' )
		{
			break;
		}
		// stage definition
		else if ( token[ 0 ] == '{' )
		{
			if ( s >= ( MAX_SHADER_STAGES - 1 ) )
			{
				Log::Warn("too many stages in shader %s", shader.name );
				return false;
			}

			shaderStage_t *stage = &stages[ s ];

			if ( !ParseStage( stage, text ) )
			{
				return false;
			}

			stage->active = true;

			/* Examples of shaders with light styles, those
			shaders are generated by the q3map2 map compiler.

			All color stages with “tcGen lightmap” are light style
			stages, they use direct path to the light map image
			like “maps/<nam>/lm_<num>[ext]”.

			Light map stages using the “$lightmap” image don't
			need “tcGen lightmap” and then such stage with such
			keyword should be light style stages, but some legacy
			materials may needlessly set “tcGen lightmap” on the
			light map stage so we need more data to not wrongly
			detect them as light style stages.

			The textures/conveyor/p_01_s material in cruz map
			is a good example of such light map stage with
			useless “tcGen lightmap” keywords.

			It looks like all light style stages use
			the “blendFunc GL_SRC_ALPHA GL_ONE” blend operation,
			so we use that to know if a light map stage is a
			light map one or a light style one.

			gloom2/673A7C895A24ECE49A80324DF57B0CC1
			{ // Q3Map2 defaulted
				{
					map $lightmap
					rgbGen identity
				}

				// Q3Map2 custom lightstyle stage(s)
				{
					map $lightmap
					blendFunc GL_SRC_ALPHA GL_ONE
					rgbGen wave noise 1 .75 1.6 4.2 // style 2
					tcGen lightmap
					tcMod transform 1 0 0 1 -0.50000 0.50000
				}

				{
					map textures/gloom2/barrel_rst_top.tga
					blendFunc GL_DST_COLOR GL_ZERO
					rgbGen identity
				}
			}

			gloom2/2EAD7A05FC2E0F822744BCF91FA7176D
			{ // Q3Map2 defaulted
				{
					map $lightmap
					rgbGen identity
				}

				// Q3Map2 custom lightstyle stage(s)
				{
					map $lightmap
					blendFunc GL_SRC_ALPHA GL_ONE
					rgbGen wave noise 1 .75 1.6 4.2 // style 2
					tcGen lightmap
					tcMod transform 1 0 0 1 0.06250 0.00000
				}
				{
					map $lightmap
					blendFunc GL_SRC_ALPHA GL_ONE
					rgbGen wave noise 1 .5 3.7 4.9 // style 3
					tcGen lightmap
					tcMod transform 1 0 0 1 0.12500 0.00000
				}
				{
					map $lightmap
					blendFunc GL_SRC_ALPHA GL_ONE
					rgbGen wave noise 1 1 2.6 1.3 // style 4
					tcGen lightmap
					tcMod transform 1 0 0 1 -0.06250 0.04395
				}

				{
					map textures/gloom2/e8clangfloor05c.tga
					blendFunc GL_DST_COLOR GL_ZERO
					rgbGen identity
				}
			}

			gloom2/1AE453216BF2550BD776F35268418499
			{ // Q3Map2 defaulted
				{
					map $lightmap
					rgbGen identity
				}

				// Q3Map2 custom lightstyle stage(s)
				{
					map maps/gloom2/lm_0000.tga
					blendFunc GL_SRC_ALPHA GL_ONE
					rgbGen wave noise 1 .5 3.7 4.9 // style 3
					tcGen lightmap
					tcMod transform 1 0 0 1 0.00000 0.50000
				}

				{
					map textures/gloom2/rust_1.tga
					blendFunc GL_DST_COLOR GL_ZERO
					rgbGen identity
				}
			}

			Light style map stages using the $lightmap keyword
			should not be rendered with Render_lightmap but with
			Render_generic otherwise we would blend dynamic lights
			on every light style map while such dynamic lights are
			already blended on the first lightmap stage. So we should
			not keep the ST_LIGHTMAP type but set on such light style
			map stage a specific ST_LIGHTSTYLEMAP type to recognize
			them and route the render properly. We can't set them
			to ST_COLORMAP type because we need to remember the image
			has to be found at render time.

			Light style map stages using the direct image path
			are already rendered by Render_lightmap as they have
			a ST_COLORMAP type so we don't have to change them. */

			bool lightStyleBlend = ( stage->stateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_SRC_ALPHA
				&& ( stage->stateBits & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE;

			if ( stage->type == stageType_t::ST_LIGHTMAP
				&& stage->tcGen_Lightmap
				&& lightStyleBlend )
			{
				stage->type = stageType_t::ST_STYLELIGHTMAP;
			}
			else if ( stage->type != stageType_t::ST_LIGHTMAP
				&& stage->tcGen_Lightmap )
			{
				stage->type = stageType_t::ST_STYLECOLORMAP;
			}

			/* Light styles are only compatible with light mapping
			as light styles are just extra light map stages with
			special effects.

			When light mapping is disabled, the first light map stage
			can be rendered using vertex lighting, but the light style
			stages should not be rendered.

			So we disable light style stages when light mapping
			is disabled. */
			if ( tr.lightMode != lightMode_t::MAP || r_lightStyles->integer == 0 )
			{
				switch ( stage->type )
				{
					case stageType_t::ST_STYLELIGHTMAP:
					case stageType_t::ST_STYLECOLORMAP:
						stage->active = false;
						break;

					default:
						break;
				}
			}

			s++;
			continue;
		}
		// skip stuff that only the QuakeEdRadient needs
		else if ( !Q_strnicmp( token, "qer", 3 ) )
		{
			SkipRestOfLine( text );
			continue;
		}
		// skip description
		else if ( !Q_stricmp( token, "description" ) )
		{
			SkipRestOfLine( text );
			continue;
		}
		// skip renderbump
		else if ( !Q_stricmp( token, "renderbump" ) )
		{
			SkipRestOfLine( text );
			continue;
		}
		// skip guiSurf
		else if ( !Q_stricmp( token, "guiSurf" ) )
		{
			SkipRestOfLine( text );
			continue;
		}
		// sun parms
		else if ( !Q_stricmp( token, "q3map_sun" ) )
		{
			float a, b;

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parm for 'q3map_sun' keyword in shader '%s'", shader.name );
				continue;
			}

			tr.sunLight[ 0 ] = atof( token );

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parm for 'q3map_sun' keyword in shader '%s'", shader.name );
				continue;
			}

			tr.sunLight[ 1 ] = atof( token );

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parm for 'q3map_sun' keyword in shader '%s'", shader.name );
				continue;
			}

			tr.sunLight[ 2 ] = atof( token );

			VectorNormalize( tr.sunLight );

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parm for 'q3map_sun' keyword in shader '%s'", shader.name );
				continue;
			}

			a = atof( token );
			VectorScale( tr.sunLight, a, tr.sunLight );

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parm for 'q3map_sun' keyword in shader '%s'", shader.name );
				continue;
			}

			a = DEG2RAD( atof( token ) );

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parm for 'q3map_sun' keyword in shader '%s'", shader.name );
				continue;
			}

			b = DEG2RAD( atof( token ) );

			tr.sunDirection[ 0 ] = cosf( a ) * cosf( b );
			tr.sunDirection[ 1 ] = sinf( a ) * cosf( b );
			tr.sunDirection[ 2 ] = sinf( b );
			continue;
		}
		// noShadows
		else if ( !Q_stricmp( token, "noShadows" ) )
		{
			shader.noShadows = true;
			continue;
		}
		// translucent
		else if ( !Q_stricmp( token, "translucent" ) )
		{
			shader.translucent = true;
			continue;
		}
		// forceOpaque
		else if ( !Q_stricmp( token, "forceOpaque" ) )
		{
			shader.forceOpaque = true;
			continue;
		}
		// forceSolid
		else if ( !Q_stricmp( token, "forceSolid" ) || !Q_stricmp( token, "solid" ) )
		{
			continue;
		}
		else if ( !Q_stricmp( token, "deformVertexes" ) || !Q_stricmp( token, "deform" ) )
		{
			ParseDeform( text );
			continue;
		}
		else if ( !Q_stricmp( token, "tesssize" ) )
		{
			SkipRestOfLine( text );
			continue;
		}

		// skip noFragment
		if ( !Q_stricmp( token, "noFragment" ) )
		{
			continue;
		}
		// skip stuff that only the q3map needs
		else if ( !Q_strnicmp( token, "q3map", 5 ) )
		{
			SkipRestOfLine( text );
			continue;
		}
		// skip stuff that only q3map or the server needs
		else if ( !Q_stricmp( token, "surfaceParm" ) )
		{
			ParseSurfaceParm( text );
			continue;
		}
		// no mip maps
		else if ( !Q_stricmp( token, "nomipmap" ) || !Q_stricmp( token, "nomipmaps" ) )
		{
			shader.filterType = filterType_t::FT_LINEAR;
			shader.noPicMip = true;
			continue;
		}
		// no picmip adjustment
		else if ( !Q_stricmp( token, "nopicmip" ) )
		{
			shader.noPicMip = true;
			continue;
		}
		// fit screen adjustment
		else if ( !Q_stricmp( token, "fitScreen" ) )
		{
			shader.fitScreen = true;
			continue;
		}
		// imageMinDimension enforcement
		else if ( !Q_stricmp( token, "imageMinDimension" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] )
			{
				shader.imageMinDimension = atoi( token );
			}

			continue;
		}
		// imageMaxDimension enforcement
		else if ( !Q_stricmp( token, "imageMaxDimension" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] )
			{
				shader.imageMaxDimension = atoi( token );
			}

			continue;
		}
		// RF, allow each shader to permit compression if available (removed option)
		else if ( !Q_stricmp( token, "allowcompress" ) )
		{
			continue;
		}
		// (removed option)
		else if ( !Q_stricmp( token, "nocompress" ) )
		{
			continue;
		}
		// polygonOffset
		else if ( !Q_stricmp( token, "polygonOffset" ) )
		{
			shader.polygonOffset = true;

			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] )
			{
				shader.polygonOffsetValue = atof( token );
			}

			continue;
		}
		// relief mapping
		else if ( !Q_stricmp( token, "parallax" ) )
		{
			/* Legacy lone “parallax” XreaL material keyword was never used,
			its purpose was to enable relief mapping for the current shader.

			The engine relied on it to know the heightmap was stored in
			normalmap alpha channel but there was no other storage options.

			The engine also had to rely on it to know that heightmap
			was stored in normalmap because of a design flaw that used
			depthmap (black is up) instead of heightmap (black is down).
			I meant a missing depthmap would cause a wrong displacement
			if not discarded.

			So that option was there to tell the engine to not discard
			heightmap when the artist knew it was not flat.

			Since the engine knows how to automatically load and enable
			heightmap stored in normalmap if detection is required, and
			has not problem with flat heightmap in any way because of a
			better design, this keyword is now pretty useless.

			Artists can use “normalMap” material keyword for normalmap
			without heightmap, and can use “normalHeightMap” keyword for
			normalmap with heightmap in alpha channel.

			Developers can use ParseNormalMapDetectHeightMap function to detect
			heightmap in normal map alpha channel, please never add a material
			keyword for this behaviour because some texture format can lead to
			wrong detection when GL_ARB_texture_compression_rgtc extension is
			missing. Only use this function for compatibility layers for games
			that relies on detection (and were already unable to use such
			texture format because of that). */

			Log::Warn("deprecated keyword '%s' in shader '%s', this was a workaround for a design flaw, there is no need for it", token, shader.name );

			SkipRestOfLine( text );
			continue;
		}
		else if ( r_dpMaterial.Get() && !Q_stricmp( token, "dpoffsetmapping" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( !Q_stricmp( token, "none" ) )
			{
				shader.disableReliefMapping = true;
			}
			else if ( !Q_stricmp( token, "disable" ) )
			{
				shader.disableReliefMapping = true;
			}
			else if ( !Q_stricmp( token, "off" ) )
			{
				shader.disableReliefMapping = true;
			}
			else if ( !Q_stricmp( token, "default" ) )
			{
				// do nothing more
			}
			else if ( !Q_stricmp( token, "normal" ) )
			{
				// do nothing more (same as default)
			}
			else if ( !Q_stricmp( token, "linear" ) )
			{
				// not implemented yet
				Log::Warn("unsupported parm for dpoffsetmapping keyword in shader '%s'", shader.name );
			}
			else if ( !Q_stricmp( token, "relief" ) )
			{
				// the only implemented algorithm
				// hence the default
				// do nothing more
			}
			else if ( !Q_stricmp( token, "-" ) )
			{
				// do nothing, that's just a filler
			}
			else
			{
				Log::Warn("invalid parm for dpoffsetmapping keyword in shader '%s'", shader.name );
				SkipRestOfLine( text );
				continue;
			}

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parm for dpoffsetmapping keyword in shader '%s'", shader.name );
				continue;
			}

			shader.reliefDepthScale = atof( token );

			token = COM_ParseExt2( text, false );

			// dpoffsetmapping - 2
			if ( !token[ 0 ] )
			{
				continue;
			}

			// dpoffsetmapping - 2 match8 65
			float off;
			float div;

			if ( !Q_stricmp( token, "bias" ) )
			{
				off = 0.0f;
				div = 1.0f;
			}
			else if ( !Q_stricmp( token, "match" ) )
			{
				off = 1.0f;
				div = 1.0f;
			}
			else if ( !Q_stricmp( token, "match8" ) )
			{
				off = 1.0f;
				div = 255.0f;
			}
			else if ( !Q_stricmp( token, "match16" ) )
			{
				off = 1.0f;
				div = 65535.0f;
			}
			else
			{
				Log::Warn("invalid parm for dpoffsetmapping keyword in shader '%s'", shader.name );
				SkipRestOfLine( text );
				continue;
			}

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parm for dpoffsetmapping keyword in shader '%s'", shader.name );
				continue;
			}

			float bias = atof( token );
			shader.reliefOffsetBias = off - bias / div;
			continue;
		}
		// entityMergable, allowing sprite surfaces from multiple entities
		// to be merged into one batch.  This is a savings for smoke
		// puffs and blood, but can't be used for anything where the
		// shader calcs (not the surface function) reference the entity color or scroll
		else if ( !Q_stricmp( token, "entityMergable" ) )
		{
			shader.entityMergable = true;
			continue;
		}
		// fogParms
		else if ( !Q_stricmp( token, "fogParms" ) )
		{
			/*
			Log::Warn("fogParms keyword not supported in shader '%s'", shader.name);
			SkipRestOfLine(text);

			*/

			if ( !ParseVector( text, 3, shader.fogParms.color ) )
			{
				return false;
			}

			token = COM_ParseExt2( text, false );

			if ( !token[ 0 ] )
			{
				Log::Warn("missing parm for 'fogParms' keyword in shader '%s'", shader.name );
				continue;
			}

			shader.fogParms.depthForOpaque = atof( token );

			shader.sort = Util::ordinal(shaderSort_t::SS_FOG);

			// skip any old gradient directions
			SkipRestOfLine( text );
			continue;
		}
		// noFog
		else if ( !Q_stricmp( token, "noFog" ) )
		{
			shader.noFog = true;
			continue;
		}
		// portal
		else if ( !Q_stricmp( token, "portal" ) )
		{
			shader.sort = Util::ordinal(shaderSort_t::SS_PORTAL);
			shader.isPortal = true;

			/* Quake3 shaders required the portalRange to be set at stage level
			as “alphaGen portal <range>”, we choose to make it totally optional,
			allowing to set it there or fallback to the default value if is set
			nowhere. Allowing to set the portal range ther allows to set the
			range for portals that don't blend with textures. */
			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] )
			{
				shader.portalRange = atof( token );
			}
			continue;
		}
		// portal or mirror
		else if ( !Q_stricmp( token, "mirror" ) )
		{
			shader.sort = Util::ordinal(shaderSort_t::SS_PORTAL);
			shader.isPortal = true;

			/* Mirrors don't use any portal range, probably because they always
			render the room the mirror is in, and then the renderer processes the
			same geometry that would be processed with or without mirror. In case
			there is more than a mirror visible, there is a recursion limit. */
			continue;
		}
		// skyparms <cloudheight> <outerbox> <innerbox>
		else if ( !Q_stricmp( token, "skyparms" ) )
		{
			ParseSkyParms( text );
			continue;
		}
		// light <value> determines flaring in q3map, not needed here
		else if ( !Q_stricmp( token, "light" ) )
		{
			token = COM_ParseExt2( text, false );
			continue;
		}
		// cull <face>
		else if ( !Q_stricmp( token, "cull" ) )
		{
			token = COM_ParseExt2( text, false );

			if ( token[ 0 ] == 0 )
			{
				Log::Warn("missing cull parms in shader '%s'", shader.name );
				continue;
			}

			if ( !Q_stricmp( token, "none" ) || !Q_stricmp( token, "twoSided" ) || !Q_stricmp( token, "disable" ) )
			{
				shader.cullType = CT_TWO_SIDED;
			}
			else if ( !Q_stricmp( token, "back" ) || !Q_stricmp( token, "backside" ) || !Q_stricmp( token, "backsided" ) )
			{
				shader.cullType = CT_BACK_SIDED;
			}
			else
			{
				Log::Warn("invalid cull parm '%s' in shader '%s'", token, shader.name );
			}

			continue;
		}
		// twoSided
		else if ( !Q_stricmp( token, "twoSided" ) )
		{
			shader.cullType = CT_TWO_SIDED;
			continue;
		}
		// backSided
		else if ( !Q_stricmp( token, "backSided" ) )
		{
			shader.cullType = CT_BACK_SIDED;
			continue;
		}
		// clamp, edgeClamp etc.
		else if ( ParseClampType( token, &shader.wrapType ) )
		{
			continue;
		}
		// sort
		else if ( !Q_stricmp( token, "sort" ) )
		{
			ParseSort( text );
			continue;
		}
		// ydnar: implicit default mapping to eliminate redundant/incorrect explicit shader stages
		else if ( !Q_strnicmp( token, "implicit", 8 ) )
		{
			bool GL1 = false;

			// set implicit mapping state
			if ( !Q_stricmp( token, "implicitBlend" ) )
			{
				implicitStateBits = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
				implicitCullType = CT_TWO_SIDED;
			}
			else if ( !Q_stricmp( token, "implicitMask" ) )
			{
				implicitStateBits = GLS_DEPTHMASK_TRUE | GLS_ATEST_GE_128;
				implicitCullType = CT_TWO_SIDED;
			}
			else if ( !Q_stricmp( token, "implicitMapGL1" ) )
			{
				GL1 = true;
			}
			else // "implicitMap"
			{
				implicitStateBits = GLS_DEPTHMASK_TRUE;
				implicitCullType = CT_FRONT_SIDED;
			}

			// get image
			token = COM_ParseExt( text, false );

			if ( !GL1 )
			{
				if ( token[ 0 ] != '\0' )
				{
					Q_strncpyz( implicitMap, token, sizeof( implicitMap ) );
				}
				else
				{
					implicitMap[ 0 ] = '-';
					implicitMap[ 1 ] = '\0';
				}
			}

			continue;
		}
		// diffuseMap <image>
		else if ( !Q_stricmp( token, "diffuseMap" ) )
		{
			Log::Warn("deprecated idTech4 standalone stage '%s' in shader '%s', better move this line and pack related textures within one single curly bracket stage pair", token, shader.name );
			ParseLegacyDiffuseStage( &stages[ s ], text );
			s++;
			continue;
		}
		// normalMap <image>
		else if ( !Q_stricmp( token, "normalMap" ) )
		{
			Log::Warn("deprecated idTech4 standalone stage '%s' in shader '%s', better move this line and pack related textures within one single curly bracket stage pair", token, shader.name );
			ParseLegacyNormalStage( &stages[ s ], text );
			SetNormalFormat( &stages[ s ], dxNormalFormat );
			s++;
			continue;
		}
		// bumpMap <image>
		else if ( !Q_stricmp( token, "bumpMap" ) )
		{
			Log::Warn("deprecated idTech4 standalone stage '%s' in shader '%s', better use 'normalMap' keyword and move this line and pack related textures within one single curly bracket stage pair", token, shader.name );
			ParseLegacyNormalStage( &stages[ s ], text );
			SetNormalFormat( &stages[ s ], dxNormalFormat );
			s++;
			continue;
		}
		// specularMap <image>
		else if ( !Q_stricmp( token, "specularMap" ) )
		{
			Log::Warn("deprecated idTech4 standalone stage '%s' in shader '%s', better move this line and pack related textures within one single curly bracket stage pair", token, shader.name );
			ParseLegacySpecularStage( &stages[ s ], text );
			s++;
			continue;
		}
		// physicalMap <image> did not exist
		// glowMap <image>
		else if ( !Q_stricmp( token, "glowMap" ) )
		{
			Log::Warn("deprecated idTech4 standalone stage '%s' in shader '%s', better move this line and pack related textures within one single curly bracket stage pair", token, shader.name );
			ParseLegacyGlowStage( &stages[ s ], text );
			s++;
			continue;
		}
		// reflectionMap <image>
		else if ( !Q_stricmp( token, "reflectionMap" ) )
		{
			Log::Warn("deprecated idTech4 standalone stage '%s' in shader '%s', better move this line and pack related textures within one single curly bracket stage pair", token, shader.name );
			ParseReflectionStage( &stages[ s ], text );
			s++;
			continue;
		}
		// reflectionMapBlended <image>
		else if ( !Q_stricmp( token, "reflectionMapBlended" ) )
		{
			Log::Warn("deprecated idTech4 standalone stage '%s' in shader '%s', better move this line and pack related textures within one single curly bracket stage pair", token, shader.name );
			ParseReflectionStageBlended( &stages[ s ], text );
			s++;
			continue;
		}
		else if ( !Q_stricmp( token, "dpreflectcube" ) )
		{
			if ( r_dpMaterial.Get() )
			{
				ParseReflectionStageBlended( &stages[ s ], text );
				s++;
			}
			else
			{
				Log::Warn("disabled DarkPlaces shader parameter '%s' in '%s'", token, shader.name );
			}
			continue;
		}
		// lightFalloffImage <image>
		else if ( !Q_stricmp( token, "lightFalloffImage" ) )
		{
			ParseLightFalloffImage( &stages[ s ], text );
			s++;
			continue;
		}
		// Doom 3 DECAL_MACRO
		else if ( !Q_stricmp( token, "DECAL_MACRO" ) )
		{
			shader.polygonOffset = true;
			shader.polygonOffsetValue = 1;
			shader.sort = Util::ordinal(shaderSort_t::SS_DECAL);
			SurfaceParm( "discrete" );
			SurfaceParm( "noShadows" );
			continue;
		}
		// Prey DECAL_ALPHATEST_MACRO
		else if ( !Q_stricmp( token, "DECAL_ALPHATEST_MACRO" ) )
		{
			// what's different?
			shader.polygonOffset = true;
			shader.polygonOffsetValue = 1;
			shader.sort = Util::ordinal(shaderSort_t::SS_DECAL);
			SurfaceParm( "discrete" );
			SurfaceParm( "noShadows" );
			continue;
		}
		// when <state> <shader name>
		else if ( !Q_stricmp( token, "when" ) )
		{
			int i;
			const char *p;
			int index = 0;

			token = COM_ParseExt2( text, false );

			for ( i = 1, p = whenTokens; i < MAX_ALTSHADERS && *p; ++i, p += strlen( p ) + 1 )
			{
				if ( !Q_stricmp( token, p ) )
				{
					index = i;
					break;
				}
			}

			if ( index == 0 )
			{
				Log::Warn("unknown parameter '%s' for 'when' in '%s'", token, shader.name );
			}
			else
			{
				int tokenLen;

				token = COM_ParseExt( text, false );

				if ( !token[ 0 ] )
				{
					Log::Warn("missing shader name for 'when'" );
					continue;
				}

				tokenLen = strlen( token ) + 1;
				shader.altShader[ index ].index = 0;
				shader.altShader[ index ].name = ( char* )ri.Hunk_Alloc( sizeof( char ) * tokenLen, ha_pref::h_low );
				Q_strncpyz( shader.altShader[ index ].name, token, tokenLen );
			}
		}
		else if ( SurfaceParm( token ) )
		{
			continue;
		}
		else if ( !Q_strnicmp( token, "dp", 2 ) )
		{
			if ( r_dpMaterial.Get() )
			{
				Log::Warn("unknown DarkPlaces shader parameter '%s' in '%s'", token, shader.name );
			}
			else
			{
				Log::Warn("disabled DarkPlaces shader parameter '%s' in '%s'", token, shader.name );
			}
			SkipRestOfLine( text );
			continue;
		}
		else
		{
			Log::Warn("unknown general shader parameter '%s' in '%s'", token, shader.name );
			SkipRestOfLine( text );
			continue;
		}
	}

	// ignore shaders that don't have any stages, unless it is a sky or fog
	if ( s == 0 && !shader.forceOpaque && !shader.isSky && !( shader.contentFlags & CONTENTS_FOG ) && implicitMap[ 0 ] == '\0' )
	{
		return false;
	}

	return true;
}

/*
========================================================================================

SHADER OPTIMIZATION AND FOGGING

========================================================================================
*/

// Group all active stages to the begining of the array.
static void GroupActiveStages()
{
	size_t numActiveStages = 0;

	for ( size_t i = 0; i < numStages; i++ )
	{
		if ( !stages[ i ].active )
		{
			continue;
		}

		if ( i != numActiveStages )
		{
			stages[ numActiveStages ] = stages[ i ];

			stages[ i ].active = false;
		}

		numActiveStages++;
	}

	numStages = numActiveStages;
}

/*
================
CollapseMultitexture
=================
*/
// *INDENT-OFF*
static void CollapseStages()
{
	int colorStage = -1;
	int diffuseStage = -1;
	int normalStage = -1;
	int specularStage = -1;
	int physicalStage = -1;
	int reflectionStage = -1;
	int lightStage = -1;
	int glowStage = -1;

	/* DarkPlaces only supports one kind of lightmap blend:
	   https://gitlab.com/xonotic/darkplaces/blob/324a5329d33ef90df59e6488abce6433d90ac04c/model_shared.c#L1886-1887

	So we may implement specific code when stages[ i ].dpMaterial
	is true to hide bugs that may be hidden by the incomplete
	DarPlaces implementation, in a similar way to r_dpBlend. */

	for ( int i = 0, step = 0; i < MAX_SHADER_STAGES && step < 2; i++ )
	{
		if ( !stages[ i ].active )
		{
			continue;
		}

		bool isLightStage = false;
		bool isColorStage = false;
		bool isCollapseColorStage = false;

		switch ( stages[ i ].type )
		{
			case stageType_t::ST_LIGHTMAP:
				isLightStage = true;
				break;
			case stageType_t::ST_COLORMAP:
				isColorStage = true;
				break;
			case stageType_t::ST_COLLAPSE_COLORMAP:
				isCollapseColorStage = true;
				break;
			case stageType_t::ST_NORMALMAP:
			case stageType_t::ST_HEIGHTMAP:
			case stageType_t::ST_PHYSICALMAP:
			case stageType_t::ST_SPECULARMAP:
				continue;
			default:
				break;
		}

		bool rgbGen_identity =
			stages[ i ].rgbGen == colorGen_t::CGEN_IDENTITY
			|| stages[ i ].rgbGen == colorGen_t::CGEN_IDENTITY_LIGHTING;

		bool alphaGen_identity =
			stages[ i ].alphaGen == alphaGen_t::AGEN_IDENTITY;

		bool blendFunc_add =
			( stages[ i ].stateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_ONE
			&& ( stages[ i ].stateBits & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE;

		bool blendFunc_filter =
			( stages[ i ].stateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_DST_COLOR
			&& ( stages[ i ].stateBits & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ZERO;

		// Do not collapse lightmap with custom depthFunc.
		bool depthFunc_lequal =
			( stages[ i ].stateBits & GLS_DEPTHFUNC_BITS ) == 0;

		bool tcGen_Environment = stages[ i ].tcGen_Environment;

		if ( step == 0 )
		{
			if ( ( isColorStage || isCollapseColorStage )
				&& rgbGen_identity
				&& alphaGen_identity
				&& !tcGen_Environment )
			{
				colorStage = i;
				step++;
				continue;
			}
			else if ( isLightStage
				&& rgbGen_identity
				&& alphaGen_identity
				&& blendFunc_filter )
			{
				lightStage = i;
				step++;
				continue;
			}
		}
		else if ( step == 1 )
		{
			if ( isLightStage
				&& rgbGen_identity
				&& alphaGen_identity
				&& depthFunc_lequal )
			{
				lightStage = i;
				step++;
				continue;
			}
			else if ( ( isColorStage || isCollapseColorStage )
				&& rgbGen_identity
				&& alphaGen_identity
				&& blendFunc_filter
				&& colorStage != -1 )
			{
				colorStage = i;
				step++;
				continue;
			}
		}
		else if ( step == 2 )
		{
			if ( isColorStage
				&& rgbGen_identity
				&& alphaGen_identity
				&& blendFunc_add
				&& !tcGen_Environment )
			{
				bool hasGlowMap = stages[ colorStage ].bundle[ TB_GLOWMAP ].image[ 0 ] != nullptr;

				if ( hasGlowMap )
				{
					break;
				}
				else
				{
					glowStage = i;
					step++;
					continue;
				}
			}
		}
		break;
	}

	if ( colorStage != -1 && lightStage != -1 )
	{
		switch ( stages[ colorStage ].type )
		{
			case stageType_t::ST_COLORMAP:
				// Turn color stage as diffuse stage.
				// Merge light stage with diffuse stage.
				stages[ colorStage ].type = stageType_t::ST_DIFFUSEMAP;
				colorStage = -1;

				// Disable light stage since it's merged.
				stages[ lightStage ].active = false;

				// The glow stage will be merged later.
				if ( glowStage != -1 )
				{
					stages[ glowStage ].type = stageType_t::ST_GLOWMAP;
				}

				// The glow stage will be rediscovered later.
				glowStage = -1;
				break;

			case stageType_t::ST_COLLAPSE_COLORMAP:
				// Turn collapse color stage as collapse diffuse stage.
				// Merge light stage with color diffuse stage.
				stages[ colorStage ].type = stageType_t::ST_COLLAPSE_DIFFUSEMAP;
				colorStage = -1;

				// Disable light stage since it's merged.
				stages[ lightStage ].active = false;

				// The can't be a glowStage to merge.
				break;

			default:
				break;
		}
	}
	else
	{
		lightStage = -1;
	}

	for ( int i = 0; i < MAX_SHADER_STAGES; i++ )
	{
		if ( !stages[ i ].active )
		{
			continue;
		}
		else if ( stages[ i ].type == stageType_t::ST_DIFFUSEMAP )
		{
			if ( diffuseStage != -1 )
			{
				Log::Warn( "more than one diffuse map stage in shader '%s'", shader.name );
			}
			else
			{
				diffuseStage = i;
			}
		}
		else if ( stages[ i ].type == stageType_t::ST_NORMALMAP )
		{
			if ( normalStage != -1 )
			{
				Log::Warn( "more than one normal map stage in shader '%s'", shader.name );
			}
			else
			{
				normalStage = i;
			}
		}
		else if ( stages[ i ].type == stageType_t::ST_SPECULARMAP )
		{
			if ( specularStage != -1 )
			{
				Log::Warn( "more than one specular map stage in shader '%s'", shader.name );
			}
			else
			{
				specularStage = i;
			}
		}
		else if ( stages[ i ].type == stageType_t::ST_PHYSICALMAP )
		{
			if ( physicalStage != -1 )
			{
				Log::Warn( "more than one physical map stage in shader '%s'", shader.name );
			}
			else
			{
				physicalStage = i;
			}
		}
		else if ( stages[ i ].type == stageType_t::ST_REFLECTIONMAP )
		{
			if ( reflectionStage != -1 )
			{
				Log::Warn( "more than one reflection map stage in shader '%s'", shader.name );
			}
			else
			{
				reflectionStage = i;
			}
		}
		else if ( stages[ i ].type == stageType_t::ST_LIGHTMAP )
		{
			if ( lightStage != -1 )
			{
				Log::Warn( "more than one lightmap stage in shader '%s'", shader.name );
			}
			else
			{
				lightStage = i;
			}
		}
		else if ( stages[ i ].type == stageType_t::ST_GLOWMAP )
		{
			if ( glowStage != -1 )
			{
				Log::Warn( "more than one glow map stage in shader '%s'", shader.name );
			}
			else
			{
				glowStage = i;
			}
		}
	}

	for ( int i = 0; i < MAX_SHADER_STAGES; i++ )
	{
		/* Store heathaze and liquid normal map in normal map slot.

		NOTE: liquidMap may be entirely redesigned in the future
		to be a variant of diffuseMap (hence supporting all others textures). */
		if ( stages[ i ].type == stageType_t::ST_HEATHAZEMAP
			|| stages[ i ].type == stageType_t::ST_LIQUIDMAP )
		{
			stages[ i ].bundle[ TB_NORMALMAP ] = stages[ i ].bundle[ TB_COLORMAP ];
			stages[ i ].bundle[ TB_COLORMAP ] = {};
		}
	}

	// note that same stage can be merged twice
	// like the normal stage being merged in both
	// reflection stage and diffuse stage

	if ( reflectionStage != -1 && normalStage != -1 )
	{
		// note that if uncollapsed reflectionStage had to be merged in another stage
		// it would have to be backed-up somewhere

		Log::Debug("found reflection collapsible stage in shader '%s':", shader.name);
		stages[ reflectionStage ].collapseType = collapseType_t::COLLAPSE_REFLECTIONMAP;
		stages[ reflectionStage ].type = stageType_t::ST_COLLAPSE_REFLECTIONMAP;

		// merge with reflection stage
		stages[ reflectionStage ].bundle[ TB_NORMALMAP ] = stages[ normalStage ].bundle[ TB_COLORMAP ];
		// disable since it's merged
		stages[ normalStage ].active = false;
	}

	if ( specularStage != -1 && physicalStage != -1 )
	{
		Log::Warn("Supposedly you shouldn't have both specularMap and physicalMap (in shader '%s')?", shader.name);
		// keep specular stage
		stages[ physicalStage ].active = false;
		physicalStage = -1;
	}

	if ( diffuseStage != -1
		&& ( specularStage != -1
			|| normalStage != -1
			|| specularStage != -1
			|| physicalStage != -1
			|| glowStage != -1 ) )
	{
		if ( physicalStage != -1 )
		{
			Log::Debug("found PBR lighting collapsible stage in shader '%s'", shader.name);
			stages[ diffuseStage ].collapseType = collapseType_t::COLLAPSE_PBR;
		}
		else
		{
			Log::Debug("found Phong lighting collapsible stage in shader '%s'", shader.name);
			stages[ diffuseStage ].collapseType = collapseType_t::COLLAPSE_PHONG;
		}

		if ( normalStage != -1 )
		{
			// merge with diffuse stage
			stages[ diffuseStage ].bundle[ TB_NORMALMAP ] = stages[ normalStage ].bundle[ TB_COLORMAP ];

			stages[ diffuseStage ].normalIntensityExp = stages[ normalStage ].normalIntensityExp;

			for ( int i = 0; i < 3; i++ )
			{
				stages[ diffuseStage ].normalFormat[ i ] = stages[ normalStage ].normalFormat[ i ];
				stages[ diffuseStage ].normalScale[ i ] = stages[ normalStage ].normalScale[ i ];
			}

			stages[ diffuseStage ].hasHeightMapInNormalMap = stages[ normalStage ].hasHeightMapInNormalMap;

			// disable since it's merged
			stages[ normalStage ].active = false;
		}

		if ( specularStage != -1 )
		{
			// merge with diffuse stage
			stages[ diffuseStage ].bundle[ TB_SPECULARMAP ] = stages[ specularStage ].bundle[ TB_COLORMAP ];
			stages[ diffuseStage ].specularExponentMin = stages[ specularStage ].specularExponentMin;
			stages[ diffuseStage ].specularExponentMax = stages[ specularStage ].specularExponentMax;
			// disable since it's merged
			stages[ specularStage ].active = false;
		}

		if ( physicalStage != -1 )
		{
			// merge with diffuse stage
			stages[ diffuseStage ].bundle[ TB_PHYSICALMAP ] = stages[ physicalStage ].bundle[ TB_COLORMAP ];
			// disable since it's merged
			stages[ physicalStage ].active = false;
		}

		if ( glowStage != -1 )
		{
			// merge with diffuse stage
			stages[ diffuseStage ].bundle[ TB_GLOWMAP ] = stages[ glowStage ].bundle[ TB_COLORMAP ];
			// disable since it's merged
			stages[ glowStage ].active = false;
		}
	}

	GroupActiveStages();
}

// Make shader stages ready to be used by renderer functions.
static void FinishStages()
{
	bool lightStageFound = false;

	/* Skip standalone lightmaps, they are assumed to be buggy,
	see: https://github.com/DaemonEngine/Daemon/issues/322 */
	if ( numStages == 1 && stages[ 0 ].type == stageType_t::ST_LIGHTMAP )
	{
		Log::Warn("Skipping standalone lightmap in shader '%s', assumed to be buggy.", shader.name);
		stages[ 0 ].active = false;
		numStages = 0;
	}

	for ( size_t s = 0; s < numStages; s++ )
	{
		shaderStage_t *stage = &stages[ s ];

		if ( r_showLightMaps->integer && lightStageFound )
		{
			stage->active = false;
			continue;
		}

		switch ( stage->type )
		{
			case stageType_t::ST_HEATHAZEMAP:
				stage->active = r_heatHaze->integer;
				break;

			case stageType_t::ST_LIQUIDMAP:
				if ( !r_liquidMapping->integer )
				{
					stage->type = stageType_t::ST_COLLAPSE_DIFFUSEMAP;
					stage->bundle[ TB_DIFFUSEMAP ].image[ 0 ] = tr.whiteImage;
				}
				break;

			case stageType_t::ST_REFLECTIONMAP:
			case stageType_t::ST_COLLAPSE_REFLECTIONMAP:
				stage->active = glConfig2.reflectionMappingAvailable;
				break;

			case stageType_t::ST_LIGHTMAP:
				// standalone lightmap stage: paint shadows over a white texture
				stage->bundle[ TB_DIFFUSEMAP ].image[ 0 ] = tr.whiteImage;
				lightStageFound = true;
				break;

			case stageType_t::ST_DIFFUSEMAP:
			case stageType_t::ST_COLLAPSE_DIFFUSEMAP:
				lightStageFound = true;
				break;

			case stageType_t::ST_ATTENUATIONMAP_XY:
			case stageType_t::ST_ATTENUATIONMAP_Z:
				stage->active = ( glConfig2.realtimeLighting && r_realtimeLightingRenderer.Get() == Util::ordinal( realtimeLightingRenderer_t::LEGACY ) );
				break;

			default:
				break;
		}

		memset( stage->variantOffsets, -1, ShaderStageVariant::ALL * sizeof( int ) );
	}

	GroupActiveStages();

	int deformIndex = shader.numDeforms > 0
		? gl_shaderManager.getDeformShaderIndex( shader.deforms, shader.numDeforms )
		: 0;

	for ( size_t s = 0; s < numStages; s++ )
	{
		shaderStage_t *stage = &stages[ s ];

		stage->deformIndex = deformIndex;

		// Available textures.
		bool hasNormalMap = stage->bundle[ TB_NORMALMAP ].image[ 0 ] != nullptr;
		bool hasHeightMap = stage->bundle[ TB_HEIGHTMAP ].image[ 0 ] != nullptr;
		bool hasMaterialMap = stage->bundle[ TB_MATERIALMAP ].image[ 0 ] != nullptr;
		bool hasGlowMap = stage->bundle[ TB_GLOWMAP ].image[ 0 ] != nullptr;

		// Texture storage variants.
		stage->hasHeightMapInNormalMap = stage->hasHeightMapInNormalMap && hasNormalMap;

		// Available features.
		stage->enableNormalMapping = glConfig2.normalMapping && hasNormalMap;
		stage->enableDeluxeMapping = glConfig2.deluxeMapping && ( hasNormalMap || hasMaterialMap );

		stage->enableReliefMapping = glConfig2.reliefMapping && !shader.disableReliefMapping
			&& ( hasHeightMap || stage->hasHeightMapInNormalMap );

		stage->enableGlowMapping = r_glowMapping->integer && hasGlowMap;

		if ( stage->collapseType == collapseType_t::COLLAPSE_PBR )
		{
			stage->enablePhysicalMapping = glConfig2.physicalMapping && hasMaterialMap;
			stage->enableSpecularMapping = false;
		}
		else
		{
			stage->enableSpecularMapping = glConfig2.specularMapping && hasMaterialMap;
			stage->enablePhysicalMapping = false;
		}

		// FIXME: Workaround for textures having both an alpha mask (like gratings) with an height map,
		// The engine does not displace the depth test map yet so we disable relief mapping to prevent garbage
		// to appears between the displaced diffuse map and the non-displaced alpha mask.
		// See https://github.com/DaemonEngine/Daemon/issues/334
		if ( stage->enableReliefMapping && stage->stateBits & ~GLS_DEPTHMASK_TRUE )
		{
			Log::Debug( "Workaround: disabling relief mapping for stage %d in shader '%s' because of alpha mask", s, shader.name );
			stage->enableReliefMapping = false;
		}

		// Finally disable useless heightMapInNormalMap if both normal and relief mapping are disabled.
		// see https://github.com/DaemonEngine/Daemon/issues/376
		stage->hasHeightMapInNormalMap = stage->hasHeightMapInNormalMap
			&& ( stage->enableNormalMapping || stage->enableReliefMapping );

		// Bind fallback textures if required.
		if ( !stage->enableNormalMapping && !( stage->enableReliefMapping && stage->hasHeightMapInNormalMap) )
		{
			stage->bundle[ TB_NORMALMAP ].image[ 0 ] = tr.flatImage;
		}

		if ( !stage->enableSpecularMapping && !stage->enablePhysicalMapping )
		{
			// If specular mapping is enabled always use the specular mapping
			// shader to avoid costly GLSL shader switching.
			if ( glConfig2.specularMapping )
			{
				stage->enableSpecularMapping = true;
				stage->bundle[ TB_MATERIALMAP ].image[ 0 ] = tr.blackImage;
			}
		}

		if ( !stage->enableGlowMapping )
		{
			stage->bundle[ TB_GLOWMAP ].image[ 0 ] = tr.blackImage;
		}

		// Compute normal scale.
		if ( hasNormalMap )
		{
			/* Please make sure the parser sets a normal format
			when adding a new normal map syntax. */
			DAEMON_ASSERT( HasNormalFormat( stage ) );

			/* Without true as third argument, this function does
			nothing if a normal format is already set, this is done
			here as a fallback, GL format is 1,1,1. */
			SetNormalFormat( stage, glNormalFormat );
			
			stage->normalScale[ 0 ] *= stage->normalFormat[ 0 ];
			stage->normalScale[ 1 ] *= stage->normalFormat[ 1 ];

			/* Because Z reconstruction is destructive on alpha channel
			Z reconstruction is never done on normal map shipping height
			map in alpha channel and Z is read from the file itself.
			Those files always provide Z anyway.

			If height map is not stored in normal map alpha channel,
			the Z component will be reconstructed from X and Y whatever
			Z is provided by the file or not) and Z will be fine from
			the start, so we must not apply the format translation on
			the Z channel.

			So this test means X and Y formats are always applied,
			but Z format is applied only when Z is not reconstructed.

			The XYZ format translations are not done on RGB channels
			from the file but on the RGB channels as seen in GLSL shader,
			there is no need to worry about DXn storing X in alpha channel.

			This way the material syntax is expected to work the same with
			both the PNG source and the released CRN. */
			if ( stage->hasHeightMapInNormalMap )
			{
				stage->normalScale[ 2 ] *= stage->normalFormat[ 2 ];
			}
		}
	}

	GroupActiveStages();
}

// Preselect the renderers to be used to render the stages.
static void SetStagesRenderers()
{
	struct stageRendererOptions_t {
		// Core renderer (code path for when only OpenGL Core is available, or compatible OpenGL 2).
		stageRenderer_t colorRenderer;

		// Material renderer (code path for advanced OpenGL techniques like bindless textures).
		surfaceDataUpdater_t surfaceDataUpdater;
		stageShaderBinder_t shaderBinder;
		stageMaterialProcessor_t materialProcessor;

		// Per-stage configuration.
		bool doShadowFill;
		bool doForwardLighting;
	};

	for ( size_t s = 0; s < numStages; s++ )
	{
		shaderStage_t *stage = &stages[ s ];

		stageRendererOptions_t stageRendererOptions = {
			&Render_NONE,
			&UpdateSurfaceDataNONE, &BindShaderNONE, &ProcessMaterialNONE,
			false, false,
		};

		bool opaqueOrLess = shader.sort <= Util::ordinal(shaderSort_t::SS_OPAQUE);

		switch ( stage->type )
		{
			case stageType_t::ST_COLORMAP:
				stageRendererOptions = {
					&Render_generic,
					&UpdateSurfaceDataGeneric3D, &BindShaderGeneric3D, &ProcessMaterialGeneric3D,
					opaqueOrLess, false,
				};
				break;
			case stageType_t::ST_STYLELIGHTMAP:
			case stageType_t::ST_STYLECOLORMAP:
				stageRendererOptions = {
					&Render_generic3D,
					&UpdateSurfaceDataGeneric3D, &BindShaderGeneric3D, &ProcessMaterialGeneric3D,
					true, false,
				};
				break;
			case stageType_t::ST_LIGHTMAP:
			case stageType_t::ST_DIFFUSEMAP:
			case stageType_t::ST_COLLAPSE_DIFFUSEMAP:
				stageRendererOptions = {
					&Render_lightMapping,
					&UpdateSurfaceDataLightMapping, &BindShaderLightMapping, &ProcessMaterialLightMapping,
					true, true,
				};
				break;
			case stageType_t::ST_COLLAPSE_COLORMAP:
				stageRendererOptions = {
					&Render_lightMapping,
					&UpdateSurfaceDataLightMapping, &BindShaderLightMapping, &ProcessMaterialLightMapping,
					true, false,
				};
				break;
			case stageType_t::ST_REFLECTIONMAP:
			case stageType_t::ST_COLLAPSE_REFLECTIONMAP:
				stageRendererOptions = {
					&Render_reflection_CB,
					&UpdateSurfaceDataReflection, &BindShaderReflection, &ProcessMaterialReflection,
					false, false,
				};
				break;
			case stageType_t::ST_SKYBOXMAP:
				stageRendererOptions = {
					&Render_skybox,
					&UpdateSurfaceDataSkybox, &BindShaderSkybox, &ProcessMaterialSkybox,
					false, false,
				};
				break;
			case stageType_t::ST_SCREENMAP:
				stageRendererOptions = {
					&Render_screen,
					&UpdateSurfaceDataScreen, &BindShaderScreen, &ProcessMaterialScreen,
					false, false,
				};
				break;
			case stageType_t::ST_PORTALMAP:
				/* Comment from the Material code:
				This is supposedly used for alphagen portal and portal surfaces should never get here. */
				stageRendererOptions = {
					&Render_portal,
					&UpdateSurfaceDataNONE, &BindShaderNONE, &ProcessMaterialNONE,
					false, false,
				};
				break;
			case stageType_t::ST_HEATHAZEMAP:
				stageRendererOptions = {
					&Render_heatHaze,
					&UpdateSurfaceDataHeatHaze, &BindShaderHeatHaze, &ProcessMaterialHeatHaze,
					false, false,
				};
				break;
			case stageType_t::ST_LIQUIDMAP:
				stageRendererOptions = {
					&Render_liquid,
					&UpdateSurfaceDataLiquid, &BindShaderLiquid, &ProcessMaterialLiquid,
					false, false,
				};
				break;
			case stageType_t::ST_FOGMAP:
				stageRendererOptions = {
					&Render_fog,
					&UpdateSurfaceDataFog, &BindShaderFog, &ProcessMaterialFog,
					false, false,
				};
				break;
			case stageType_t::ST_ATTENUATIONMAP_XY:
			case stageType_t::ST_ATTENUATIONMAP_Z:
				stageRendererOptions = {
					&Render_NOP,
					&UpdateSurfaceDataNOP, &BindShaderNOP, &ProcessMaterialNOP,
					false, true,
				};
				break;
			default:
				Log::Warn( "Missing renderer for stage type %d in shader %s, stage %d",
					Util::ordinal(stage->type), shader.name, s );
				stageRendererOptions = {
					&Render_NOP,
					&UpdateSurfaceDataNOP, &BindShaderNOP, &ProcessMaterialNOP,
					false, false,
				};
				break;
		}

		stage->colorRenderer = stageRendererOptions.colorRenderer;

		stage->surfaceDataUpdater = stageRendererOptions.surfaceDataUpdater;
		stage->shaderBinder = stageRendererOptions.shaderBinder;
		stage->materialProcessor = stageRendererOptions.materialProcessor;

		stage->doShadowFill = stageRendererOptions.doShadowFill;
		stage->doForwardLighting = stageRendererOptions.doForwardLighting;

		// Disable stages that have no renderer yet.
		if ( stage->colorRenderer == &Render_NONE )
		{
			stage->active = false;
			continue;
		}
	}

	GroupActiveStages();
}

// *INDENT-ON*

/*
==============
SortNewShader

Positions the most recently created shader in the tr.sortedShaders[]
array so that the shader->sort key is sorted relative to the other
shaders.

Sets shader->sortedIndex
==============
*/
static void SortNewShader()
{
	int      i;
	float    sort;
	shader_t *newShader;

	newShader = tr.shaders[ tr.numShaders - 1 ];
	sort = newShader->sort;

	for ( i = tr.numShaders - 2; i >= 0; i-- )
	{
		if ( tr.sortedShaders[ i ]->sort <= sort )
		{
			break;
		}

		tr.sortedShaders[ i + 1 ] = tr.sortedShaders[ i ];
		tr.sortedShaders[ i + 1 ]->sortedIndex++;
	}

	newShader->sortedIndex = i + 1;
	tr.sortedShaders[ i + 1 ] = newShader;
}

// Copy the current global shader to a newly allocated shader.
static shader_t *MakeShaderPermanent()
{
	if ( tr.numShaders == MAX_SHADERS )
	{
		Log::Warn("MakeShaderPermanent - MAX_SHADERS hit" );
		return tr.defaultShader;
	}

	shader_t *newShader = (shader_t*) ri.Hunk_Alloc( sizeof( shader_t ), ha_pref::h_low );

	*newShader = shader;

	tr.shaders[ tr.numShaders ] = newShader;
	newShader->index = tr.numShaders;

	tr.sortedShaders[ tr.numShaders ] = newShader;
	newShader->sortedIndex = tr.numShaders;

	tr.numShaders++;

	ASSERT( numStages <= MAX_SHADER_STAGES );

	newShader->stages = (shaderStage_t*) ri.Hunk_Alloc( sizeof( shaderStage_t ) * numStages, ha_pref::h_low );
	std::copy_n( stages.data(), numStages, newShader->stages );

	for ( size_t s = 0; s < numStages; s++ )
	{
		for ( size_t b = 0; b < MAX_TEXTURE_BUNDLES; b++ )
		{
			size_t size = newShader->stages[ s ].bundle[ b ].numTexMods * sizeof( texModInfo_t );
			newShader->stages[ s ].bundle[ b ].texMods = (texModInfo_t*) ri.Hunk_Alloc( size, ha_pref::h_low );
			std::copy_n( stages[ s ].bundle[ b ].texMods, newShader->stages[ s ].bundle[ b ].numTexMods,
			             newShader->stages[ s ].bundle[ b ].texMods );
		}

		newShader->stages[ s ].shader = newShader;
	}

	newShader->lastStage = newShader->stages + numStages;

	SortNewShader();

	int hash = generateHashValue( newShader->name, FILE_HASH_SIZE );
	newShader->next = shaderHashTable[ hash ];
	shaderHashTable[ hash ] = newShader;

	return newShader;
}

/*
====================
GeneratePermanentShaderTable
====================
*/
static void GeneratePermanentShaderTable( const float *values, int numValues )
{
	shaderTable_t *newTable;
	int           i;
	int           hash;

	if ( tr.numTables == MAX_SHADER_TABLES )
	{
		Log::Warn("GeneratePermanentShaderTable - MAX_SHADER_TABLES hit" );
		return;
	}

	const float emptyTable[] = { 0.0f };
	if ( numValues == 0)
	{
		Log::Warn("Empty shader table");
		values = emptyTable;
		numValues = 1;
	}

	newTable = (shaderTable_t*) ri.Hunk_Alloc( sizeof( shaderTable_t ), ha_pref::h_low );

	*newTable = table;

	tr.shaderTables[ tr.numTables ] = newTable;
	newTable->index = tr.numTables;

	tr.numTables++;

	newTable->numValues = numValues;
	newTable->values = (float*) ri.Hunk_Alloc( sizeof( float ) * numValues, ha_pref::h_low );

	for ( i = 0; i < numValues; i++ )
	{
		newTable->values[ i ] = values[ i ];
	}

	hash = generateHashValue( newTable->name, MAX_SHADERTABLE_HASH );
	newTable->next = shaderTableHashTable[ hash ];
	shaderTableHashTable[ hash ] = newTable;
}

bool CheckShaderNameLength( const char* func_err, const char* name, const char* suffix )
{
	if ( strlen( name ) + strlen( suffix ) >= MAX_QPATH )
	{
		Log::Warn("%s Shader name %s%s length longer than MAX_QPATH %d", func_err, name, suffix, MAX_QPATH );
		return false;
	}

	return true;
}

static void ValidateStage( shaderStage_t *pStage )
{
	struct stageCheck_t {
		bool expected;
		bool check;
		bool fallback;
		const std::string name;
	};

	static const std::unordered_map<stageType_t, stageCheck_t> stageTypeCheck = {
		{ stageType_t::ST_COLORMAP, { true, true, true, "color map" } },
		{ stageType_t::ST_GLOWMAP, { true, true, false, "glowMap" } },
		{ stageType_t::ST_DIFFUSEMAP, { true, true, true, "diffuseMap" } },
		{ stageType_t::ST_NORMALMAP, { true, true, false, "normalMap" } },
		{ stageType_t::ST_HEIGHTMAP, { true, true, false, "heightMap" } },
		// There is no standalone physicalMap stage.
		{ stageType_t::ST_PHYSICALMAP, { false, false, false, "physicalMap" } },
		{ stageType_t::ST_SPECULARMAP, { true, true, false, "specularMap" } },
		{ stageType_t::ST_HEATHAZEMAP, { true, true, false, "heatHazeMap" } },
		{ stageType_t::ST_LIQUIDMAP, { true, true, false, "liquidMap" } },
		{ stageType_t::ST_FOGMAP, { true, false, false, "fogMap" } },
		// The lightmap is fetched at render time.
		{ stageType_t::ST_LIGHTMAP, { true, false, false, "light map" } },
		// The lightmap is fetched at render time.
		{ stageType_t::ST_STYLELIGHTMAP, { true, false, false, "style light map" } },
		{ stageType_t::ST_STYLECOLORMAP, { true, true, false, "style color map" } },
		{ stageType_t::ST_COLLAPSE_COLORMAP, { true, true, true, "collapsed color map" } },
		{ stageType_t::ST_COLLAPSE_DIFFUSEMAP, { true, true, true, "collapsed diffuseMap" } },
		{ stageType_t::ST_ATTENUATIONMAP_XY, { true, true, false, "attenuationMapXY" } },
		{ stageType_t::ST_ATTENUATIONMAP_Z, { true, true, false, "attenuationMapZ" } },
		// TODO: Document the remaining stage types.
	};

	auto it = stageTypeCheck.find( pStage->type );
	if ( it != stageTypeCheck.end()  )
	{
		stageCheck_t stageCheck = it->second;

		if ( stageCheck.expected )
		{
			if ( stageCheck.check )
			{
				// Check for a missing texture.
				if ( !pStage->bundle[ 0 ].image[ 0 ] )
				{
					if ( stageCheck.fallback )
					{
						Log::Warn("Shader %s has a %s stage with no image (using default as a fallback)",
							shader.name, stageCheck.name );

						pStage->bundle[ 0 ].image[ 0 ] = tr.defaultImage;
					}
					else
					{
						Log::Warn("Shader %s has a %s stage with no image (ignored)", 
							shader.name, stageCheck.name );

						pStage->active = false;
					}
				}
			}
		}
		else
		{
			Log::Warn("Shader %s has unexpected standalone %s stage (ignored)", 
				shader.name, stageCheck.name );

			pStage->active = false;
		}
	}
	else if ( !pStage->bundle[ 0 ].image[ 0 ] )
	{
		Log::Warn("Shader %s has an undocumented type %d stage with no image (ignored)",
			shader.name, Util::ordinal(pStage->type) );

		pStage->active = false;
	}
}

// Note: this code was written to be exactly equivalent to previous sort determination code, which
// was scattered chaotically throughout FinishShader(). So which conditions take priority over
// which won't make sense. For example if there is an explicit "sort" keyword, probably that should
// override anything else? But maybe there are some buggy legacy assets so I won't change it
// without a thorough investigation.
static float DetermineShaderSort()
{
	// fogonly shaders don't have any stage passes
	if ( numStages == 0 && !shader.isSky )
	{
		return Util::ordinal(shaderSort_t::SS_FOG);
	}

	for ( size_t stage = numStages; stage--; )
	{
		ASSERT( stages[ stage ].active );

		if ( shader.isSky && stages[ stage ].noFog )
		{
			return Util::ordinal(shaderSort_t::SS_ENVIRONMENT_NOFOG);
		}
	}

	if ( shader.forceOpaque )
	{
		return Util::ordinal(shaderSort_t::SS_OPAQUE);
	}

	// set sky stuff appropriate
	if ( shader.isSky )
	{
		if ( shader.noFog )
		{
			return Util::ordinal(shaderSort_t::SS_ENVIRONMENT_NOFOG);
		}
		else
		{
			return Util::ordinal(shaderSort_t::SS_ENVIRONMENT_FOG);
		}
	}

	// If we get to this point without overriding it, we use the sort which was already set before
	// calling FinishShader(). This may be set by an explicit "sort XXX", or various other shader-
	// or stage-level keywords which cause it to be set automatically. (TODO: move the other things
	// that set it automatically to this function.)
	if ( shader.sort )
	{
		return shader.sort;
	}

	// set polygon offset
	if ( shader.polygonOffset )
	{
		return Util::ordinal(shaderSort_t::SS_DECAL);
	}

	for ( size_t stage = 0; stage < numStages; stage++ )
	{
		if ( stages[ stage ].type == stageType_t::ST_HEATHAZEMAP )
		{
			return Util::ordinal(shaderSort_t::SS_BLEND0);
		}

		// determine sort order and fog color adjustment
		if ( ( stages[ stage ].stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) &&
		     ( stages[ 0 ].stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) )
		{
			// see through item, like a grill or grate
			if ( stages[ stage ].stateBits & GLS_DEPTHMASK_TRUE)
			{
				return Util::ordinal(shaderSort_t::SS_SEE_THROUGH);
			}
			else
			{
				return Util::ordinal(shaderSort_t::SS_BLEND0);
			}
		}
	}

	// there are times when you will need to manually apply a sort to
	// opaque alpha tested shaders that have later blend passes
	if ( shader.translucent )
	{
		return Util::ordinal(shaderSort_t::SS_DECAL);
	}

	return Util::ordinal(shaderSort_t::SS_OPAQUE);
}

/*
=========================
FinishShader

Returns a freshly allocated shader with all the needed info
from the current global working shader
=========================
*/
static shader_t *FinishShader()
{
	if ( !shader.portalRange )
	{
		/* Mirrors will not use that range but we need all
		other portals to have it set. */
		shader.portalRange = r_portalDefaultRange.Get();
	}

	// all light materials need at least one z attenuation stage as first stage
	if ( shader.type == shaderType_t::SHADER_LIGHT )
	{
		if ( stages[ 0 ].type != stageType_t::ST_ATTENUATIONMAP_Z )
		{
			// move up subsequent stages
			memmove( &stages[ 1 ], &stages[ 0 ], sizeof( stages[ 0 ] ) * ( MAX_SHADER_STAGES - 1 ) );

			stages[ 0 ].active = true;
			stages[ 0 ].type = stageType_t::ST_ATTENUATIONMAP_Z;
			stages[ 0 ].rgbGen = colorGen_t::CGEN_IDENTITY;
			stages[ 0 ].stateBits = GLS_DEFAULT;
			stages[ 0 ].overrideWrapType = true;
			stages[ 0 ].wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;

			const char *squarelight1a = "lights/squarelight1a";
			Log::Debug( "loading '%s' image as shader", squarelight1a );
			LoadMap( &stages[ 0 ], squarelight1a, stageType_t::ST_COLORMAP );
		}

		// force following shader stages to be xy attenuation stages
		for ( int i = 1; i < MAX_SHADER_STAGES; i++ )
		{
			shaderStage_t *pStage = &stages[ i ];

			if ( !pStage->active )
			{
				break;
			}

			pStage->type = stageType_t::ST_ATTENUATIONMAP_XY;
		}
	}

	numStages = MAX_SHADER_STAGES;
	GroupActiveStages();

	if ( shader.forceLightMap ) {
		for( size_t stage = 0; stage < numStages; stage++ ) {
			shaderStage_t* pStage = &stages[numStages];

			// TODO: Add lightmap as the first stage if there's a heatHaze stage here?
			if ( pStage->type == stageType_t::ST_COLORMAP ) {
				pStage->type = stageType_t::ST_DIFFUSEMAP;
			}
		}
	}

	// set appropriate stage information
	for ( size_t stage = 0; stage < numStages; stage++ )
	{
		shaderStage_t *pStage = &stages[ stage ];

		if ( !shader.isSky )
		{
			switch ( pStage->type )
			{
				case stageType_t::ST_NORMALMAP:
				case stageType_t::ST_STYLELIGHTMAP:
				case stageType_t::ST_STYLECOLORMAP:
				case stageType_t::ST_LIGHTMAP:
				case stageType_t::ST_DIFFUSEMAP:
				case stageType_t::ST_COLLAPSE_DIFFUSEMAP:
					shader.interactLight = true;
					break;
				default:
					break;
			}
		}

		ValidateStage( pStage );

		if ( !pStage->active )
		{
			continue;
		}

		if ( shader.forceOpaque )
		{
			pStage->stateBits |= GLS_DEPTHMASK_TRUE;
		}
	}

	GroupActiveStages();

	shader.sort = DetermineShaderSort();

	// HACK: allow alpha tested surfaces to create shadowmaps
	if ( glConfig2.shadowMapping )
	{
		if ( shader.noShadows && shader.alphaTest )
		{
			shader.noShadows = false;
		}
	}

	if ( shader.sort <= Util::ordinal( shaderSort_t::SS_OPAQUE ) ) {
		shader.fogPass = fogPass_t::FP_EQUAL;
	} else if ( shader.contentFlags & CONTENTS_FOG ) {
		shader.fogPass = fogPass_t::FP_LE;
	}

	shader.noFog = shader.noFog || shader.fogPass == fogPass_t::FP_NONE;

	// look for multitexture potential
	CollapseStages();

	// Make shader stages ready to be used by renderer functions.
	FinishStages();

	// Preselect the renderers to be used to render the stages.
	SetStagesRenderers();

	// Copy the current global shader to a newly allocated shader.
	shader_t *ret = MakeShaderPermanent();

	if ( !shader.noFog ) {
		if ( shader.fogPass == fogPass_t::FP_EQUAL ) {
			ret->fogShader = tr.fogEqualShader;
		} else {
			ret->fogShader = tr.fogLEShader;
		}
	}

	// generate depth-only shader if necessary
	if( r_depthShaders.Get() &&
	    !shader.isSky &&
	    numStages > 0 &&
	    (stages[0].stateBits & GLS_DEPTHMASK_TRUE) &&
	    !(stages[0].stateBits & GLS_DEPTHFUNC_EQUAL) &&
	    !(shader.type == shaderType_t::SHADER_2D) &&
	    !shader.polygonOffset ) {
		// keep only the first stage
		stages[1].active = false;
		numStages = 1;
		shader.noFog = true;
		shader.fogShader = nullptr;

		const char* depthShaderSuffix = "$depth";

		if ( !CheckShaderNameLength( "FinishShader", shader.name, depthShaderSuffix ) )
		{
			ret->depthShader = nullptr;

			if ( glConfig2.usingMaterialSystem && !tr.worldLoaded ) {
				uint8_t maxStages = ret->lastStage - ret->stages;

				// Add 1 for potential fog stages
				maxStages = PAD( maxStages + 1, 4 ); // Aligned to 4 components
				materialSystem.maxStages = std::max( maxStages, materialSystem.maxStages );
			}

			return ret;
		}

		strcat( shader.name, depthShaderSuffix );

		if( stages[0].stateBits & GLS_ATEST_BITS ) {
			// alpha test requires a custom depth shader
			shader.sort = Util::ordinal( shaderSort_t::SS_DEPTH );
			stages[0].stateBits &= ~GLS_SRCBLEND_BITS & ~GLS_DSTBLEND_BITS;
			stages[0].stateBits |= GLS_COLORMASK_BITS;
			stages[0].type = stageType_t::ST_COLORMAP;

			// Preselect the renderers to be used to render the stages.
			SetStagesRenderers();

			// Copy the current global shader to a newly allocated shader.
			ret->depthShader = MakeShaderPermanent();
		} else if ( shader.cullType == 0 &&
			    shader.numDeforms == 0 &&
			    tr.defaultShader ) {
			// can use the default depth shader
			ret->depthShader = tr.defaultShader->depthShader;
		} else {
			// requires a custom depth shader, but can skip
			// the texturing
			shader.sort = Util::ordinal( shaderSort_t::SS_DEPTH );
			stages[0].stateBits &= ~GLS_SRCBLEND_BITS & ~GLS_DSTBLEND_BITS;
			stages[0].stateBits |= GLS_COLORMASK_BITS;
			stages[0].type = stageType_t::ST_COLORMAP;

			stages[0].bundle[0].image[0] = tr.whiteImage;
			stages[0].bundle[0].numTexMods = 0;
			stages[0].tcGen_Environment = false;
			stages[0].tcGen_Lightmap = false;
			stages[0].rgbGen = colorGen_t::CGEN_IDENTITY;
			stages[0].alphaGen = alphaGen_t::AGEN_IDENTITY;

			// Preselect the renderers to be used to render the stages.
			SetStagesRenderers();

			// Copy the current global shader to a newly allocated shader.
			ret->depthShader = MakeShaderPermanent();
		}

		// disable depth writes in the main pass
		ret->stages[0].stateBits &= ~GLS_DEPTHMASK_TRUE;
	} else {
		ret->depthShader = nullptr;
	}

	if ( glConfig2.usingMaterialSystem && !tr.worldLoaded ) {
		uint8_t maxStages = ret->lastStage - ret->stages;

		// Add 1 for potential depth stages
		// Add 1 for potential fog stages
		maxStages = PAD( maxStages + 2, 4 ); // Aligned to 4 components
		materialSystem.maxStages = std::max( maxStages, materialSystem.maxStages );
	}

	// load all altShaders recursively
	for ( int i = 1; i < MAX_ALTSHADERS; ++i )
	{
		if ( ret->altShader[ i ].name )
		{
			// flags were previously stashed in altShader[0].index
			shader_t *sh = R_FindShader( ret->altShader[ i ].name, ret->type, (RegisterShaderFlags_t)ret->altShader[ 0 ].index );

			ret->altShader[ i ].index = sh->defaultShader ? 0 : sh->index;
		}
	}

	return ret;
}

//========================================================================================

/*
====================
FindShaderInShaderText

Scans the combined text description of all the shader files for
the given shader name.

return nullptr if not found

If found, it will return a valid shader
=====================
*/
static const char    *FindShaderInShaderText( const char *shaderName )
{
	const char *token, *p;

	int  i, hash;

	hash = generateHashValue( shaderName, MAX_SHADERTEXT_HASH );

	for ( i = 0; shaderTextHashTable[ hash ][ i ]; i++ )
	{
		p = shaderTextHashTable[ hash ][ i ];
		token = COM_ParseExt2( &p, true );

		if ( !Q_stricmp( token, shaderName ) )
		{
			return p;
		}
	}

	// if the shader is not in the table, it must not exist
	return nullptr;
}

/*
==================
R_FindShaderByName

Will always return a valid shader, but it might be the
default shader if the real one can't be found.
==================
*/
shader_t       *R_FindShaderByName( const char *name )
{
	char     strippedName[ MAX_QPATH ];
	int      hash;
	shader_t *sh;

	if ( ( name == nullptr ) || ( name[ 0 ] == 0 ) )
	{
		// bk001205
		return tr.defaultShader;
	}

	COM_StripExtension3( name, strippedName, sizeof( strippedName ) );

	hash = generateHashValue( strippedName, FILE_HASH_SIZE );

	// see if the shader is already loaded
	for ( sh = shaderHashTable[ hash ]; sh; sh = sh->next )
	{
		// NOTE: if there was no shader or image available with the name strippedName
		// then a default shader is created with type == SHADER_3D_DYNAMIC, so we
		// have to check all default shaders otherwise for every call to R_FindShader
		// with that same strippedName a new default shader is created.
		if ( Q_stricmp( sh->name, strippedName ) == 0 )
		{
			// match found
			return sh;
		}
	}

	return tr.defaultShader;
}

static void ClearGlobalShader()
{
	ResetStruct( shader );
	ResetStruct( stages );
	numStages = 0;
}

/*
===============
R_FindShader

Will always return a valid shader, but it might be the
default shader if the real one can't be found.

In the interest of not requiring an explicit shader text entry to
be defined for every single image used in the game, three default
shader behaviors can be auto-created for any image:

If type == SHADER_2D, then the image will be used
for 2D rendering unless an explicit shader is found

If type == SHADER_3D_DYNAMIC, then the image will have
dynamic diffuse lighting applied to it, as appropriate for most
entity skin surfaces.

If type == SHADER_3D_STATIC, then the image will use
the vertex rgba modulate values, as appropriate for misc_model
pre-lit surfaces.
===============
*/
shader_t       *R_FindShader( const char *name, shaderType_t type, int flags )
{
	char     strippedName[ MAX_QPATH ];
	char     fileName[ MAX_QPATH ];
	int      i, hash, bits;
	const char     *shaderText;
	image_t  *image;
	shader_t *sh;

	if ( name[ 0 ] == 0 )
	{
		return tr.defaultShader;
	}

	COM_StripExtension3( name, strippedName, sizeof( strippedName ) );

	hash = generateHashValue( strippedName, FILE_HASH_SIZE );

	// see if the shader is already loaded
	for ( sh = shaderHashTable[ hash ]; sh; sh = sh->next )
	{
		// NOTE: if there was no shader or image available with the name strippedName
		// then a default shader is created with type == SHADER_3D_DYNAMIC, so we
		// have to check all default shaders otherwise for every call to R_FindShader
		// with that same strippedName a new default shader is created.
		if ( ( sh->type == type || sh->defaultShader ) && !Q_stricmp( sh->name, strippedName ) )
		{
			// match found
			return sh;
		}
	}

	shader.altShader[ 0 ].index = flags; // save for later use (in case of alternative shaders)

	// make sure the render thread is stopped, because we are probably
	// going to have to upload an image
	if ( r_smp->integer )
	{
		R_SyncRenderThread();
	}

	ClearGlobalShader();

	Q_strncpyz( shader.name, strippedName, sizeof( shader.name ) );
	shader.type = type;

	for ( i = 0; i < MAX_SHADER_STAGES; i++ )
	{
		stages[ i ].bundle[ 0 ].texMods = texMods[ i ];
	}

	// ydnar: default to no implicit mappings
	implicitMap[ 0 ] = '\0';
	implicitStateBits = GLS_DEFAULT;
	if( shader.type == shaderType_t::SHADER_2D )
	{
		implicitCullType = CT_TWO_SIDED;
	}
	else
	{
		implicitCullType = CT_FRONT_SIDED;
	}

	if ( flags & RSF_NOMIP )
	{
		shader.noPicMip = true;
	}

	if ( flags & RSF_FITSCREEN )
	{
		shader.fitScreen = true;
	}

	if ( flags & RSF_SPRITE )
	{
		shader.entitySpriteFaceViewDirection = true;
	}

	if ( flags & RSF_FORCE_LIGHTMAP )
	{
		shader.forceLightMap = true;
	}

	// attempt to define shader from an explicit parameter file
	shaderText = FindShaderInShaderText( strippedName );

	if ( shaderText )
	{
		// enable this when building a pak file to get a global list
		// of all explicit shaders
		if ( r_printShaders->integer )
		{
			Log::Notice("loading explicit shader '%s'", strippedName );
		}

		if ( !ParseShader( shaderText ) )
		{
			// had errors, so use default shader
			shader.defaultShader = true;
			sh = FinishShader();
			return sh;
		}

		// ydnar: allow implicit mappings
		if ( implicitMap[ 0 ] == '\0' )
		{
			sh = FinishShader();
			return sh;
		}
	}

	// ydnar: allow implicit mapping ('-' = use shader name)
	if ( implicitMap[ 0 ] == '\0' || implicitMap[ 0 ] == '-' )
	{
		Q_strncpyz( fileName, strippedName, sizeof( fileName ) );
	}
	else
	{
		Q_strncpyz( fileName, implicitMap, sizeof( fileName ) );
	}

	// if not defined in the in-memory shader descriptions,
	// look for a single supported image file
	bits = IF_NONE;

	if ( flags & RSF_NOMIP )
	{
		bits |= IF_NOPICMIP;
	}

	if ( flags & RSF_FITSCREEN )
	{
		bits |= IF_FITSCREEN;
	}

	Log::Debug( "loading '%s' image as shader", fileName );

	if( bits & RSF_2D )
	{
		imageParams_t imageParams = {};
		imageParams.bits = bits;
		imageParams.filterType = filterType_t::FT_LINEAR;
		imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

		image = R_FindImageFile( fileName, imageParams );
	}
	else
	{
		LoadExtraMaps( &stages[ 0 ], fileName );

		imageParams_t imageParams = {};
		imageParams.bits = bits;
		imageParams.filterType = filterType_t::FT_DEFAULT;
		imageParams.wrapType = wrapTypeEnum_t::WT_REPEAT;

		image = R_FindImageFile( fileName, imageParams );
	}

	if ( !image )
	{
		Log::Warn("Couldn't find image file '%s'", name );
		shader.defaultShader = true;
		return FinishShader();
	}

	// set implicit cull type
	if ( implicitCullType && !shader.cullType )
	{
		shader.cullType = implicitCullType;
	}

	// create the default shading commands
	switch ( shader.type )
	{
		case shaderType_t::SHADER_2D:
			{
				// GUI elements
				stages[ 0 ].bundle[ 0 ].image[ 0 ] = image;
				stages[ 0 ].active = true;
				stages[ 0 ].rgbGen = colorGen_t::CGEN_VERTEX;
				stages[ 0 ].alphaGen = alphaGen_t::AGEN_VERTEX;
				stages[ 0 ].stateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
				break;
			}

		case shaderType_t::SHADER_3D_DYNAMIC:
			{
				// dynamic colors at vertexes
				stages[ 0 ].type = stageType_t::ST_COLLAPSE_DIFFUSEMAP;
				stages[ 0 ].bundle[ 0 ].image[ 0 ] = image;
				stages[ 0 ].active = true;
				stages[ 0 ].rgbGen = colorGen_t::CGEN_IDENTITY_LIGHTING;
				stages[ 0 ].stateBits = implicitStateBits;
				break;
			}

		case shaderType_t::SHADER_3D_STATIC:
			{
				// explicit colors at vertexes
				stages[ 0 ].type = stageType_t::ST_COLLAPSE_DIFFUSEMAP;
				stages[ 0 ].bundle[ 0 ].image[ 0 ] = image;
				stages[ 0 ].active = true;
				stages[ 0 ].rgbGen = colorGen_t::CGEN_IDENTITY;
				stages[ 0 ].stateBits = implicitStateBits;
				break;
			}

		case shaderType_t::SHADER_LIGHT:
			{
				stages[ 0 ].type = stageType_t::ST_ATTENUATIONMAP_Z;
				stages[ 0 ].bundle[ 0 ].image[ 0 ] = tr.noFalloffImage; // FIXME should be attenuationZImage
				stages[ 0 ].active = true;
				stages[ 0 ].rgbGen = colorGen_t::CGEN_IDENTITY;
				stages[ 0 ].stateBits = GLS_DEFAULT;

				stages[ 1 ].type = stageType_t::ST_ATTENUATIONMAP_XY;
				stages[ 1 ].bundle[ 0 ].image[ 0 ] = image;
				stages[ 1 ].active = true;
				stages[ 1 ].rgbGen = colorGen_t::CGEN_IDENTITY;
				stages[ 1 ].stateBits = GLS_DEFAULT;
				break;
			}

		default:
			break;
	}

	return FinishShader();
}

qhandle_t RE_RegisterShaderFromImage( const char *name, image_t *image )
{
	int      i, hash;
	shader_t *sh;

	hash = generateHashValue( name, FILE_HASH_SIZE );

	// see if the shader is already loaded
	for ( sh = shaderHashTable[ hash ]; sh; sh = sh->next )
	{
		// NOTE: if there was no shader or image available with the name strippedName
		// then a default shader is created with type == SHADER_3D_DYNAMIC, so we
		// have to check all default shaders otherwise for every call to R_FindShader
		// with that same strippedName a new default shader is created.
		if ( ( sh->type == shaderType_t::SHADER_2D || sh->defaultShader ) && !Q_stricmp( sh->name, name ) )
		{
			// match found
			return sh->index;
		}
	}

	// make sure the render thread is stopped, because we are probably
	// going to have to upload an image
	if ( r_smp->integer )
	{
		R_SyncRenderThread();
	}

	ClearGlobalShader();

	Q_strncpyz( shader.name, name, sizeof( shader.name ) );
	shader.type = shaderType_t::SHADER_2D;
	shader.cullType = CT_TWO_SIDED;

	for ( i = 0; i < MAX_SHADER_STAGES; i++ )
	{
		stages[ i ].bundle[ 0 ].texMods = texMods[ i ];
	}

	// create the default shading commands

	// GUI elements
	stages[ 0 ].bundle[ 0 ].image[ 0 ] = image;
	stages[ 0 ].active = true;
	stages[ 0 ].rgbGen = colorGen_t::CGEN_VERTEX;
	stages[ 0 ].alphaGen = alphaGen_t::AGEN_VERTEX;
	stages[ 0 ].stateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;

	sh = FinishShader();
	return sh->index;
}

/*
====================
RE_RegisterShader

This is the exported shader entry point for the rest of the system
It will always return an index that will be valid.

This should really only be used for explicit shaders, because there is no
way to ask for different implicit lighting modes (vertex, lightmap, etc)
====================
*/
qhandle_t RE_RegisterShader( const char *name, int flags )
{
	shader_t *sh;

	if ( !CheckShaderNameLength( "RE_RegisterShader", name, "" ) )
	{
		return 0;
	}

	sh = R_FindShader( name,
			   (flags & RSF_LIGHT_ATTENUATION) ? shaderType_t::SHADER_LIGHT : shaderType_t::SHADER_2D,
			   flags );

	// we want to return 0 if the shader failed to
	// load for some reason, but R_FindShader should
	// still keep a name allocated for it, so if
	// something calls RE_RegisterShader again with
	// the same name, we don't try looking for it again
	if ( sh->defaultShader )
	{
		return 0;
	}

	return sh->index;
}

/*
====================
R_GetShaderByHandle

When a handle is passed in by another module, this range checks
it and returns a valid (possibly default) shader_t to be used internally.
====================
*/
shader_t       *R_GetShaderByHandle( qhandle_t hShader )
{
	if ( hShader < 0 )
	{
		Log::Warn("R_GetShaderByHandle: out of range hShader '%d'", hShader );  // bk: FIXME name
		return tr.defaultShader;
	}

	if ( hShader >= tr.numShaders )
	{
		Log::Warn("R_GetShaderByHandle: out of range hShader '%d'", hShader );
		return tr.defaultShader;
	}

	return tr.shaders[ hShader ];
}

/*
===============
Dump information on all valid shaders to the console
A second parameter will cause it to print in sorted order
===============
*/
class ListShadersCmd : public Cmd::StaticCmd
{
public:
	ListShadersCmd() : StaticCmd(
		"listShaders", Cmd::RENDERER, "list q3shaders currently registered in the renderer") {}

	void Run( const Cmd::Args &args ) const override
	{
		static const std::unordered_map<shaderType_t, std::string> shaderTypeName = {
			{ shaderType_t::SHADER_2D, "2D" },
			{ shaderType_t::SHADER_3D_DYNAMIC, "3D_DYNAMIC" },
			{ shaderType_t::SHADER_3D_STATIC, "3D_STATIC" },
			{ shaderType_t::SHADER_LIGHT, "LIGHT" },
		};

		static const std::unordered_map<shaderSort_t, std::string> shaderSortName = {
			{ shaderSort_t::SS_BAD, "BAD" },
			{ shaderSort_t::SS_PORTAL, "PORTAL" },
			{ shaderSort_t::SS_DEPTH, "DEPTH" },
			{ shaderSort_t::SS_ENVIRONMENT_FOG, "ENV_FOG" },
			{ shaderSort_t::SS_ENVIRONMENT_NOFOG, "ENV_NOFOG" },
			{ shaderSort_t::SS_OPAQUE, "OPAQUE" },
			{ shaderSort_t::SS_DECAL, "DECAL" },
			{ shaderSort_t::SS_SEE_THROUGH, "SEE_THROUGH" },
			{ shaderSort_t::SS_BANNER, "BANNER" },
			{ shaderSort_t::SS_FOG, "FOG" },
			{ shaderSort_t::SS_UNDERWATER, "UNDERWATER" },
			{ shaderSort_t::SS_WATER, "WATER" },
			{ shaderSort_t::SS_FAR, "FAR" },
			{ shaderSort_t::SS_MEDIUM, "MEDIUM" },
			{ shaderSort_t::SS_CLOSE, "CLOSE" },
			{ shaderSort_t::SS_BLEND0, "BLEND0" },
			{ shaderSort_t::SS_BLEND1, "BLEND1" },
			{ shaderSort_t::SS_BLEND2, "BLEND2" },
			{ shaderSort_t::SS_BLEND3, "BLEND3" },
			{ shaderSort_t::SS_BLEND6, "BLEND6" },
			{ shaderSort_t::SS_ALMOST_NEAREST, "ALMOST_NEAREST" },
			{ shaderSort_t::SS_NEAREST, "NEAREST" },
			{ shaderSort_t::SS_POST_PROCESS, "POST_PROCESS" },
		};

		static const std::unordered_map<stageType_t, std::string> stageTypeName = {
			{ stageType_t::ST_COLORMAP, "COLORMAP" },
			{ stageType_t::ST_GLOWMAP, "GLOWMAP" },
			{ stageType_t::ST_DIFFUSEMAP, "DIFFUSEMAP" },
			{ stageType_t::ST_NORMALMAP, "NORMALMAP" },
			{ stageType_t::ST_HEIGHTMAP, "HEIGHTMAP" },
			{ stageType_t::ST_PHYSICALMAP, "PHYSICALMAP" },
			{ stageType_t::ST_SPECULARMAP, "SPECULARMAP" },
			{ stageType_t::ST_REFLECTIONMAP, "REFLECTIONMAP" },
			{ stageType_t::ST_SKYBOXMAP, "SKYBOXMAP" },
			{ stageType_t::ST_SCREENMAP, "SCREENMAP" },
			{ stageType_t::ST_PORTALMAP, "PORTALMAP" },
			{ stageType_t::ST_HEATHAZEMAP, "HEATHAZEMAP" },
			{ stageType_t::ST_LIQUIDMAP, "LIQUIDMAP" },
			{ stageType_t::ST_FOGMAP, "FOGMAP" },
			{ stageType_t::ST_LIGHTMAP, "LIGHTMAP" },
			{ stageType_t::ST_STYLELIGHTMAP, "STYLELIGHTMAP" },
			{ stageType_t::ST_STYLECOLORMAP, "STYLECOLORMAP" },
			{ stageType_t::ST_COLLAPSE_COLORMAP, "COLLAPSE_COLORMAP" },
			{ stageType_t::ST_COLLAPSE_DIFFUSEMAP, "COLLAPSE_DIFFUSEMAP" },
			{ stageType_t::ST_COLLAPSE_REFLECTIONMAP, "COLLAPSE_REFLECTIONMAP" },
			{ stageType_t::ST_ATTENUATIONMAP_XY, "ATTENUATIONMAP_XY" },
			{ stageType_t::ST_ATTENUATIONMAP_Z, "ATTENUATIONMAP_XZ" },
		};

		const char *prefix = args.Argc() > 1 ? args.Argv( 1 ).c_str() : nullptr;

		// Header names
		std::string num = "num";
		std::string shaderType = "shaderType";
		std::string shaderSort = "shaderSort";
		std::string stageType = "stageType";
		std::string interactLight = "interactLight";
		std::string stageNumber = "stageNumber";
		std::string shaderName = "shaderName";

		// Number sizes
		size_t numLen = 5;

		// Header number sizes
		numLen = std::max( numLen, num.length() );
		size_t shaderTypeLen = shaderType.length();
		size_t shaderSortLen = shaderSort.length();
		size_t stageTypeLen = stageType.length();
		size_t interactLightLen = interactLight.length();

		// Value size
		for ( const auto& kv : shaderTypeName )
		{
			shaderTypeLen = std::max( shaderTypeLen, kv.second.length() );
		}

		for ( const auto& kv : shaderSortName )
		{
			shaderSortLen = std::max( shaderSortLen, kv.second.length() );
		}

		for ( const auto& kv : stageTypeName )
		{
			stageTypeLen = std::max( stageTypeLen, kv.second.length() );
		}

		std::string separator = " ";
		std::stringstream lineStream;

		// Print header
		lineStream << std::left;
		lineStream << std::setw(numLen) << num << separator;
		lineStream << std::setw(shaderTypeLen) << shaderType << separator;
		lineStream << std::setw(shaderSortLen) << shaderSort << separator;
		lineStream << std::setw(stageTypeLen) << stageType << separator;
		lineStream << std::setw(interactLightLen) << interactLight << separator;
		lineStream << stageNumber << ":" << shaderName;

		std::string lineSeparator( lineStream.str().length(), '-' );

		Print( lineSeparator );
		Print( lineStream.str() );
		Print( lineSeparator );

		size_t totalStageCount = 0;
		size_t highestShaderStageCount = 0;

		for ( int i = 0; i < tr.numShaders; i++ )
		{
			shader_t *shader = args.Argc() > 2 ? tr.sortedShaders[ i ] : tr.shaders[ i ];

			// Only display shaders starting with prefix if prefix is not empty.
			if ( prefix && !Com_Filter( prefix, shader->name, false ) )
			{
				continue;
			}

			if ( !shaderTypeName.count( shader->type ) )
			{
				Log::Debug( "Undocumented shader type %i for shader %s",
					Util::ordinal( shader->type ), shader->name );
			}
			else
			{
				shaderType = shaderTypeName.at( shader->type );
			}

			if ( !shaderSortName.count( (shaderSort_t) shader->sort ) )
			{
				Log::Debug( "Undocumented shader sort %f for shader %s",
					shader->sort, shader->name );
			}
			else
			{
				shaderSort = shaderSortName.at( (shaderSort_t) shader->sort );
			}

			interactLight = shader->interactLight ? "INTERACTLIGHT" : "";
			shaderName = shader->name;
			shaderName += shader->defaultShader ? " (DEFAULTED)" : "";

			if ( shader->stages == shader->lastStage )
			{
				lineStream.clear();
				lineStream.str("");

				lineStream << std::left;
				lineStream << std::setw(numLen) << i << separator;
				lineStream << std::setw(shaderTypeLen) << shaderType << separator;
				lineStream << std::setw(shaderSortLen) << shaderSort << separator;
				lineStream << std::setw(stageTypeLen) << stageType << separator;
				lineStream << std::setw(interactLightLen) << interactLight << separator;
				lineStream << "-:" << shaderName;

				Print( lineStream.str() );
				continue;
			}

			const size_t stageCount = shader->lastStage - shader->stages;
			totalStageCount += stageCount;
			highestShaderStageCount = std::max( highestShaderStageCount, stageCount );

			for ( size_t j = 0; j < stageCount; j++ )
			{
				shaderStage_t *stage = &shader->stages[ j ];

				if ( !stageTypeName.count( stage->type ) )
				{
					Log::Debug( "Undocumented stage type %i for shader stage %s:%d",
						Util::ordinal( stage->type ), shader->name, j );
				}
				else
				{
					stageType = stageTypeName.at( stage->type );
				}

				lineStream.clear();
				lineStream.str("");

				lineStream << std::left;
				lineStream << std::setw(numLen) << i << separator;
				lineStream << std::setw(shaderTypeLen) << shaderType << separator;
				lineStream << std::setw(shaderSortLen) << shaderSort << separator;
				lineStream << std::setw(stageTypeLen) << stageType << separator;
				lineStream << std::setw(interactLightLen) << interactLight << separator;
				lineStream << j << ":" << shaderName;

				Print( lineStream.str() );
			}
		}

		Print( lineSeparator );
		Print( "%i total shaders, %i total stages, largest shader has %i stages",
			tr.numShaders, totalStageCount, highestShaderStageCount );
		Print( lineSeparator );
	}
};
static ListShadersCmd listShadersCmdRegistration;

class ShaderExpCmd : public Cmd::StaticCmd
{
public:
	ShaderExpCmd() : StaticCmd(
		"shaderexp", Cmd::RENDERER, "evaluate a q3shader expression (RB_EvalExpression)") {}

	void Run( const Cmd::Args &args ) const override
	{
		std::string expStr = args.ConcatArgs( 1 );
		const char* buffer_p = expStr.c_str();
		expression_t exp;

		ParseExpression( &buffer_p, &exp );

		Print( "%i total ops", exp.numOps );
		Print( "%f result", RB_EvalExpression( &exp, 0 ) );
	}
};
static ShaderExpCmd shaderExpCmdRegistration;

/*
====================
ScanAndLoadShaderFiles

Finds and loads all .shader files, combining them into
a single large text block that can be scanned for shader names
=====================
*/
static void ScanAndLoadShaderFiles()
{
	std::vector<std::string> buffers;
	const char *p;
	const char *oldp, *token;
	char *textEnd;
	const char **hashMem;
	int  shaderTextHashTableSizes[ MAX_SHADERTEXT_HASH ], hash, size;
	char filename[ MAX_QPATH ];
	size_t sum = 0;

	Log::Debug("----- ScanAndLoadShaderFiles -----" );

	// load and parse shader files
	for ( const std::string& basename : FS::PakPath::ListFiles("scripts") )
	{
		if ( !Str::IsISuffix( ".shader", basename ) )
		{
			continue;
		}

		Com_sprintf( filename, sizeof( filename ), "scripts/%s", basename.c_str() );

		Log::Debug("loading '%s' shader file", filename );
		std::error_code err;
		std::string buffer = FS::PakPath::ReadFile( filename, err );

		if ( err )
		{
			Log::Warn( "Couldn't load shader file %s", filename );
			continue;
		}

		p = buffer.c_str();
		bool syntaxError = false;

		while ( true )
		{
			token = COM_ParseExt2( &p, true );

			if ( !*token )
			{
				break;
			}

			// Step over the "table" and the name
			if ( !Q_stricmp( token, "table" ) )
			{
				token = COM_ParseExt2( &p, true );

				if ( !*token )
				{
					break;
				}
			}

			token = COM_ParseExt2( &p, true );

			if ( token[ 0 ] != '{' || token[ 1 ] != '\0' || !SkipBracedSection_Depth( &p, 1 ) )
			{
				Log::Warn("Bad shader file %s has incorrect syntax.", filename );
				syntaxError = true;
				break;
			}
		}

		if ( !syntaxError )
		{
			sum += buffer.size();
			buffers.push_back( std::move( buffer ) );
		}
	}

	// build single large buffer
	s_shaderText = (char*) ri.Hunk_Alloc( sum + buffers.size() * 2, ha_pref::h_low );
	s_shaderText[ 0 ] = '\0';
	textEnd = s_shaderText;

	for ( auto i = buffers.rbegin(); i != buffers.rend(); ++i )
	{
		strcat( textEnd, i->c_str() );
		strcat( textEnd, "\n" );
		textEnd += strlen( textEnd );
	}

	// ydnar: unixify all shaders
	COM_FixPath( s_shaderText );

	COM_Compress( s_shaderText );

	memset( shaderTextHashTableSizes, 0, sizeof( shaderTextHashTableSizes ) );
	size = 0;

	p = s_shaderText;

	// look for shader names
	while ( true )
	{
		token = COM_ParseExt2( &p, true );

		if ( token[ 0 ] == 0 )
		{
			break;
		}

		// skip shader tables
		if ( !Q_stricmp( token, "table" ) )
		{
			// skip table name
			token = COM_ParseExt2( &p, true );

			SkipBracedSection( &p );
		}
		else
		{
			hash = generateHashValue( token, MAX_SHADERTEXT_HASH );
			shaderTextHashTableSizes[ hash ]++;
			size++;
			SkipBracedSection( &p );
		}
	}

	size += MAX_SHADERTEXT_HASH;

	hashMem = (const char**) ri.Hunk_Alloc( size * sizeof( char * ), ha_pref::h_low );

	for ( int i = 0; i < MAX_SHADERTEXT_HASH; i++ )
	{
		shaderTextHashTable[ i ] = hashMem;
		hashMem += shaderTextHashTableSizes[ i ] + 1;
	}

	memset( shaderTextHashTableSizes, 0, sizeof( shaderTextHashTableSizes ) );

	p = s_shaderText;

	// look for shader names
	while ( true )
	{
		oldp = p;
		token = COM_ParseExt( &p, true );

		if ( token[ 0 ] == 0 )
		{
			break;
		}

		// parse shader tables
		if ( !Q_stricmp( token, "table" ) )
		{
			int           depth;
			float         values[ FUNCTABLE_SIZE ];
			int           numValues;
			shaderTable_t *tb;
			bool      alreadyCreated;

			// zeroes shader table, booleans can be assumed as false
			table = {};

			token = COM_ParseExt2( &p, true );

			Q_strncpyz( table.name, token, sizeof( table.name ) );

			// check if already created
			alreadyCreated = false;
			hash = generateHashValue( table.name, MAX_SHADERTABLE_HASH );

			for ( tb = shaderTableHashTable[ hash ]; tb; tb = tb->next )
			{
				if ( Q_stricmp( tb->name, table.name ) == 0 )
				{
					// match found
					alreadyCreated = true;
					break;
				}
			}

			depth = 0;
			numValues = 0;

			do
			{
				token = COM_ParseExt2( &p, true );

				if ( !Q_stricmp( token, "snap" ) )
				{
					table.snap = true;
				}
				else if ( !Q_stricmp( token, "clamp" ) )
				{
					table.clamp = true;
				}
				else if ( token[ 0 ] == '{' )
				{
					depth++;
				}
				else if ( token[ 0 ] == '}' )
				{
					depth--;
				}
				else if ( token[ 0 ] == ',' )
				{
					continue;
				}
				else
				{
					if ( numValues == FUNCTABLE_SIZE )
					{
						Log::Warn("FUNCTABLE_SIZE hit" );
						break;
					}

					values[ numValues++ ] = atof( token );
				}
			}
			while ( depth && p );

			if ( !alreadyCreated )
			{
				Log::Debug("...generating '%s'", table.name );
				GeneratePermanentShaderTable( values, numValues );
			}
		}
		else
		{
			hash = generateHashValue( token, MAX_SHADERTEXT_HASH );
			shaderTextHashTable[ hash ][ shaderTextHashTableSizes[ hash ]++ ] = oldp;

			SkipBracedSection( &p );
		}
	}
}

/*
====================
CreateInternalShaders
====================
*/
static void CreateInternalShaders()
{
	Log::Debug("----- CreateInternalShaders -----" );

	tr.numShaders = 0;

	ClearGlobalShader();

	Q_strncpyz( shader.name, "<default>", sizeof( shader.name ) );

	shader.type = shaderType_t::SHADER_3D_DYNAMIC;
	shader.noFog = true;
	shader.fogShader = nullptr;
	stages[ 0 ].type = stageType_t::ST_DIFFUSEMAP;
	stages[ 0 ].bundle[ 0 ].image[ 0 ] = tr.defaultImage;
	stages[ 0 ].active = true;
	stages[ 0 ].stateBits = GLS_DEFAULT;
	tr.defaultShader = FinishShader();

	Q_strncpyz( shader.name, "<fogEqual>", sizeof( shader.name ) );

	shader.type = shaderType_t::SHADER_3D_DYNAMIC;
	shader.sort = Util::ordinal( shaderSort_t::SS_FOG );
	stages[0].type = stageType_t::ST_FOGMAP;
	for ( int i = 0; i < 5; i++ ) {
		stages[0].bundle[i].image[0] = nullptr;
	}
	stages[0].active = true;
	stages[0].stateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL;
	tr.fogEqualShader = FinishShader();

	Q_strncpyz( shader.name, "<fogLE>", sizeof( shader.name ) );

	shader.type = shaderType_t::SHADER_3D_DYNAMIC;
	shader.sort = Util::ordinal( shaderSort_t::SS_FOG );
	stages[0].type = stageType_t::ST_FOGMAP;
	for ( int i = 0; i < 5; i++ ) {
		stages[0].bundle[i].image[0] = nullptr;
	}
	stages[0].active = true;
	stages[0].stateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
	tr.fogLEShader = FinishShader();
}

static void CreateExternalShaders()
{
	Log::Debug("----- CreateExternalShaders -----" );

	tr.defaultPointLightShader = R_FindShader( "lights/defaultPointLight", shaderType_t::SHADER_LIGHT, RSF_DEFAULT );
	tr.defaultProjectedLightShader = R_FindShader( "lights/defaultProjectedLight", shaderType_t::SHADER_LIGHT, RSF_DEFAULT );
	tr.defaultDynamicLightShader = R_FindShader( "lights/defaultDynamicLight", shaderType_t::SHADER_LIGHT, RSF_DEFAULT );
}

/*
==================
R_InitShaders
==================
*/
void R_InitShaders()
{
	Cvar::Latch(r_dpMaterial);
	Cvar::Latch(r_depthShaders);
	Cvar::Latch(r_portalDefaultRange);

	memset( shaderTableHashTable, 0, sizeof( shaderTableHashTable ) );
	memset( shaderHashTable, 0, sizeof( shaderHashTable ) );

	CreateInternalShaders();

	ScanAndLoadShaderFiles();

	CreateExternalShaders();
}

/*
==================
R_SetAltShaderTokens
==================
*/
void R_SetAltShaderTokens( const char *list )
{
	memset( whenTokens, 0, sizeof( whenTokens ) );
	Q_strncpyz( whenTokens, list, sizeof( whenTokens ) - 1 ); // will have double-NUL termination

	for ( char* p = whenTokens; ( p = strchr( p, ',' ) ); p++ )
	{
		*p = '\0';
	}
}

/*
==================
RE_GetShaderNameFromHandle
==================
*/

const char *RE_GetShaderNameFromHandle( qhandle_t shader )
{
	return R_GetShaderByHandle( shader )->name;
}
