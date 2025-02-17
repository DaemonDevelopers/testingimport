/*
===========================================================================

Daemon GPL Source Code

Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.
Copyright (C) 2010 Robert Beckebans

This file is part of the Daemon GPL Source Code (Daemon Source Code).

Daemon Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Daemon Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Daemon Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following the
terms and conditions of the GNU General Public License which accompanied the Daemon
Source Code.  If not, please request a copy in writing from id Software at the address
below.

If you have questions concerning this license or the applicable additional terms, you
may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville,
Maryland 20850 USA.

===========================================================================
*/

// This file is part of the VM ABI. Changes here may cause incompatibilities.

#ifndef __TR_TYPES_H
#define __TR_TYPES_H

// bool can't be safely deserialized by memcpy
#pragma push_macro("bool")
#undef bool
#define bool DO_NOT_USE_BOOL_IN_IPC_MESSAGE_TYPES
using bool8_t = uint8_t;

// XreaL BEGIN
#define MAX_REF_LIGHTS     1024
#define MAX_REF_ENTITIES   1023 // can't be increased without changing drawsurf bit packing
#define MAX_BONES          256
#define MAX_WEIGHTS        4 // GPU vertex skinning limit, never change this without rewriting many GLSL shaders
// XreaL END

#define MAX_ENTITIES       MAX_REF_ENTITIES // RB: for compatibility

// renderfx flags
#define RF_MINLIGHT        0x000001 // always have some light (viewmodel, some items)
#define RF_THIRD_PERSON    0x000002 // don't draw through eyes, only mirrors (player bodies, chat sprites)
#define RF_FIRST_PERSON    0x000004 // only draw through eyes (view weapon, damage blood blob)
#define RF_DEPTHHACK       0x000008 // for view weapon Z crunching
#define RF_NOSHADOW        0x000010 // don't add stencil shadows

#define RF_LIGHTING_ORIGIN 0x000020 // use refEntity->lightingOrigin instead of refEntity->origin
// for lighting.  This allows entities to sink into the floor
// with their origin going solid, and allows all parts of a
// player to get the same lighting
#define RF_SWAPCULL      0x000040 // swap CT_FRONT_SIDED and CT_BACK_SIDED

// refdef flags
#define RDF_NOWORLDMODEL ( 1 << 0 ) // used for player configuration screen
#define RDF_HYPERSPACE   ( 1 << 1 ) // teleportation effect

// Rafael
#define RDF_SKYBOXPORTAL ( 1 << 2 )

// XreaL BEGIN
#define RDF_NOCUBEMAP    ( 1 << 3 ) // RB: don't use cubemaps
#define RDF_NOBLOOM      ( 1 << 4 ) // RB: disable bloom. useful for HUD models
// XreaL END

#define MAX_ALTSHADERS   64 // alternative shaders ('when <condition> <shader>') – selection controlled from cgame

#define GL_INDEX_TYPE GL_UNSIGNED_INT
using glIndex_t = unsigned int;

enum RegisterShaderFlags_t {
	RSF_DEFAULT = BIT( 0 ),
	RSF_2D = BIT( 1 ),
	RSF_NOMIP = BIT( 2 ),
	RSF_FITSCREEN = BIT( 3 ),
	RSF_LIGHT_ATTENUATION = BIT( 4 ),
	RSF_NOLIGHTSCALE = BIT( 5 ), // TODO(0.56): delete, does nothing
	RSF_SPRITE = BIT( 6 ),
};

struct polyVert_t
{
	vec3_t xyz;
	float  st[ 2 ];
	byte   modulate[ 4 ];
};

struct poly_t
{
	qhandle_t  hShader;
	int        numVerts;
	polyVert_t *verts;
};

enum class refEntityType_t
{
  RT_MODEL,

  // A square 3D polygon at the specified `origin` with size `radius` with its face
  // oriented toward the viewer. By default the square's edges will be aligned so they are
  // drawn parallel to the edges of the screen; this can be changed by setting `rotation`.
  RT_SPRITE,

  RT_PORTALSURFACE, // doesn't draw anything, just info for portals

  RT_MAX_REF_ENTITY_TYPE
};

// XreaL BEGIN

// RB: defining any of the following macros would break the compatibility to old ET mods
//#define USE_REFENTITY_NOSHADOWID 1

// RB: having bone names for each refEntity_t takes several MiBs
// in backEndData_t so only use it for debugging and development
// enabling this will show the bone names with r_showSkeleton 1

struct refBone_t
{
#if defined( REFBONE_NAMES )
	char   name[ 64 ];
#endif
	short  parentIndex; // parent index (-1 if root)
	transform_t t;
};

enum class refSkeletonType_t
{
  SK_INVALID,
  SK_RELATIVE,
  SK_ABSOLUTE
};

struct alignas(16) refSkeleton_t
{
	refSkeletonType_t type; // skeleton has been reset

	unsigned short numBones;

	vec3_t            bounds[ 2 ]; // bounds of all applied animations
	vec_t             scale;

	refBone_t         bones[ MAX_BONES ];
};

// XreaL END

struct md5Channel_t {
	char     name[MAX_QPATH];
	int8_t   parentIndex;

	uint8_t  componentsBits; // e.g. (COMPONENT_BIT_TX | COMPONENT_BIT_TY | COMPONENT_BIT_TZ)
	uint16_t componentsOffset;

	vec3_t   baseOrigin;
	quat_t   baseQuat;
};

struct md5Frame_t {
	vec3_t bounds[2]; // bounds of all surfaces of all LODs for this frame
	float* components; // numAnimatedComponents many
};

struct md5Animation_t {
	uint16_t     numFrames;
	md5Frame_t* frames;

	uint8_t      numChannels; // same as numBones in model
	md5Channel_t* channels;

	int16_t      frameRate;

	uint32_t     numAnimatedComponents;
};

struct IQAnim_t {
	int             num_frames;
	int             num_joints;
	int             framerate;
	int             flags;

	// skeleton data
	int* jointParents;
	transform_t* poses;
	float* bounds;
	char* name;
	char* jointNames;
};

enum {
	IQM_LOOP = 1 << 0
};

enum class animType_t {
	AT_BAD,
	AT_MD5,
	AT_IQM,
};

enum {
	COMPONENT_BIT_TX = 1 << 0,
	COMPONENT_BIT_TY = 1 << 1,
	COMPONENT_BIT_TZ = 1 << 2,
	COMPONENT_BIT_QX = 1 << 3,
	COMPONENT_BIT_QY = 1 << 4,
	COMPONENT_BIT_QZ = 1 << 5
};

struct skelAnimation_t {
	char           name[MAX_QPATH]; // game path, including extension
	animType_t     type;
	int            index; // anim = tr.animations[anim->index]

	union {
		md5Animation_t* md5;
		IQAnim_t* iqm;
	};
};

struct refEntity_t
{
	refEntityType_t reType;
	int             renderfx;

	qhandle_t       hModel; // opaque type outside refresh

	// most recent data
	vec3_t    lightingOrigin; // so multi-part models can be lit identically (RF_LIGHTING_ORIGIN)

	vec3_t    axis[ 3 ]; // rotation vectors
	bool8_t  nonNormalizedAxes; // axis are not normalized, i.e. they have scale
	vec3_t    origin;
	int       frame;

	// previous data for frame interpolation
	vec3_t    oldorigin; // also used as MODEL_BEAM's "to"
	int       oldframe;
	float     backlerp; // 0.0 = current, 1.0 = old

	// texturing
	int       skinNum; // inline skin index
	qhandle_t customSkin; // nullptr for default skin
	qhandle_t customShader; // use one image for the entire thing

	// misc
	Color::Color32Bit shaderRGBA; // colors used by rgbgen entity shaders
	float shaderTexCoord[ 2 ]; // texture coordinates used by tcMod entity modifiers
	float shaderTime; // subtracted from refdef time to control effect start times

	// extra sprite information
	float radius;
	float rotation;

#if defined( USE_REFENTITY_NOSHADOWID )
	// extra light interaction information
	short noShadowID;
#endif

	int altShaderIndex;

	// KEEP SKELETON AT THE END OF THE STRUCTURE
	// it is to make a serialization hack for refEntity_t easier
	// by memcpying up to skeleton and then serializing skeleton
	refSkeleton_t skeleton;

};

// ================================================================================================

// XreaL BEGIN

enum class refLightType_t
{
  RL_OMNI, // point light
  RL_PROJ, // spot light
  RL_DIRECTIONAL, // sun light

  RL_MAX_REF_LIGHT_TYPE
};

struct refLight_t
{
	refLightType_t rlType;
//  int             lightfx;

	qhandle_t attenuationShader;

	vec3_t    origin;
	quat_t    rotation;
	vec3_t    center;
	vec3_t    color; // should be color normalized

	// omni-directional light specific
	float     radius;

	// projective light specific
	vec3_t   projTarget;
	vec3_t   projRight;
	vec3_t   projUp;
	vec3_t   projStart;
	vec3_t   projEnd;

	bool8_t noShadows;
	short    noShadowID; // don't cast shadows of all entities with this id

	bool8_t inverseShadows; // don't cast light and draw shadows by darken the scene
	// this is useful for drawing player shadows with shadow mapping
};

// XreaL END

// ================================================================================================

struct refdef_t
{
	int    x, y, width, height;
	float  fov_x, fov_y;
	vec3_t vieworg;
	vec3_t viewaxis[ 3 ]; // transformation matrix
	vec3_t blurVec;       // motion blur direction

	int    time; // time in milliseconds for shader effects and other time dependent rendering issues
	int    rdflags; // RDF_NOWORLDMODEL, etc

	// 1 bits will prevent the associated area from rendering at all
	byte areamask[ MAX_MAP_AREA_BYTES ];

	vec4_t  gradingWeights;
};

// XreaL BEGIN

// cg_shadows modes
enum class shadowingMode_t
{
  SHADOWING_NONE,
  SHADOWING_BLOB,
  SHADOWING_ESM16,
  SHADOWING_ESM32,
  SHADOWING_VSM16,
  SHADOWING_VSM32,
  SHADOWING_EVSM32,
};
// XreaL END

enum class textureCompression_t
{
  TC_NONE,
  TC_S3TC,
  TC_EXT_COMP_S3TC
};

// Keep the list in sdl_glimp.c:reportDriverType in sync with this
enum class glDriverType_t
{
  GLDRV_UNKNOWN = -1,
  GLDRV_ICD, // driver is integrated with window system
  // WARNING: there are tests that check for
  // > GLDRV_ICD for minidriverness, so this
  // should always be the lowest value in this
  // enum set
  GLDRV_STANDALONE, // driver is a non-3Dfx standalone driver

// XreaL BEGIN
  GLDRV_OPENGL3, // new driver system
// XreaL END
};

// Keep the list in sdl_glimp.c:reportHardwareType in sync with this
enum class glHardwareType_t
{
  GLHW_UNKNOWN = -1,
  GLHW_GENERIC, // where everthing works the way it should

// XreaL BEGIN
  GLHW_R300, // pre-GL3 ATI hack
// XreaL END
};

/**
 * Contains variables specific to the OpenGL configuration
 * being run right now.  These are constant once the OpenGL
 * subsystem is initialized.
 */
struct glconfig_t
{
	char                 renderer_string[ MAX_STRING_CHARS ];
	char                 vendor_string[ MAX_STRING_CHARS ];
	char                 version_string[ MAX_STRING_CHARS ];

	int                  maxTextureSize; // queried from GL

	int colorBits;

	glDriverType_t       driverType;
	glHardwareType_t     hardwareType;

	textureCompression_t textureCompression;

	int displayIndex;
	float displayAspect;
	int displayWidth, displayHeight; // the entire monitor (the one indicated by displayIndex)
	int vidWidth, vidHeight; // what the game is using

	bool8_t smpActive; // dual processor
};

#pragma pop_macro("bool")

#endif // __TR_TYPES_H
