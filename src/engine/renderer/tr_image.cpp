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
// tr_image.c
#include <common/FileSystem.h>
#include "InternalImage.h"
#include "tr_local.h"
#include <iomanip>
#include "Material.h"

int                  gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
int                  gl_filter_max = GL_LINEAR;

image_t              *r_imageHashTable[ IMAGE_FILE_HASH_SIZE ];

#define Tex_ByteToFloat(v) ( ( (int)(v) - 128 ) / 127.0f )
#define Tex_FloatToByte(v) ( 128 + (int) ( (v) * 127.0f + 0.5 ) )

struct textureMode_t
{
	const char *name;
	int  minimize, maximize;
};

static const textureMode_t modes[] =
{
	{ "GL_NEAREST",                GL_NEAREST,                GL_NEAREST },
	{ "GL_LINEAR",                 GL_LINEAR,                 GL_LINEAR  },
	{ "GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_NEAREST",  GL_LINEAR_MIPMAP_NEAREST,  GL_LINEAR  },
	{ "GL_NEAREST_MIPMAP_LINEAR",  GL_NEAREST_MIPMAP_LINEAR,  GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_LINEAR",   GL_LINEAR_MIPMAP_LINEAR,   GL_LINEAR  }
};

/*
================
return a hash value for the filename
================
*/
unsigned int GenerateImageHashValue( const char *fname )
{
	int  i;
	unsigned hash;
	char letter;

	// better use Log::CommandInteractionMessage
	// Log::Notice("tr_image::GenerateImageHashValue: '%s'", fname);

	hash = 0;
	i = 0;

	while ( fname[ i ] != '\0' )
	{
		letter = Str::ctolower( fname[ i ] );

		if ( letter == '\\' )
		{
			letter = '/'; // damn path names
		}

		hash += ( unsigned )( letter ) * ( i + 119 );
		i++;
	}

	hash &= ( IMAGE_FILE_HASH_SIZE - 1 );
	return hash;
}

/*
===============
GL_TextureMode
===============
*/
void GL_TextureMode( const char *string )
{
	int     i;

	for ( i = 0; i < 6; i++ )
	{
		if ( !Q_stricmp( modes[ i ].name, string ) )
		{
			break;
		}
	}

	if ( i == 6 )
	{
		Log::Warn("bad filter name" );
		return;
	}

	gl_filter_min = modes[ i ].minimize;
	gl_filter_max = modes[ i ].maximize;

	if ( glConfig2.usingBindlessTextures && !tr.images.empty() )
	{
		Log::Notice( "Changing filter type of existing bindless textures requires a restart" );
		return;
	}

	// change all the existing mipmap texture objects
	for ( image_t *image : tr.images )
	{
		if ( image->filterType == filterType_t::FT_DEFAULT )
		{
			GL_Bind( image );

			// set texture filter
			glTexParameterf( image->type, GL_TEXTURE_MIN_FILTER, gl_filter_min );
			glTexParameterf( image->type, GL_TEXTURE_MAG_FILTER, gl_filter_max );

			// set texture anisotropy
			if ( glConfig2.textureAnisotropyAvailable )
			{
				glTexParameterf( image->type, GL_TEXTURE_MAX_ANISOTROPY_EXT, glConfig2.textureAnisotropy );
			}
		}
	}
}

class ListImagesCmd : public Cmd::StaticCmd
{
public:
	ListImagesCmd() : StaticCmd("listImages", Cmd::RENDERER, "list images loaded in renderer") {}

	void Run( const Cmd::Args &args ) const override
	{
		static const std::unordered_map<GLenum, std::string> imageTypeName = {
			{ GL_TEXTURE_2D, "2D" },
			{ GL_TEXTURE_3D, "3D" },
			{ GL_TEXTURE_CUBE_MAP, "CUBE" },
		};

		typedef std::pair<const std::string, int> nameSizePair;
		static const std::unordered_map<uint32_t, nameSizePair> imageFormatNameSize = {
			/* Generic format that does not have any specified actual format
			and implementations can choose one of a few different ones
			GL4.6 spec (https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf):
			8.5. TEXTURE IMAGE SPECIFICATION
			"If internalformat is specified as a base internal format, the GL stores the resulting texture with
			 internal component resolutions of its own choosing, referred to as the effective internal format."
			 Use 4 bytes as an estimate: */
			{ GL_RGBA, { "RGBA", 4 } },

			{ GL_RGB8, { "RGB8", 3 } },
			{ GL_RGBA8, { "RGBA8", 4 } },
			{ GL_RGB16, { "RGB16", 6 } },
			{ GL_RGBA16, { "RGBA16", 8 } },
			{ GL_RGB16F, { "RGB16F", 6 } },
			{ GL_RGB32F, { "RGB32F", 12 } },
			{ GL_RGBA16F, { "RGBA16F", 8 } },
			{ GL_RGBA32F, { "RGBA32F", 16 } },
			{ GL_RGBA32UI, { "RGBA32UI", 16 } },
			{ GL_ALPHA16F_ARB, { "A16F", 2 } },
			{ GL_ALPHA32F_ARB, { "A32F", 4 } },
			{ GL_R16F, { "R16F", 2 } },
			{ GL_R32F, { "R32F", 4 } },
			{ GL_LUMINANCE_ALPHA16F_ARB, { "LA16F", 4 } },
			{ GL_LUMINANCE_ALPHA32F_ARB, { "LA32F", 8 } },
			{ GL_RG16F, { "RG16F", 4 } },
			{ GL_RG32F, { "RG32F", 8 } },

			// Compressed formats here use imageDataSize multiplier as blocksize
		
			/* Generic compressed format that does not have any specified actual format
			and implementations can compress it in whatever way
			GL4.6 spec (https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf):
			8.5. TEXTURE IMAGE SPECIFICATION
			"Generic compressed internal formats are never used directly as the internal formats of texture images.
			 If internalformat is one of the six generic compressed internal formats, its value is replaced by the symbolic constant
			 for a specific compressed internal format of the GL’s choosing with the same base internal format."
			 Use 4x4 blocks with 8 bytes per block here as an estimate: */
			{ GL_COMPRESSED_RGBA, { "RGBAC", 8 } },

			/* https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_compression_s3tc.txt
			S3TC Compressed Texture Image Formats
			"Compressed texture images stored using the S3TC compressed image formats
			 are represented as a collection of 4x4 texel blocks, where each block
			 contains 64 or 128 bits of texel data.  The image is encoded as a normal
			 2D raster image in which each 4x4 block is treated as a single pixel.  If
			 an S3TC image has a width or height that is not a multiple of four, the
			 data corresponding to texels outside the image are irrelevant and
			 undefined.

	  		 When an S3TC image with a width of w, height of h, and block size of
			 blocksize (8 or 16 bytes) is decoded, the corresponding image size (in
			 bytes) is: ceil( w / 4 ) * ceil( h / 4 ) * blocksize."

			All of the following have blocksize of 8 bytes: */
			{ GL_COMPRESSED_RGB_S3TC_DXT1_EXT, { "DXT1", 8 } },
			{ GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, { "DXT1a", 8 } },
			{ GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, { "DXT3", 8 } },
			{ GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, { "DXT5", 8 } },

			/* GL4.6 spec: Appendix D Compressed Texture Image Formats
			D.1 RGTC Compressed Texture Image Formats (https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf)
			Khronos Data Format Specification v1.1 rev 9
			14. RGTC Compressed Texture Image Formats (https://registry.khronos.org/DataFormat/specs/1.1/dataformat.1.1.html#RGTC)
			"Compressed texture images stored using the RGTC compressed image encodings are represented as a collection of 4×4 texel blocks,
			 where each block contains 64 or 128 bits of texel data. The image is encoded as a normal 2D raster image in which each 4×4 block
			 is treated as a single pixel. If an RGTC image has a width or height that is not a multiple of four,
			 the data corresponding to texels outside the image are irrelevant and undefined.
		   
			 When an RGTC image with a width of w, height of h, and block size of blocksize (8 or 16 bytes) is decoded,
			 the corresponding image size (in bytes) is: ceil( w / 4 ) * ceil( h / 4 ) * blocksize."

			All of the following use blocksize of 8 bytes: */
			{ GL_COMPRESSED_RED_RGTC1, { "RGTC1r", 8 } },
			{ GL_COMPRESSED_SIGNED_RED_RGTC1, { "RGTC1Sr", 8 } },
			{ GL_COMPRESSED_RG_RGTC2, { "RGTC2rg", 8 } },
			{ GL_COMPRESSED_SIGNED_RG_RGTC2, { "RGTC2Srg", 8 } },

			// These usually still use a 32-bit format internally
			{ GL_DEPTH_COMPONENT16, { "D16", 2 } },
			{ GL_DEPTH_COMPONENT24, { "D24", 3 } },
			{ GL_DEPTH_COMPONENT32, { "D32", 4 } },
			{ GL_DEPTH24_STENCIL8, { "D24S8", 4 } },
		};

		static const std::unordered_map<wrapTypeEnum_t, std::string> wrapTypeName = {
			{ wrapTypeEnum_t::WT_REPEAT, "rept" },
			{ wrapTypeEnum_t::WT_CLAMP, "clmp" },
			{ wrapTypeEnum_t::WT_EDGE_CLAMP, "eclmp" },
			{ wrapTypeEnum_t::WT_ZERO_CLAMP, "0clmp" },
			{ wrapTypeEnum_t::WT_ONE_CLAMP, "1clmp" },
			{ wrapTypeEnum_t::WT_ALPHA_ZERO_CLAMP, "a0clmp" },
		};

		const char *yesno[] = { "no", "yes" };
		const char *filter = args.Argc() > 1 ? args.Argv( 1 ).c_str() : nullptr;

		// Header names
		std::string num = "num";
		std::string width = "width";
		std::string height = "height";
		std::string layers = "layers";
		std::string mm = "mm";
		std::string type = "type";
		std::string format = "format";
		std::string twrap = "wrap.t";
		std::string swrap = "wrap.s";
		std::string name = "name";

		// Number sizes
		size_t numLen = 5;
		size_t widthLen = 5;
		size_t heightLen = 5;
		size_t layersLen = 5;
		size_t mmLen = 3;
		size_t typeLen = 4;
		size_t formatLen = 4;
		size_t twrapLen = 4;
		size_t swrapLen = 4;

		// Header number sizes
		numLen = std::max( numLen, num.length() );
		widthLen = std::max( widthLen, width.length() );
		heightLen = std::max( heightLen, height.length() );
		layersLen = std::max( layersLen, layers.length() );
		mmLen = std::max( mmLen, mm.length() );
		typeLen = std::max( typeLen, type.length() );
		formatLen = std::max( formatLen, format.length() );
		twrapLen = std::max( twrapLen, twrap.length() );
		swrapLen = std::max( swrapLen, swrap.length() );

		// Value sizes
		for ( const auto& kv : imageTypeName )
		{
			typeLen = std::max( typeLen, kv.second.length() );
		}
	
		for ( const auto& kv : imageFormatNameSize )
		{
			formatLen = std::max( formatLen, kv.second.first.length() );
		}

		for ( const auto& kv: wrapTypeName )
		{
			// 2 for the "t." and "s." prefix length.
			twrapLen = std::max( twrapLen, 2 + kv.second.length() );
			swrapLen = std::max( swrapLen, 2 + kv.second.length() );
		}

		std::string separator = " ";
		std::stringstream lineStream;

		// Print header
		lineStream << std::left;
		lineStream << std::setw(numLen) << num << separator;
		lineStream << std::right;
		lineStream << std::setw(widthLen) << width << separator;
		lineStream << std::setw(heightLen) << height << separator;
		lineStream << std::setw(layersLen) << layers << separator;
		lineStream << std::left;
		lineStream << std::setw(mmLen) << mm << separator;
		lineStream << std::setw(typeLen) << type << separator;
		lineStream << std::setw(formatLen) << format << separator;
		lineStream << std::setw(twrapLen) << twrap << separator;
		lineStream << std::setw(swrapLen) << swrap << separator;
		lineStream << name;

		std::string lineSeparator( lineStream.str().length(), '-' );

		Print( lineSeparator );
		Print( lineStream.str() );
		Print( lineSeparator );

		int texels = 0;
		int dataSize = 0;

		for ( size_t i = 0; i < tr.images.size(); i++ )
		{
			const image_t *image = tr.images[ i ];
			if ( filter && !Com_Filter( filter, image->name, true ) )
			{
				continue;
			}

			mm = yesno[ image->filterType == filterType_t::FT_DEFAULT ];

			if ( !imageTypeName.count( image->type ) )
			{
				Log::Debug( "Undocumented image type %i (%X) for image %s",
					image->type, image->type, image->name );

				type = Str::Format( "0x%4X", image->type );
			}
			else
			{
				type = imageTypeName.at( image->type );
			}

			int imageDataSize = 0;
			texels += imageDataSize;
		
			if ( !imageFormatNameSize.count( image->internalFormat ) )
			{
				Log::Debug( "Undocumented image format %i (%X) for image %s",
					image->internalFormat, image->internalFormat, image->name );

				format = Str::Format( "0x%04X", image->internalFormat);
				imageDataSize *= 4;
			}
			else
			{
				switch ( image->internalFormat ) {
					// Compressed formats encode blocks of 4x4 texels
					case GL_COMPRESSED_RGBA:
					case GL_COMPRESSED_RED_RGTC1:
					case GL_COMPRESSED_SIGNED_RED_RGTC1:
					case GL_COMPRESSED_RG_RGTC2:
					case GL_COMPRESSED_SIGNED_RG_RGTC2:
					case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
					case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
					case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
					case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
					{
						format = imageFormatNameSize.at( image->internalFormat ).first;

						uint16_t imageWidth = image->uploadWidth;
						uint16_t imageHeight = image->uploadHeight;
						uint16_t imageLayers = image->numLayers;

						int numMips = 1;
						if ( image->filterType == filterType_t::FT_DEFAULT ) {
							numMips = log2f( std::max( std::max( imageWidth, imageHeight ), imageLayers ) ) + 1;
						}

						for ( int j = 0; j < numMips; j++ ) {
							imageDataSize += ceil( imageWidth / 4.0 ) * ceil( imageHeight / 4.0 ) * imageLayers;
							imageWidth >>= imageWidth > 1 ? 1 : 0;
							imageHeight >>= imageHeight > 1 ? 1 : 0;
							imageLayers >>= imageLayers > 1 ? 1 : 0;
						}

						imageDataSize *= imageFormatNameSize.at( image->internalFormat ).second;
						break;
					}
					default:
					{
						format = imageFormatNameSize.at( image->internalFormat ).first;

						uint16_t imageWidth = image->uploadWidth;
						uint16_t imageHeight = image->uploadHeight;
						uint16_t imageLayers = image->numLayers;

						int numMips = 1;
						if ( image->filterType == filterType_t::FT_DEFAULT ) {
							numMips = log2f( std::max( std::max( imageWidth, imageHeight ), imageLayers ) ) + 1;
						}
					
						for ( int j = 0; j < numMips; j++ ) {
							imageDataSize += imageWidth * imageHeight * imageLayers;
							imageWidth >>= imageWidth > 1 ? 1 : 0;
							imageHeight >>= imageHeight > 1 ? 1 : 0;
							imageLayers >>= imageLayers > 1 ? 1 : 0;
						}

						imageDataSize *= imageFormatNameSize.at( image->internalFormat ).second;
						break;
					}
				}
			}

			if ( !wrapTypeName.count( image->wrapType.t ) )
			{
				Log::Debug( "Undocumented wrapType.t %i for image %s",
					Util::ordinal(image->wrapType.t), image->name );

				twrap = Str::Format( "t.%4i", Util::ordinal(image->wrapType.t) );
			}
			else
			{
				twrap = "t." + wrapTypeName.at( image->wrapType.t );
			}

			if ( !wrapTypeName.count( image->wrapType.s ) )
			{
				Log::Debug( "Undocumented wrapType.s %i for image %s",
					Util::ordinal(image->wrapType.s), image->name );

				swrap = Str::Format( "s.%4i", Util::ordinal(image->wrapType.s) );
			}
			else
			{
				swrap = "s." + wrapTypeName.at( image->wrapType.s );
			}

			name = image->name;

			lineStream.clear();
			lineStream.str("");

			lineStream << std::left;
			lineStream << std::setw(numLen) << i << separator;
			lineStream << std::right;
			lineStream << std::setw(widthLen) << image->uploadWidth << separator;
			lineStream << std::setw(heightLen) << image->uploadHeight << separator;
			lineStream << std::setw(layersLen) << image->numLayers << separator;
			lineStream << std::left;
			lineStream << std::setw(mmLen) << mm << separator;
			lineStream << std::setw(typeLen) << type << separator;
			lineStream << std::setw(formatLen) << format << separator;
			lineStream << std::setw(twrapLen) << twrap << separator;
			lineStream << std::setw(swrapLen) << swrap << separator;
			lineStream << name;

			Print( lineStream.str() );

			dataSize += imageDataSize;
		}

		Print( lineSeparator );
		Print( "%i total texels (not including mipmaps)", texels );
		Print( "%d.%02d MB total image memory (estimated)",
			dataSize / ( 1024 * 1024 ), ( dataSize % ( 1024 * 1024 ) ) * 100 / ( 1024 * 1024 ) );
		Print( "%i total images", tr.images.size() );
		Print( lineSeparator );
	}
};
static ListImagesCmd listImagesCmdRegistration;

//=======================================================================

/*
================
ResampleTexture

Used to resample images in a more general than quartering fashion.

This will only be filtered properly if the resampled size
is greater than half the original size.

If a larger shrinking is needed, use the mipmap function
before or after.
================
*/
static void ResampleTexture( unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight,
                             bool normalMap )
{
	int      x, y;
	unsigned *inrow, *inrow2;
	unsigned frac, fracstep;
	unsigned p1[ 2048 ], p2[ 2048 ];
	byte     *pix1, *pix2, *pix3, *pix4;
	vec3_t   n, n2, n3, n4;

	fracstep = inwidth * 0x10000 / outwidth;

	frac = fracstep >> 2;

	for ( x = 0; x < outwidth; x++ )
	{
		p1[ x ] = 4 * ( frac >> 16 );
		frac += fracstep;
	}

	frac = 3 * ( fracstep >> 2 );

	for ( x = 0; x < outwidth; x++ )
	{
		p2[ x ] = 4 * ( frac >> 16 );
		frac += fracstep;
	}

	if ( normalMap )
	{
		for ( y = 0; y < outheight; y++ )
		{
			inrow = in + inwidth * ( int )( ( y + 0.25 ) * inheight / outheight );
			inrow2 = in + inwidth * ( int )( ( y + 0.75 ) * inheight / outheight );

			for ( x = 0; x < outwidth; x++ )
			{
				pix1 = ( byte * ) inrow + p1[ x ];
				pix2 = ( byte * ) inrow + p2[ x ];
				pix3 = ( byte * ) inrow2 + p1[ x ];
				pix4 = ( byte * ) inrow2 + p2[ x ];

				n[ 0 ] = Tex_ByteToFloat( pix1[ 0 ] );
				n[ 1 ] = Tex_ByteToFloat( pix1[ 1 ] );
				n[ 2 ] = Tex_ByteToFloat( pix1[ 2 ] );

				n2[ 0 ] = Tex_ByteToFloat( pix2[ 0 ] );
				n2[ 1 ] = Tex_ByteToFloat( pix2[ 1 ] );
				n2[ 2 ] = Tex_ByteToFloat( pix2[ 2 ] );

				n3[ 0 ] = Tex_ByteToFloat( pix3[ 0 ] );
				n3[ 1 ] = Tex_ByteToFloat( pix3[ 1 ] );
				n3[ 2 ] = Tex_ByteToFloat( pix3[ 2 ] );

				n4[ 0 ] = Tex_ByteToFloat( pix4[ 0 ] );
				n4[ 1 ] = Tex_ByteToFloat( pix4[ 1 ] );
				n4[ 2 ] = Tex_ByteToFloat( pix4[ 2 ] );

				VectorAdd( n, n2, n );
				VectorAdd( n, n3, n );
				VectorAdd( n, n4, n );

				if ( !VectorNormalize( n ) )
				{
					VectorSet( n, 0, 0, 1 );
				}

				( ( byte * )( out ) ) [ 0 ] = Tex_FloatToByte( n[ 0 ] );
				( ( byte * )( out ) ) [ 1 ] = Tex_FloatToByte( n[ 1 ] );
				( ( byte * )( out ) ) [ 2 ] = Tex_FloatToByte( n[ 2 ] );
				( ( byte * )( out ) ) [ 3 ] = 255;

				++out;
			}
		}
	}
	else
	{
		for ( y = 0; y < outheight; y++ )
		{
			inrow = in + inwidth * ( int )( ( y + 0.25 ) * inheight / outheight );
			inrow2 = in + inwidth * ( int )( ( y + 0.75 ) * inheight / outheight );

			for ( x = 0; x < outwidth; x++ )
			{
				pix1 = ( byte * ) inrow + p1[ x ];
				pix2 = ( byte * ) inrow + p2[ x ];
				pix3 = ( byte * ) inrow2 + p1[ x ];
				pix4 = ( byte * ) inrow2 + p2[ x ];

				( ( byte * )( out ) ) [ 0 ] = ( pix1[ 0 ] + pix2[ 0 ] + pix3[ 0 ] + pix4[ 0 ] ) >> 2;
				( ( byte * )( out ) ) [ 1 ] = ( pix1[ 1 ] + pix2[ 1 ] + pix3[ 1 ] + pix4[ 1 ] ) >> 2;
				( ( byte * )( out ) ) [ 2 ] = ( pix1[ 2 ] + pix2[ 2 ] + pix3[ 2 ] + pix4[ 2 ] ) >> 2;
				( ( byte * )( out ) ) [ 3 ] = ( pix1[ 3 ] + pix2[ 3 ] + pix3[ 3 ] + pix4[ 3 ] ) >> 2;

				++out;
			}
		}
	}
}

/*
==================
R_UnpackDXT5A
==================
*/
static void
R_UnpackDXT5A( const byte *in, byte *out )
{
	unsigned int bits0, bits1, val;
	int i;

	bits0 = in[2] + (in[3] << 8) + (in[4] << 16);
	bits1 = in[5] + (in[6] << 8) + (in[7] << 16);

	if( in[0] > in[1] ) {
		for( i = 0; i < 8; i++ ) {
			val = bits0 & 7; bits0 >>= 3;
			switch( val ) {
			case 0: val = in[0]; break;
			case 1: val = in[1]; break;
			case 2: val = (6 * in[0] + 1 * in[1]) / 7; break;
			case 3: val = (5 * in[0] + 2 * in[1]) / 7; break;
			case 4: val = (4 * in[0] + 3 * in[1]) / 7; break;
			case 5: val = (3 * in[0] + 4 * in[1]) / 7; break;
			case 6: val = (2 * in[0] + 5 * in[1]) / 7; break;
			case 7: val = (1 * in[0] + 6 * in[1]) / 7; break;
			}
			out[ i ] = val;

			val = bits1 & 7; bits1 >>= 3;
			switch( val ) {
			case 0: val = in[0]; break;
			case 1: val = in[1]; break;
			case 2: val = (6 * in[0] + 1 * in[1]) / 7; break;
			case 3: val = (5 * in[0] + 2 * in[1]) / 7; break;
			case 4: val = (4 * in[0] + 3 * in[1]) / 7; break;
			case 5: val = (3 * in[0] + 4 * in[1]) / 7; break;
			case 6: val = (2 * in[0] + 5 * in[1]) / 7; break;
			case 7: val = (1 * in[0] + 6 * in[1]) / 7; break;
			}
			out[ i + 8 ] = val;
		}
	} else {
		for( i = 0; i < 8; i++ ) {
			val = bits0 & 7; bits0 >>= 3;
			switch( val ) {
			case 0: val = in[0]; break;
			case 1: val = in[1]; break;
			case 2: val = (4 * in[0] + 1 * in[1]) / 5; break;
			case 3: val = (3 * in[0] + 2 * in[1]) / 5; break;
			case 4: val = (2 * in[0] + 3 * in[1]) / 5; break;
			case 5: val = (1 * in[0] + 4 * in[1]) / 5; break;
			case 6: val = 0;                           break;
			case 7: val = 0xff;                        break;
			}
			out[ i ] = val;

			val = bits1 & 7; bits1 >>= 3;
			switch( val ) {
			case 0: val = in[0]; break;
			case 1: val = in[1]; break;
			case 2: val = (4 * in[0] + 1 * in[1]) / 5; break;
			case 3: val = (3 * in[0] + 2 * in[1]) / 5; break;
			case 4: val = (2 * in[0] + 3 * in[1]) / 5; break;
			case 5: val = (1 * in[0] + 4 * in[1]) / 5; break;
			case 6: val = 0;                           break;
			case 7: val = 0xff;                        break;
			}
			out[ i + 8 ] = val;
		}
	}
}

static void
R_PackDXT1_Green( const byte *in, byte *out )
{
	int i;
	byte min, max;
	byte lim1, lim2, lim3;
	unsigned int bits0, bits1;

	min = max = in[ 0 ];
	for( i = 1; i < 16; i++ ) {
		if( in[ i ] < min )
			min = in[ i ];
		if( in[ i ] > max )
			max = in[ i ];
	}

	// truncate min and max to 6 bits
	i = (max - min) >> 3;
	min = (min + i) & 0xfc;
	max = (max - i) & 0xfc;
	if( min == max ) {
		if( max == 0xfc ) {
			min -= 4;
		} else {
			max += 4;
		}
	}
	min |= min >> 6;
	max |= max >> 6;

	// find best match for every pixel
	lim1 = (max + 5 * min) / 6;
	lim2 = (max + min) / 2;
	lim3 = (5 * max + min) / 6;
	bits0 = bits1 = 0;
	for( i = 7; i >= 0; i-- ) {
		bits0 <<= 2;
		bits1 <<= 2;
		if( in[ i ] > lim2 ) {
			if( in[ i ] > lim3 ) {
				bits0 |= 0;
			} else {
				bits0 |= 2;
			}
		} else {
			if( in[ i ] > lim1 ) {
				bits0 |= 3;
			} else {
				bits0 |= 1;
			}
		}
		if( in[ i + 8 ] > lim2 ) {
			if( in[ i + 8 ] > lim3 ) {
				bits1 |= 0;
			} else {
				bits1 |= 2;
			}
		} else {
			if( in[ i + 8 ] > lim1 ) {
				bits1 |= 3;
			} else {
				bits1 |= 1;
			}
		}
	}

	// write back DXT1 format data into out[]
	out[0] = (max & 0x1c) << 3;
	out[1] = 0xf8 | (max >> 5);
	out[2] = (min & 0x1c) << 3;
	out[3] = 0xf8 | (min >> 5);
	out[4] = bits0 & 0xff;
	out[5] = (bits0 >> 8) & 0xff;
	out[6] = bits1 & 0xff;
	out[7] = (bits1 >> 8) & 0xff;
}

/*
==================
R_ConvertBC5Image

If the OpenGL doesn't support BC5 (= OpenGL rgtc) textures, convert
them to BC3 (= dxt5). Sampling the original texture yields RGBA = XY01,
the converted texture yields RGBA = 1Y0X, so the shader can reconstruct
XY as vec2(tex.x * tex.a, tex.y).
==================
*/
static void
R_ConvertBC5Image(const byte **in, byte **out, int numMips, int numLayers,
		  int width, int height, bool is3D )
{
	int blocks = 0;
	int mipWidth, mipHeight, mipLayers, mipSize;
	byte data[16], *to;
	const byte *from;
	int i, j, k;

	// Allocate buffer for converted images
	mipWidth = width;
	mipHeight = height;
	mipLayers = numLayers;
	for ( i = 0; i < numMips; i++ ) {
		mipSize = ((mipWidth + 3) >> 2) * ((mipHeight + 3) >> 2);

		blocks += mipSize * mipLayers;

		if( mipWidth > 1 )
			mipWidth >>= 1;
		if( mipHeight > 1 )
			mipHeight >>= 1;
		if( is3D && mipLayers > 1 )
			mipLayers >>= 1;
	}

	*out = to = (byte *)ri.Hunk_AllocateTempMemory( blocks * 16 );

	// Convert each mipmap
	mipWidth = width;
	mipHeight = height;
	mipLayers = numLayers;
	for ( i = 0; i < numMips; i++ ) {
		mipSize = ((mipWidth + 3) >> 2) * ((mipHeight + 3) >> 2);

		for( j = 0; j < mipLayers; j++ ) {
			from = in[ i * numLayers + j ];
			in [ i * numLayers + j ] = to;

			for( k = 0; k < mipSize; k++ ) {
				// red channel is unchanged
				memcpy( to, from, 8 );

				// green channel is converted to DXT1
				R_UnpackDXT5A( from + 8, data );
				R_PackDXT1_Green( data, to + 8 );

				from += 16; to += 16;
			}
		}

		if( mipWidth > 1 )
			mipWidth >>= 1;
		if( mipHeight > 1 )
			mipHeight >>= 1;
		if( is3D && mipLayers > 1 )
			mipLayers >>= 1;
	}
}

/*
===============
R_UploadImage

dataArray points to an array of numLayers * numMips pointers, first all layers
of mip level 0, then all layers of mip level 1, etc.

Note that for 3d textures the higher mip levels have fewer layers, e.g. mip
level 1 has only numLayers/2 layers. There are still numLayers pointers in
the dataArray for every mip level, the unneeded elements at the end aren't used.
===============
*/
void R_UploadImage( const char *name, const byte **dataArray, int numLayers, int numMips, image_t *image, const imageParams_t &imageParams )
{
	const byte *data;
	byte       *scaledBuffer = nullptr;
	int        mipWidth, mipHeight, mipLayers, mipSize, blockSize = 0;
	int        i, c;
	const byte *scan;
	GLenum     target;
	GLenum     format = GL_RGBA;
	GLenum     internalFormat = GL_RGB;

	static const vec4_t oneClampBorder = { 1, 1, 1, 1 };
	static const vec4_t zeroClampBorder = { 0, 0, 0, 1 };
	static const vec4_t alphaZeroClampBorder = { 0, 0, 0, 0 };

	if ( numMips <= 0 )
		numMips = 1;

	GL_Bind( image );

	int scaledWidth = image->width;
	int scaledHeight = image->height;

	// If r_imageFitScreen is disabled, use nopicmip instead.
	if ( ( image->bits & IF_FITSCREEN ) && !r_imageFitScreen.Get() )
	{
		image->bits &= ~IF_FITSCREEN;
		image->bits |= IF_NOPICMIP;
	}

	int customScalingStep = R_GetImageCustomScalingStep( image, imageParams );
	R_DownscaleImageDimensions( customScalingStep, &scaledWidth, &scaledHeight, &dataArray, numLayers, &numMips );

	// clamp to the current upper OpenGL limit
	// scale both axis down equally so we don't have to
	// deal with a half mip resampling
	if ( image->type == GL_TEXTURE_CUBE_MAP )
	{
		while ( scaledWidth > glConfig2.maxCubeMapTextureSize || scaledHeight > glConfig2.maxCubeMapTextureSize )
		{
			scaledWidth >>= 1;
			scaledHeight >>= 1;
		}
	}
	else
	{
		while ( scaledWidth > glConfig.maxTextureSize || scaledHeight > glConfig.maxTextureSize )
		{
			scaledWidth >>= 1;
			scaledHeight >>= 1;
		}
	}

	// set target
	switch ( image->type )
	{
		case GL_TEXTURE_3D:
			target = GL_TEXTURE_3D;
			break;

		case GL_TEXTURE_CUBE_MAP:
			target = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
			break;

		default:
			target = GL_TEXTURE_2D;
			break;
	}

	if ( image->bits & ( IF_DEPTH16 | IF_DEPTH24 | IF_DEPTH32 ) )
	{
		format = GL_DEPTH_COMPONENT;

		if ( image->bits & IF_DEPTH16 )
		{
			internalFormat = GL_DEPTH_COMPONENT16;
		}
		else if ( image->bits & IF_DEPTH24 )
		{
			internalFormat = GL_DEPTH_COMPONENT24;
		}
		else if ( image->bits & IF_DEPTH32 )
		{
			internalFormat = GL_DEPTH_COMPONENT32;
		}
	}
	else if ( image->bits & ( IF_PACKED_DEPTH24_STENCIL8 ) )
	{
		format = GL_DEPTH_STENCIL;
		internalFormat = GL_DEPTH24_STENCIL8;
	}
	else if ( image->bits & IF_RGBA16 )
	{
		if ( !glConfig2.textureRGBA16BlendAvailable )
		{
			Log::Warn("RGBA16 image '%s' cannot be blended", image->name );
			internalFormat = GL_RGBA8;
		}
		else
		{
			internalFormat = GL_RGBA16;
		}
	}
	else if ( image->bits & ( IF_RGBA16F | IF_RGBA32F | IF_TWOCOMP16F | IF_TWOCOMP32F | IF_ONECOMP16F | IF_ONECOMP32F ) )
	{
		if( !glConfig2.textureFloatAvailable ) {
			Log::Warn("floating point image '%s' cannot be loaded", image->name );
			internalFormat = GL_RGBA8;
		}
		else if ( image->bits & IF_RGBA16F )
		{
			internalFormat = GL_RGBA16F;
		}
		else if ( image->bits & IF_RGBA32F )
		{
			internalFormat = GL_RGBA32F;
		}
		else if ( image->bits & IF_TWOCOMP16F )
		{
			internalFormat = glConfig2.textureRGAvailable ?
			  GL_RG16F : GL_LUMINANCE_ALPHA16F_ARB;
		}
		else if ( image->bits & IF_TWOCOMP32F )
		{
			internalFormat = glConfig2.textureRGAvailable ?
			  GL_RG32F : GL_LUMINANCE_ALPHA32F_ARB;
		}
		else if ( image->bits & IF_ONECOMP16F )
		{
			internalFormat = glConfig2.textureRGAvailable ?
			  GL_R16F : GL_ALPHA16F_ARB;
		}
		else if ( image->bits & IF_ONECOMP32F )
		{
			internalFormat = glConfig2.textureRGAvailable ?
			  GL_R32F : GL_ALPHA32F_ARB;
		}
	}
	else if ( image->bits & ( IF_RGBA32UI ) )
	{
		if( !glConfig2.textureIntegerAvailable ) {
			Log::Warn( "integer image '%s' cannot be loaded", image->name );
		}
		internalFormat = GL_RGBA32UI;
		format = GL_RGBA_INTEGER;
	}
	else if ( IsImageCompressed( image->bits ) )
	{
		if( !GLEW_EXT_texture_compression_dxt1 &&
		    !GLEW_EXT_texture_compression_s3tc ) {
			Log::Warn("compressed image '%s' cannot be loaded", image->name );
			internalFormat = GL_RGBA8;
		}
		else if( image->bits & IF_BC1 ) {
			format = GL_NONE;
			internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
			blockSize = 8;
		}
		else if ( image->bits & IF_BC2 ) {
			format = GL_NONE;
			internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
			blockSize = 16;
		}
		else if ( image->bits & IF_BC3 ) {
			format = GL_NONE;
			internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			blockSize = 16;
		}
		else if ( image->bits & IF_BC4 ) {
			if( !glConfig2.textureCompressionRGTCAvailable ) {
				format = GL_NONE;
				internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
				blockSize = 8;

				if( dataArray ) {
					// convert to BC1/dxt1
				}
			}
			else {
				format = GL_NONE;
				internalFormat = GL_COMPRESSED_RED_RGTC1;
				blockSize = 8;
			}
		}
		else if ( image->bits & IF_BC5 ) {
			if( !glConfig2.textureCompressionRGTCAvailable ) {
				format = GL_NONE;
				internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
				blockSize = 16;

				R_ConvertBC5Image( dataArray, &scaledBuffer,
						   numMips, numLayers,
						   scaledWidth, scaledHeight,
						   image->type == GL_TEXTURE_3D );
			}
			else {
				format = GL_NONE;
				internalFormat = GL_COMPRESSED_RG_RGTC2;
				blockSize = 16;
			}
		}
	}
	else if ( image->bits & IF_RGBE )
	{
		internalFormat = GL_RGBA8;
	}
	else if ( !dataArray ) {
		internalFormat = GL_RGBA8;
	}
	else
	{
		// scan the texture for each channel's max values
		// and verify if the alpha channel is being used or not

		c = image->width * image->height;
		scan = dataArray[0];

		// lightmap does not have alpha channel

		// normalmap may have the heightmap in the alpha channel
		// opaque alpha channel means no displacement, so we can enable
		// alpha channel everytime it is used, even for normalmap

		internalFormat = GL_RGB8;

		if ( !( image->bits & IF_LIGHTMAP ) )
		{
			for ( i = 0; i < c; i++ )
			{
				if ( scan[ i * 4 + 3 ] != 255 )
				{
					internalFormat = GL_RGBA8;
					break;
				}
			}
		}
	}

	Log::Debug( "Uploading image %s (%d×%d, %d layers, %0#x type, %0#x format)", name, scaledWidth, scaledHeight, numLayers, image->type, internalFormat );

	// 3D textures are uploaded in slices via glTexSubImage3D,
	// so the storage has to be allocated before the loop
	if( image->type == GL_TEXTURE_3D ) {
		mipWidth = scaledWidth;
		mipHeight = scaledHeight;
		mipLayers = numLayers;

		for( i = 0; i < numMips; i++ ) {
			glTexImage3D( GL_TEXTURE_3D, i, internalFormat,
				      scaledWidth, scaledHeight, mipLayers,
				      0, format, GL_UNSIGNED_BYTE, nullptr );

			if( mipWidth  > 1 ) mipWidth  >>= 1;
			if( mipHeight > 1 ) mipHeight >>= 1;
			if( mipLayers > 1 ) mipLayers >>= 1;
		}
	}

	if( format != GL_NONE ) {
		if( dataArray )
			scaledBuffer = (byte*) ri.Hunk_AllocateTempMemory( sizeof( byte ) * scaledWidth * scaledHeight * 4 );

		for ( i = 0; i < numLayers; i++ )
		{
			if( dataArray )
				data = dataArray[ i ];
			else
				data = nullptr;

			if( scaledBuffer )
			{
				// copy or resample data as appropriate for first MIP level
				if ( ( scaledWidth == image->width ) && ( scaledHeight == image->height ) )
				{
					memcpy( scaledBuffer, data, scaledWidth * scaledHeight * 4 );
				}
				else
				{
					ResampleTexture( ( unsigned * ) data, image->width, image->height, ( unsigned * ) scaledBuffer, scaledWidth, scaledHeight,
							 ( image->bits & IF_NORMALMAP ) );
				}

				if( image->bits & IF_NORMALMAP ) {
					c = scaledWidth * scaledHeight;
					for ( int j = 0; j < c; j++ )
					{
						vec3_t n;

						n[ 0 ] = Tex_ByteToFloat( scaledBuffer[ j * 4 + 0 ] );
						n[ 1 ] = Tex_ByteToFloat( scaledBuffer[ j * 4 + 1 ] );
						n[ 2 ] = Tex_ByteToFloat( scaledBuffer[ j * 4 + 2 ] );

						VectorNormalize( n );

						scaledBuffer[ j * 4 + 0 ] = Tex_FloatToByte( n[ 0 ] );
						scaledBuffer[ j * 4 + 1 ] = Tex_FloatToByte( n[ 1 ] );
						scaledBuffer[ j * 4 + 2 ] = Tex_FloatToByte( n[ 2 ] );
					}
				}
			}

			image->uploadWidth = scaledWidth;
			image->uploadHeight = scaledHeight;
			image->internalFormat = internalFormat;

			switch ( image->type )
			{
			case GL_TEXTURE_3D:
				if( scaledBuffer ) {
					glTexSubImage3D( GL_TEXTURE_3D, 0, 0, 0, i,
							 scaledWidth, scaledHeight, 1,
							 format, GL_UNSIGNED_BYTE,
							 scaledBuffer );
				}
				break;
			case GL_TEXTURE_CUBE_MAP:
				glTexImage2D( target + i, 0, internalFormat, scaledWidth, scaledHeight, 0, format, GL_UNSIGNED_BYTE,
				              scaledBuffer );
				break;

			default:
				if ( image->bits & IF_PACKED_DEPTH24_STENCIL8 )
				{
					glTexImage2D( target, 0, internalFormat, scaledWidth, scaledHeight, 0, format, GL_UNSIGNED_INT_24_8, nullptr );
				}
				else
				{
					glTexImage2D( target, 0, internalFormat, scaledWidth, scaledHeight, 0, format, GL_UNSIGNED_BYTE, scaledBuffer );
				}

				break;
			}

			if ( image->filterType == filterType_t::FT_DEFAULT )
			{
				if( image->type != GL_TEXTURE_CUBE_MAP || i == 5 ) {
					GL_fboShim.glGenerateMipmap( image->type );
					glTexParameteri( image->type, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );  // default to trilinear
				}
			}
		}
	} else {
		// already compressed texture data, precomputed mipmaps must be
		// in the data array
		image->uploadWidth = scaledWidth;
		image->uploadHeight = scaledHeight;
		image->internalFormat = internalFormat;

		mipWidth = scaledWidth;
		mipHeight = scaledHeight;
		mipLayers = numLayers;

		for ( i = 0; i < numMips; i++ )
		{
			mipSize = ((mipWidth + 3) >> 2)
			  * ((mipHeight + 3) >> 2) * blockSize;

			for ( int j = 0; j < mipLayers; j++ ) {
				if( dataArray )
					data = dataArray[ j * numMips + i ];
				else
					data = nullptr;

				switch ( image->type )
				{
				case GL_TEXTURE_3D:
					glCompressedTexSubImage3D( GL_TEXTURE_3D, i, 0, 0, j,
								   scaledWidth, scaledHeight, 1,
								   internalFormat, mipSize, data );
					break;
				case GL_TEXTURE_CUBE_MAP:
					glCompressedTexImage2D( target + j, i, internalFormat, mipWidth, mipHeight, 0, mipSize, data );
					break;

				default:
					glCompressedTexImage2D( target, i, internalFormat, mipWidth, mipHeight, 0, mipSize, data );
					break;
				}

				if( mipWidth > 1 )
					mipWidth >>= 1;
				if( mipHeight > 1 )
					mipHeight >>= 1;
				if( image->type == GL_TEXTURE_3D && mipLayers > 1 )
					mipLayers >>= 1;
			}
		}
	}

	GL_CheckErrors();

	// set filter type
	switch ( image->filterType )
	{
		case filterType_t::FT_DEFAULT:

			// set texture anisotropy
			if ( glConfig2.textureAnisotropyAvailable )
			{
				glTexParameterf( image->type, GL_TEXTURE_MAX_ANISOTROPY_EXT, glConfig2.textureAnisotropy );
			}

			glTexParameterf( image->type, GL_TEXTURE_MIN_FILTER, gl_filter_min );
			glTexParameterf( image->type, GL_TEXTURE_MAG_FILTER, gl_filter_max );
			break;

		case filterType_t::FT_LINEAR:
			glTexParameterf( image->type, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameterf( image->type, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			break;

		case filterType_t::FT_NEAREST:
			glTexParameterf( image->type, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			glTexParameterf( image->type, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			break;

		default:
			Log::Warn("unknown filter type for image '%s'", image->name );
			glTexParameterf( image->type, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameterf( image->type, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			break;
	}

	GL_CheckErrors();

	// set wrap type
	if ( image->wrapType.s == image->wrapType.t )
        {
                switch ( image->wrapType.s )
                {
                        case wrapTypeEnum_t::WT_REPEAT:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_S, GL_REPEAT );
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_T, GL_REPEAT );
                                break;

                        case wrapTypeEnum_t::WT_CLAMP:
                        case wrapTypeEnum_t::WT_EDGE_CLAMP:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
                                break;
                        case wrapTypeEnum_t::WT_ONE_CLAMP:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
                                glTexParameterfv( image->type, GL_TEXTURE_BORDER_COLOR, oneClampBorder );
                                break;
                        case wrapTypeEnum_t::WT_ZERO_CLAMP:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
                                glTexParameterfv( image->type, GL_TEXTURE_BORDER_COLOR, zeroClampBorder );
                                break;

                        case wrapTypeEnum_t::WT_ALPHA_ZERO_CLAMP:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
                                glTexParameterfv( image->type, GL_TEXTURE_BORDER_COLOR, alphaZeroClampBorder );
                                break;

                        default:
								Log::Warn("unknown wrap type for image '%s'", image->name );
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_S, GL_REPEAT );
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_T, GL_REPEAT );
                                break;
                }
        } else {
        	// warn about mismatched clamp types if both require a border colour to be set
                if ( ( image->wrapType.s == wrapTypeEnum_t::WT_ZERO_CLAMP || image->wrapType.s == wrapTypeEnum_t::WT_ONE_CLAMP || image->wrapType.s == wrapTypeEnum_t::WT_ALPHA_ZERO_CLAMP ) &&
	             ( image->wrapType.t == wrapTypeEnum_t::WT_ZERO_CLAMP || image->wrapType.t == wrapTypeEnum_t::WT_ONE_CLAMP || image->wrapType.t == wrapTypeEnum_t::WT_ALPHA_ZERO_CLAMP ) )
        	{
					Log::Warn("mismatched wrap types for image '%s'", image->name );
                }

                switch ( image->wrapType.s )
                {
                        case wrapTypeEnum_t::WT_REPEAT:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_S, GL_REPEAT );
                                break;

                        case wrapTypeEnum_t::WT_CLAMP:
                        case wrapTypeEnum_t::WT_EDGE_CLAMP:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
                                break;

                        case wrapTypeEnum_t::WT_ONE_CLAMP:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
                                glTexParameterfv( image->type, GL_TEXTURE_BORDER_COLOR, oneClampBorder );
                                break;

                        case wrapTypeEnum_t::WT_ZERO_CLAMP:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
                                glTexParameterfv( image->type, GL_TEXTURE_BORDER_COLOR, zeroClampBorder );
                                break;

                        case wrapTypeEnum_t::WT_ALPHA_ZERO_CLAMP:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
                                glTexParameterfv( image->type, GL_TEXTURE_BORDER_COLOR, alphaZeroClampBorder );
                                break;

                        default:
								Log::Warn("unknown wrap type for image '%s' axis S", image->name );
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_S, GL_REPEAT );
                                break;
                }

                switch ( image->wrapType.t )
                {
                        case wrapTypeEnum_t::WT_REPEAT:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_T, GL_REPEAT );
                                break;

                        case wrapTypeEnum_t::WT_CLAMP:
                        case wrapTypeEnum_t::WT_EDGE_CLAMP:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
                                break;

                        case wrapTypeEnum_t::WT_ONE_CLAMP:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
                                glTexParameterfv( image->type, GL_TEXTURE_BORDER_COLOR, oneClampBorder );
                                break;

                        case wrapTypeEnum_t::WT_ZERO_CLAMP:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
                                glTexParameterfv( image->type, GL_TEXTURE_BORDER_COLOR, zeroClampBorder );
                                break;

                        case wrapTypeEnum_t::WT_ALPHA_ZERO_CLAMP:
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
                                glTexParameterfv( image->type, GL_TEXTURE_BORDER_COLOR, alphaZeroClampBorder );
                                break;

                        default:
								Log::Warn("unknown wrap type for image '%s' axis T", image->name );
                                glTexParameterf( image->type, GL_TEXTURE_WRAP_T, GL_REPEAT );
                                break;
                }
        }

	GL_CheckErrors();

	if ( scaledBuffer != nullptr )
	{
		ri.Hunk_FreeTempMemory( scaledBuffer );
	}

	switch ( image->internalFormat )
	{
		case GL_RGBA:
		case GL_RGBA8:
		case GL_RGBA16:
		case GL_RGBA16F:
		case GL_RGBA32F:
		case GL_RGBA32UI:
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
			image->bits |= IF_ALPHA;
	}

	GL_Unbind( image );
}

/*
================
R_AllocImage
================
*/
image_t        *R_AllocImage( const char *name, bool linkIntoHashTable )
{
	Log::Debug( "Allocating image %s", name );

	image_t *image;
	unsigned hash;

	if ( strlen( name ) >= sizeof( image->name ) )
	{
		Sys::Drop( "R_AllocImage: \"%s\" image name is too long", name );
	}

	image = (image_t*) ri.Hunk_Alloc( sizeof( image_t ), ha_pref::h_low );
	*image = {};
	image->texture = new Texture();

	glGenTextures( 1, &image->texnum );
	image->texture->textureHandle = image->texnum;

	tr.images.push_back( image );

	Q_strncpyz( image->name, name, sizeof( image->name ) );

	if ( linkIntoHashTable )
	{
		hash = GenerateImageHashValue( name );
		image->next = r_imageHashTable[ hash ];
		r_imageHashTable[ hash ] = image;
	}

	// Default image number of layers.
	image->numLayers = 1;

	return image;
}

/*
================
================
*/
static void R_ExportTexture( image_t *image )
{
	char path[ 1024 ];
	Com_sprintf( path, sizeof( path ), "texexp/%s.ktx", image->name );

	// quick and dirty sanitize path name
	for( size_t i = strlen( path ) - 1; i >= 7; i-- ) {
		if( !Str::cisalnum( path[ i ] ) && path[ i ] != '.' && path[ i ] != '-' ) {
			path[ i ] = 'z';
		}
	}
	SaveImageKTX( path, image );
}

/*
================
R_CreateImage
================
*/
image_t *R_CreateImage( const char *name, const byte **pic, int width, int height, int numMips, const imageParams_t &imageParams )
{
	Log::Debug( "Creating image %s (%d×%d, %d mips)", name, width, height, numMips );

	image_t *image;

	image = R_AllocImage( name, true );

	if ( !image )
	{
		return nullptr;
	}

	image->type = GL_TEXTURE_2D;
	image->texture->target = GL_TEXTURE_2D;

	image->width = width;
	image->height = height;

	image->bits = imageParams.bits;
	image->filterType = imageParams.filterType;
	image->wrapType = imageParams.wrapType;

	R_UploadImage( name, pic, 1, numMips, image, imageParams );

	if( r_exportTextures->integer ) {
		R_ExportTexture( image );
	}

	return image;
}

/*
================
R_CreateGlyph
================
*/
image_t *R_CreateGlyph( const char *name, const byte *pic, int width, int height )
{
	image_t *image = R_AllocImage( name, true );

	if ( !image )
	{
		return nullptr;
	}

	image->type = GL_TEXTURE_2D;
	image->texture->target = GL_TEXTURE_2D;
	image->width = width;
	image->height = height;
	image->bits = IF_NOPICMIP;
	image->filterType = filterType_t::FT_LINEAR;
	image->wrapType = wrapTypeEnum_t::WT_CLAMP;

	GL_Bind( image );

	image->uploadWidth = width;
	image->uploadHeight = height;
	image->internalFormat = GL_RGBA;

	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pic );

	GL_CheckErrors();

	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	GL_CheckErrors();

	GL_Unbind( image );

	return image;
}

/*
================
R_CreateCubeImage
================
*/
image_t *R_CreateCubeImage( const char *name, const byte *pic[ 6 ], int width, int height, const imageParams_t &imageParams )
{
	image_t *image;

	image = R_AllocImage( name, true );

	if ( !image )
	{
		return nullptr;
	}

	image->type = GL_TEXTURE_CUBE_MAP;
	image->texture->target = GL_TEXTURE_CUBE_MAP;

	image->width = width;
	image->height = height;
	image->numLayers = 6;

	image->bits = imageParams.bits;
	image->filterType = imageParams.filterType;
	image->wrapType = imageParams.wrapType;

	R_UploadImage( name, pic, 6, 1, image, imageParams );

	if( r_exportTextures->integer ) {
		R_ExportTexture( image );
	}

	return image;
}

/*
================
R_Create3DImage
================
*/
image_t *R_Create3DImage( const char *name, const byte *pic, int width, int height, int numLayers, const imageParams_t &imageParams )
{
	image_t *image;
	const byte **pics;
	int i;

	image = R_AllocImage( name, true );

	if ( !image )
	{
		return nullptr;
	}

	image->type = GL_TEXTURE_3D;
	image->texture->target = GL_TEXTURE_3D;

	image->width = width;
	image->height = height;
	image->numLayers = numLayers;

	if( pic ) {
		pics = (const byte**) ri.Hunk_AllocateTempMemory( numLayers * sizeof(const byte *) );
		for( i = 0; i < numLayers; i++ ) {
			pics[i] = pic + i * width * height * sizeof(u8vec4_t);
		}
	} else {
		pics = nullptr;
	}

	image->bits = imageParams.bits;
	image->filterType = imageParams.filterType;
	image->wrapType = imageParams.wrapType;

	R_UploadImage( name, pics, numLayers, 1, image, imageParams );

	if( pics ) {
		ri.Hunk_FreeTempMemory( pics );
	}

	if( r_exportTextures->integer ) {
		R_ExportTexture( image );
	}

	return image;
}

using imageLoader_t = void( * )( const char *, byte **, int *, int *, int *, int *, int *, byte );

struct imageExtLoader_t
{
	const char *ext;
	const char *name;
	imageLoader_t imageLoader;
	bool cubemap;
};

/* The ordering indicates the order of preference used when
there are multiple images of different formats available. */
static const imageExtLoader_t imageLoaders[] =
{
	{ "webp", "WebP", LoadWEBP, false },
	{ "png",  "PNG",  LoadPNG,  false },
	{ "tga",  "TGA",  LoadTGA,  false },
	{ "jpg",  "JPEG", LoadJPG,  false },
	{ "jpeg", "JPEG", LoadJPG,  false },
	{ "dds",  "DDS",  LoadDDS,  false },
	{ "crn",  "CRN",  LoadCRN,  true  },
	{ "ktx",  "KTX",  LoadKTX,  true  },
};

/*
=================
R_FindImageLoader

Finds and returns an image loader for a given basename,
tells the extra prefix that may be required to load the image.
=================
*/
static const imageExtLoader_t* R_FindImageLoader( const char *baseName, const char **prefix ) {
	const FS::PakInfo* bestPak = nullptr;
	const imageExtLoader_t* bestLoader = nullptr;

	*prefix = "";

	// try and find a suitable match using all the image formats supported
	// prioritize with the pak priority
	for ( const imageExtLoader_t &loader : imageLoaders )
	{
		std::string altName = Str::Format( "%s.%s", baseName, loader.ext );
		const FS::PakInfo* pak = FS::PakPath::LocateFile( altName );

		/* We found a file and its pak is better than the best pak we have
		this relies on how the filesystem works internally.
		FIXME: Move to a more explicit interface once there is one. */
		if ( pak != nullptr && ( bestPak == nullptr || pak < bestPak ) )
		{
			/* We found a file and its pak is better than the best pak we have
			this relies on how the filesystem works internally. */
			bestPak = pak;
			bestLoader = &loader;
		}

		// DarkPlaces or Doom3 packages can ship alternative texture path in the form of
		//   dds/<path without ext>.dds
		if ( bestPak == nullptr && !Q_stricmp( "dds", loader.ext ) )
		{
			std::string prefixedName = Str::Format( "dds/%s.dds", baseName );
			bestPak = FS::PakPath::LocateFile( prefixedName );
			if ( bestPak != nullptr ) {
				*prefix = "dds/";
				bestPak = pak;
				bestLoader = &loader;
			}
		}
	}

	return bestLoader;
}

bool R_HasImageLoader( const char *baseName ) {
	// not used but required by R_FindImageLoader
	const char *prefix;

	return R_FindImageLoader( baseName, &prefix ) != nullptr;
}

static void R_LoadImageWithLoader( const char* fileName, const char* altName, const imageExtLoader_t *loader, byte **pic, int *width, int *height, int *numLayers, int *numMips, int *bits, byte alphaByte )
{
	Log::Debug( "Found %s image candidate '%s': %s", loader->name, fileName, altName );
	loader->imageLoader( altName, pic, width, height, numLayers, numMips, bits, alphaByte );

	if ( *pic )
	{
		Log::Debug("Found %d×%d %s image '%s': %s", *width, *height, loader->name, fileName, altName );
	}
}

/*
=================
R_LoadImage

Loads any of the supported image types into a canonical
32 bit format.
=================
*/
static void R_LoadImage( const char *name, byte **pic, int *width, int *height,
			 int *numLayers, int *numMips,
			 int *bits )
{
	*pic = nullptr;
	*width = *height = 0;

	if ( !name )
	{
		Log::Warn("NULL parameter for R_LoadImage" );
		return;
	}

	// missing alpha means fully opaque
	byte alphaByte = 0xFF;

	const char *ext = COM_GetExtension( name );

	/* The Daemon's default strategy is to use the hardcoded path if it exists.
	If we are given the name “some.thing.tga” we will look for “some.thing.tga” first
	and if we are given the name “something.tga” we will look for “something.tga” first. */
	if ( *ext )
	{
		// Look for the correct loader and load the image is the file is found.
		for ( const auto &loader : imageLoaders )
		{
			if ( !Q_stricmp( ext, loader.ext ) )
			{
				/* Do not complain on missing file if the extension is hardcoded to a wrong one since
				the file can exist with another extension and it will tested right after that. */
				if ( FS::PakPath::FileExists( name ) )
				{
					R_LoadImageWithLoader( name, name, &loader, pic, width, height, numLayers, numMips, bits, alphaByte );

					if ( *pic )
					{
						return;
					}
				}

				// We have to break because the expected loader was found.
				break;
			}
		}
	}

	const char *prefix;

	/* If we are given the name “some.thing”, we first look for “some.thing.webp”,
	“some.thing.png”, etc. It also means that if we are given the name “something.tga” we
	will also look for “something.tga.webp, “something.tga.png”, “something.tga.tga”, etc. */
	const imageExtLoader_t* loader = R_FindImageLoader( name, &prefix );

	if ( loader )
	{
		std::string altName = Str::Format( "%s%s.%s", prefix, name, loader->ext );
		R_LoadImageWithLoader( name, altName.c_str(), loader, pic, width, height, numLayers, numMips, bits, alphaByte );
		return;
	}

	/* Then for the name “something.tga“ we look for “something.webp”, “something.png”, etc.
	It also means that for the name “some.thing” we look for “some.webp”, “some.png”, etc. */
	if ( *ext )
	{
		char baseName[ 1024 ];
		COM_StripExtension3( name, baseName, sizeof(baseName) );

		loader = R_FindImageLoader( baseName, &prefix );

		if ( loader )
		{
			std::string altName = Str::Format( "%s%s.%s", prefix, baseName, loader->ext );
			R_LoadImageWithLoader( name, altName.c_str(), loader, pic, width, height, numLayers, numMips, bits, alphaByte );
		}
	}
}

/*
===============
R_FindImageFile

Finds or loads the given image.
Returns nullptr if it fails, not a default image.
==============
*/
image_t *R_FindImageFile( const char *imageName, imageParams_t &imageParams )
{
	if ( !imageName )
	{
		return nullptr;
	}

	unsigned hash = GenerateImageHashValue( imageName );

	// See if the image is already loaded.
	for ( image_t *image = r_imageHashTable[ hash ]; image; image = image->next )
	{
		if ( !Q_strnicmp( imageName, image->name, sizeof( image->name ) ) )
		{
			// The white image can be used with any set of parms, but other mismatches are errors.
			if ( Q_stricmp( imageName, "_white" ) )
			{
				unsigned int diff = imageParams.bits ^ image->bits;

				if ( diff & IF_NOPICMIP )
				{
					Log::Warn("reused image '%s' with mixed allowPicmip parm for shader", imageName );
				}

				if ( image->wrapType != imageParams.wrapType )
				{
					Log::Warn("reused image '%s' with mixed glWrapType parm for shader", imageName);
				}
			}

			return image;
		}
	}

	// Load and create the image.
	int width = 0, height = 0, numLayers = 0, numMips = 0;
	byte *pic[ MAX_TEXTURE_MIPS * MAX_TEXTURE_LAYERS ];
	pic[ 0 ] = nullptr;

	R_LoadImage( imageName, pic, &width, &height, &numLayers, &numMips, &imageParams.bits );

	if ( *pic )
	{
		if ( numLayers > 0 )
		{
			Z_Free( *pic );
			return nullptr;
		}
	}
	else
	{
		return nullptr;
	}

	if ( imageParams.bits & IF_LIGHTMAP && tr.legacyOverBrightClamping )
	{
		R_ProcessLightmap( *pic, width, height, imageParams.bits );
	}

	image_t *image = R_CreateImage( imageName, (const byte **)pic, width, height, numMips, imageParams );

	Z_Free( *pic );

	return image;
}

static void R_Flip( byte *in, int width, int height )
{
	int32_t *data = (int32_t *) in;
	int x, y;

	for ( y = 0; y < height; y++ )
	{
		for ( x = 0; x < width / 2; x++ )
		{
			int32_t texel = data[ x ];
			data[ x ] = data[ width - 1 - x ];
			data[ width - 1 - x ] = texel;
		}
		data += width;
	}
}

static void R_Flop( byte *in, int width, int height )
{
	int32_t *upper = (int32_t *) in;
	int32_t *lower = (int32_t *) in + ( height - 1 ) * width;
	int     x, y;

	for ( y = 0; y < height / 2; y++ )
	{
		for ( x = 0; x < width; x++ )
		{
			int32_t texel = upper[ x ];
			upper[ x ] = lower[ x ];
			lower[ x ] = texel;
		}

		upper += width;
		lower -= width;
	}
}

static void R_Rotate( byte *in, int dim, int degrees )
{
	byte color[ 4 ];
	int  x, y, x2, y2;
	byte *out, *tmp;

	tmp = (byte*) ri.Hunk_AllocateTempMemory( dim * dim * 4 );

	// rotate into tmp buffer
	for ( y = 0; y < dim; y++ )
	{
		for ( x = 0; x < dim; x++ )
		{
			color[ 0 ] = in[ 4 * ( y * dim + x ) + 0 ];
			color[ 1 ] = in[ 4 * ( y * dim + x ) + 1 ];
			color[ 2 ] = in[ 4 * ( y * dim + x ) + 2 ];
			color[ 3 ] = in[ 4 * ( y * dim + x ) + 3 ];

			if ( degrees == 90 )
			{
				x2 = y;
				y2 = ( dim - ( 1 + x ) );

				tmp[ 4 * ( y2 * dim + x2 ) + 0 ] = color[ 0 ];
				tmp[ 4 * ( y2 * dim + x2 ) + 1 ] = color[ 1 ];
				tmp[ 4 * ( y2 * dim + x2 ) + 2 ] = color[ 2 ];
				tmp[ 4 * ( y2 * dim + x2 ) + 3 ] = color[ 3 ];
			}
			else if ( degrees == -90 )
			{
				x2 = ( dim - ( 1 + y ) );
				y2 = x;

				tmp[ 4 * ( y2 * dim + x2 ) + 0 ] = color[ 0 ];
				tmp[ 4 * ( y2 * dim + x2 ) + 1 ] = color[ 1 ];
				tmp[ 4 * ( y2 * dim + x2 ) + 2 ] = color[ 2 ];
				tmp[ 4 * ( y2 * dim + x2 ) + 3 ] = color[ 3 ];
			}
			else
			{
				ASSERT_UNREACHABLE();
			}
		}
	}

	// copy back to input
	out = in;

	for ( y = 0; y < dim; y++ )
	{
		for ( x = 0; x < dim; x++ )
		{
			out[ 4 * ( y * dim + x ) + 0 ] = tmp[ 4 * ( y * dim + x ) + 0 ];
			out[ 4 * ( y * dim + x ) + 1 ] = tmp[ 4 * ( y * dim + x ) + 1 ];
			out[ 4 * ( y * dim + x ) + 2 ] = tmp[ 4 * ( y * dim + x ) + 2 ];
			out[ 4 * ( y * dim + x ) + 3 ] = tmp[ 4 * ( y * dim + x ) + 3 ];
		}
	}

	ri.Hunk_FreeTempMemory( tmp );
}

/*
========
R_Resize

wrapper for ResampleTexture to help to resize a texture in place like this:
pic = R_Resize( pic, width, height, newWidth, newHeight );

please not resize normalmap with this, use ResampleTexture directly instead

========
*/

byte *R_Resize( byte *in, int width, int height, int newWidth, int newHeight )
{

	byte *out;

	out = (byte*) Z_Malloc( newWidth * newHeight * 4 );
	ResampleTexture( (unsigned int*) in, width, height, (unsigned int*) out, newWidth, newHeight, false );
	Z_Free( in );

	return out;
}

static void R_FreeCubePics( byte **pic, int count )
{
	while (--count >= 0)
	{
		if ( pic[ count ] != nullptr )
		{
			Z_Free( pic[ count ] );
			pic[ count ] = nullptr;
		}
	}
}

struct multifileCubeMapFormat_t
{
	const char *name;
	const char *sep;
	const char *suffixes[6];
	bool flipX[6];
	bool flipY[6];
	int rot[6];
};

static const multifileCubeMapFormat_t multifileCubeMapFormats[] =
{
	{
		"OpenGL",
		"_",
		{ "px", "nx", "py", "ny", "pz", "nz" },
		{ false, false, false, false, false, false },
		{ false, false, false, false, false, false },
		{ 0, 0, 0, 0, 0, 0 },
	},
	{
		"Quake",
		"_",
		{ "rt", "lf", "bk", "ft", "up", "dn" },
		{ true, true, false, true, true, false },
		{ false, false, true, false, false, true },
		{ 90, -90, 0, 0, 90, -90 },
	},
	{
		"DarkPlaces",
		"",
		{ "px", "nx", "py", "ny", "pz", "nz" },
		{ false, false, false, false, false, false },
		{ false, false, false, false, false, false },
		{ 0, 0, 0, 0, 0, 0 },
	},
	{
		"Doom 3",
		"_",
		{ "forward", "back", "left", "right", "up", "down" },
		{ true, true, false, true, true, false },
		{ false, false, true, false, false, true },
		{ 90, -90, 0, 0, 90, -90 },
	},
};

struct face_t
{
	int width;
	int height;
};

static const imageExtLoader_t* R_FindCubeLoader( const char *baseName )
{
	const FS::PakInfo* bestPak = nullptr;
	const imageExtLoader_t* bestLoader = nullptr;

	for ( const auto &loader : imageLoaders )
	{
		if ( !loader.cubemap ) continue;

		std::string altName = Str::Format( "%s.%s", baseName, loader.ext );
		const FS::PakInfo* pak = FS::PakPath::LocateFile( altName );

		// FIXME: Move to a more explicit interface once there is one.
		if ( pak != nullptr && ( bestPak == nullptr || pak < bestPak ) )
		{
			/* We found a file and its pak is better than the best pak we have
			this relies on how the filesystem works internally. */
			bestPak = pak;
			bestLoader = &loader;
		}
	}

	return bestLoader;
}

static image_t *R_LoadAndCreateSingleFileCubeImage( const char *imageName, const char* altName, const imageExtLoader_t *loader, imageParams_t& imageParams )
{
	int width = 0, height = 0, numLayers = 0, numMips = 0;

	Log::Debug( "Found %s cube map '%s' candidate: %s", loader->name, imageName, altName );

	byte *pic[ MAX_TEXTURE_MIPS * MAX_TEXTURE_LAYERS ];
	pic[ 0 ] = nullptr;

	loader->imageLoader( altName, pic, &width, &height, &numLayers, &numMips, &imageParams.bits, 0 );

	if ( *pic && numLayers == 6 )
	{
		Log::Debug( "Found %d×%d×6 %s cube map '%s': %s", width, height, loader->name, imageName, altName );
		image_t *image = R_CreateCubeImage( imageName, (const byte **) pic, width, height, imageParams );

		if ( image )
		{
			R_FreeCubePics( pic, 1 );
			return image;
		}
	}

	R_FreeCubePics( pic, 1 );

	return nullptr;
}

/* Finds or loads the given cubemap image.
Returns nullptr if it fails, not a default image. */
image_t *R_FindCubeImage( const char *name, imageParams_t &imageParams )
{
	if ( !name )
	{
		return nullptr;
	}

	unsigned hash = GenerateImageHashValue( name );

	// See if the image is already loaded.
	for ( image_t *image = r_imageHashTable[ hash ]; image; image = image->next )
	{
		if ( !Q_stricmp( name, image->name ) )
		{
			return image;
		}
	}

	// Look for cached 6-face cube map file.
	if ( imageParams.bits & IF_HOMEPATH )
	{
		static const imageExtLoader_t loader = { "ktx", "KTX", LoadKTX, true };
		std::string altName = Str::Format( "%s.%s", name, loader.ext );

		Log::Debug( "Looking for cached %s cube map '%s' candidate: %s", loader.name, name, altName );

		if ( FS::HomePath::FileExists( altName ) )
		{
			return R_LoadAndCreateSingleFileCubeImage( name, altName.c_str(), &loader, imageParams );
		}

		return nullptr;
	}

	// Look for 6-files cube map.
	{
		int width = 0, height = 0, numLayers = 0, numMips = 0;
		byte *pic[ MAX_TEXTURE_MIPS * MAX_TEXTURE_LAYERS ];

		for ( int i = 0; i < 6; i++ )
		{
			pic[ i ] = nullptr;
		}

		for ( const multifileCubeMapFormat_t &format : multifileCubeMapFormats )
		{
			int greatestEdge = 0;
			face_t faces[ 6 ];

			for ( int i = 0 ; i < 6; i++ )
			{
				std::string faceName = Str::Format( "%s%s%s", name, format.sep, format.suffixes[ i ] );

				Log::Debug( "Looking for %s cube map face '%s'", format.name, faceName );

				R_LoadImage( faceName.c_str(), &pic[ i ], &width, &height, &numLayers, &numMips, &imageParams.bits );

				if ( !pic[ i ] )
				{
					/* Ignore silently, skip this format. This may silence incomplete multifile cubemap
					but this is an hardly decidable problem since some formats can share some suffixes */
					R_FreeCubePics( pic, i );
					break;
				}

				if ( IsImageCompressed( imageParams.bits ) )
				{
					Log::Warn("Cube map face '%s' has DXTn compression, cube map unusable", faceName );
					R_FreeCubePics( pic, i + 1 );
					break;
				}

				if ( numLayers > 0 )
				{
					Log::Warn("Cubemap face '%s' is a multilayer image with %d layer(s), cube map unusable", faceName, numLayers);
					R_FreeCubePics( pic, i + 1 );
					break;
				}

				if ( width > greatestEdge )
				{
					greatestEdge = width;
				}

				if ( height > greatestEdge )
				{
					greatestEdge = height;
				}

				faces[ i ].width = width;
				faces[ i ].height = height;
			}

			if ( !*pic )
			{
				continue;
			}

			for ( int j = 0; j < 6; j++ )
			{
				width = faces[ j ].width;
				height = faces[ j ].height;

				bool badSize = false;

				if ( width != height )
				{
					Log::Warn("Cubemap face '%s%s%s' is not a square with %d×%d dimension, resizing to %d×%d",
						name, format.sep, format.suffixes[ j ], width, height, greatestEdge, greatestEdge );
					badSize = true;
				}

				if ( width < greatestEdge || height < greatestEdge )
				{
					Log::Warn("Cubemap face '%s%s%s' is too small with %d×%d dimension, resizing to %d×%d",
						name, format.sep, format.suffixes[ j ], width, height, greatestEdge, greatestEdge );
					badSize = true;
				}

				if ( format.flipX[ j ] )
				{
					R_Flip( pic[ j ], width, height );
				}

				if ( format.flipY[ j ] )
				{
					R_Flop( pic[ j ], width, height );
				}

				// Make face square before doing rotation.
				if ( badSize )
				{
					pic[ j ] = R_Resize( pic[ j ], width, height, greatestEdge, greatestEdge );
				}

				if ( format.rot[ j ] != 0 )
				{
					R_Rotate( pic[ j ], greatestEdge, format.rot[ j ] );
				}
			}

			Log::Debug( "Found %d×%d×6 %s multifile cube map '%s'", greatestEdge, greatestEdge, format.name, name );
			image_t *image = R_CreateCubeImage( name, ( const byte ** ) pic, greatestEdge, greatestEdge, imageParams );

			R_FreeCubePics( pic, 6 );

			if ( image )
			{
				return image;
			}
		}
	}

	// Look for 6-faces single-file CRN or KTX cubemap.
	{
		const char *ext = COM_GetExtension( name );

		/* The Daemon's default strategy is to use the hardcoded path if it exists.
		If we are given the name “some.thing.crn” we will look for “some.thing.crn” first
		and if we are given the name “something.crn” we will look for “something.crn” first. */
		if ( *ext )
		{
			// Look for the correct loader and load the image is the file is found.
			for ( const auto &loader : imageLoaders )
			{
				if ( !loader.cubemap ) continue;

				if ( !Q_stricmp( ext, loader.ext ) )
				{
					/* Do not complain on missing file if the extension is hardcoded to a wrong one since
					the file can exist with another extension and it will tested right after that. */
					if ( FS::PakPath::FileExists( name ) )
					{
						image_t *image = R_LoadAndCreateSingleFileCubeImage( name, name, &loader, imageParams );

						if ( image )
						{
							return image;
						}
					}

					// We have to break because the expected loader was found.
					break;
				}
			}
		}

		/* If we are given the name “some.thing”, we first look for “some.thing.crn”,
		“some.thing.ktx”, etc. It also means that if we are given the name “something.ktx”
		we will also look for “something.ktx.crn, “something.ktx.ktx”, etc. */
		const imageExtLoader_t *loader = R_FindCubeLoader( name );

		if ( loader )
		{
			std::string altName = Str::Format( "%s.%s", name, loader->ext );
			return R_LoadAndCreateSingleFileCubeImage( name, altName.c_str(), loader, imageParams );
		}

		/* Then for the name “something.ktx“ we look for “something.crn”, etc.
		It also means that for the name “some.thing” we look for “some.crn”, “some.ktx”, etc. */
		if ( *ext )
		{
			char baseName[ 1024 ];
			COM_StripExtension3( name, baseName, sizeof(baseName) );

			loader = R_FindCubeLoader( baseName );

			if ( loader )
			{
				std::string altName = Str::Format( "%s.%s", baseName, loader->ext );
				return R_LoadAndCreateSingleFileCubeImage( name, altName.c_str(), loader, imageParams );
			}
		}
	}

	return nullptr;
}

/*
=================
R_InitFogTable
=================
*/
void R_InitFogTable()
{
	int   i;
	float d;
	float exp;

	exp = 0.5;

	for ( i = 0; i < FOG_TABLE_SIZE; i++ )
	{
		d = pow( ( float ) i / ( FOG_TABLE_SIZE - 1 ), exp );

		tr.fogTable[ i ] = d;
	}
}

/*
================
R_FogFactor

Returns a 0.0 to 1.0 fog density value
This is called for each texel of the fog texture on startup
and for each vertex of transparent shaders in fog dynamically
================
*/
float R_FogFactor( float s, float t )
{
	float d;

	s -= 1.0f / 512.0f;

	if ( s < 0 )
	{
		return 0;
	}

	if ( t < 1.0f / 32.0f )
	{
		return 0;
	}

	if ( t < 31.0f / 32.0f )
	{
		s *= ( t - 1.0f / 32.0f ) / ( 30.0f / 32.0f );
	}

	// we need to leave a lot of clamp range
	s *= 8;

	if ( s > 1.0f )
	{
		s = 1.0f;
	}

	d = tr.fogTable[( int )( s * ( FOG_TABLE_SIZE - 1 ) ) ];

	return d;
}

/*
================
R_CreateFogImage
================
*/
static void R_CreateFogImage()
{
	// Fog image is always created because disabling fog is cheat.

	int   x, y;
	byte  *data, *ptr;
	float d;
	float borderColor[ 4 ];

	constexpr int FOG_S = 256;
	constexpr int FOG_T = 32;

	ptr = data = (byte*) ri.Hunk_AllocateTempMemory( FOG_S * FOG_T * 4 );

	// S is distance, T is depth
	for ( y = 0; y < FOG_T; y++ )
	{
		for ( x = 0; x < FOG_S; x++ )
		{
			d = R_FogFactor( ( x + 0.5f ) / FOG_S, ( y + 0.5f ) / FOG_T );

			ptr[ 0 ] = ptr[ 1 ] = ptr[ 2 ] = 255;
			ptr[ 3 ] = 255 * d;
			ptr += 4;
		}
	}

	// standard openGL clamping doesn't really do what we want -- it includes
	// the border color at the edges.  OpenGL 1.2 has clamp-to-edge, which does
	// what we want.
	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP;
	imageParams.filterType = filterType_t::FT_DEFAULT;
	imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

	tr.fogImage = R_CreateImage( "_fog", ( const byte ** ) &data, FOG_S, FOG_T, 1, imageParams );
	ri.Hunk_FreeTempMemory( data );

	borderColor[ 0 ] = 1.0;
	borderColor[ 1 ] = 1.0;
	borderColor[ 2 ] = 1.0;
	borderColor[ 3 ] = 1;

	glTexParameterfv( GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor );
}

/*
==================
R_CreateDefaultImage
==================
*/
static const int DEFAULT_SIZE = 128;
static void R_CreateDefaultImage()
{
	int  x;
	byte data[ DEFAULT_SIZE ][ DEFAULT_SIZE ][ 4 ];
	byte *dataPtr = &data[0][0][0];

	// the default image will be a box, to allow you to see the mapping coordinates
	memset( data, 32, sizeof( data ) );

	for ( x = 0; x < DEFAULT_SIZE; x++ )
	{
		data[ 0 ][ x ][ 0 ] = data[ 0 ][ x ][ 1 ] = data[ 0 ][ x ][ 2 ] = data[ 0 ][ x ][ 3 ] = 255;
		data[ x ][ 0 ][ 0 ] = data[ x ][ 0 ][ 1 ] = data[ x ][ 0 ][ 2 ] = data[ x ][ 0 ][ 3 ] = 255;

		data[ DEFAULT_SIZE - 1 ][ x ][ 0 ] =
		  data[ DEFAULT_SIZE - 1 ][ x ][ 1 ] = data[ DEFAULT_SIZE - 1 ][ x ][ 2 ] = data[ DEFAULT_SIZE - 1 ][ x ][ 3 ] = 255;

		data[ x ][ DEFAULT_SIZE - 1 ][ 0 ] =
		  data[ x ][ DEFAULT_SIZE - 1 ][ 1 ] = data[ x ][ DEFAULT_SIZE - 1 ][ 2 ] = data[ x ][ DEFAULT_SIZE - 1 ][ 3 ] = 255;
	}

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP;
	imageParams.filterType = filterType_t::FT_DEFAULT;
	imageParams.wrapType = wrapTypeEnum_t::WT_REPEAT;

	tr.defaultImage = R_CreateImage( "_default", ( const byte ** ) &dataPtr, DEFAULT_SIZE, DEFAULT_SIZE, 1, imageParams );
}

static void R_CreateRandomNormalsImage()
{
	int  x, y;
	byte data[ DEFAULT_SIZE ][ DEFAULT_SIZE ][ 4 ];
	// the default image will be a box, to allow you to see the mapping coordinates
	memset(data, 32, sizeof(data));

	byte *ptr = &data[0][0][0];
	byte *dataPtr = &data[0][0][0];

	for ( y = 0; y < DEFAULT_SIZE; y++ )
	{
		for ( x = 0; x < DEFAULT_SIZE; x++ )
		{
			vec3_t n;
			float  r, angle;

			r = random();
			angle = 2.0f * M_PI * r; // / 360.0f;

			VectorSet( n, cosf( angle ), sinf( angle ), r );
			VectorNormalize( n );

			ptr[ 0 ] = ( byte )( 128 + 127 * n[ 0 ] );
			ptr[ 1 ] = ( byte )( 128 + 127 * n[ 1 ] );
			ptr[ 2 ] = ( byte )( 128 + 127 * n[ 2 ] );
			ptr[ 3 ] = 255;
			ptr += 4;
		}
	}

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP;
	imageParams.filterType = filterType_t::FT_DEFAULT;
	imageParams.wrapType = wrapTypeEnum_t::WT_REPEAT;

	tr.randomNormalsImage = R_CreateImage( "_randomNormals", ( const byte ** ) &dataPtr, DEFAULT_SIZE, DEFAULT_SIZE, 1, imageParams );
}

static void R_CreateNoFalloffImage()
{
	byte data[ DEFAULT_SIZE ][ DEFAULT_SIZE ][ 4 ];
	// we use a solid white image instead of disabling texturing
	memset(data, 255, sizeof(data));

	byte *dataPtr = &data[0][0][0];

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP;
	imageParams.filterType = filterType_t::FT_DEFAULT;
	imageParams.wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;

	tr.noFalloffImage = R_CreateImage( "_noFalloff", ( const byte ** ) &dataPtr, 8, 8, 1, imageParams );
}

static void R_CreateContrastRenderFBOImage()
{
	if ( !glConfig2.bloom)
	{
		return;
	}

	const int width = glConfig.vidWidth * 0.25f;
	const int height = glConfig.vidHeight * 0.25f;

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP;
	imageParams.filterType = filterType_t::FT_LINEAR;
	imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

	tr.contrastRenderFBOImage = R_CreateImage( "_contrastRenderFBO", nullptr, width, height, 1, imageParams );
}

static void R_CreateBloomRenderFBOImages()
{
	if ( !glConfig2.bloom)
	{
		return;
	}

	const int width = glConfig.vidWidth * 0.25f;
	const int height = glConfig.vidHeight * 0.25f;

	for ( int i = 0; i < 2; i++ )
	{
		imageParams_t imageParams = {};
		imageParams.bits = IF_NOPICMIP;
		imageParams.filterType = filterType_t::FT_LINEAR;
		imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

		tr.bloomRenderFBOImage[i] = R_CreateImage( va( "_bloomRenderFBO%d", i ), nullptr, width, height, 1, imageParams );
	}
}

static void R_CreateCurrentRenderImage()
{
	int width = glConfig.vidWidth;
	int height = glConfig.vidHeight;

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP;

	if ( r_highPrecisionRendering.Get() )
	{
		if ( !glConfig2.textureFloatAvailable )
		{
			Log::Warn( "High-precision current render disabled because RGBA16F framebuffer is not available" );
		}
		else
		{
			// Use float color buffer so that we can store intermediate results of a multi-stage
			// shader which are greater than 1. For example, the sum of multiple lightmaps from
			// a light style, or simply a single overbright-scaled lightmap which we did not
			// manage to collapse with the diffuse.
			// Besides preventing clamping of the color buffer itself, using a float color buffer
			// also prevents the fragment shader's output color from being clamped before blending.
			imageParams.bits |= IF_RGBA16F;
		}
	}

	imageParams.filterType = filterType_t::FT_NEAREST;
	imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

	tr.currentRenderImage[0] = R_CreateImage( "_currentRender[0]", nullptr, width, height, 1, imageParams );
	tr.currentRenderImage[1] = R_CreateImage( "_currentRender[1]", nullptr, width, height, 1, imageParams );

	imageParams = {};
	imageParams.bits = IF_NOPICMIP | IF_PACKED_DEPTH24_STENCIL8;
	imageParams.filterType = filterType_t::FT_NEAREST;
	imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

	tr.currentDepthImage = R_CreateImage( "_currentDepth", nullptr, width, height, 1, imageParams );

	if ( glConfig2.usingMaterialSystem ) {
		materialSystem.GenerateDepthImages( width, height, imageParams );
	}
}

static void R_CreateDepthRenderImage()
{
	ASSERT( glConfig2.textureFloatAvailable );

	if ( !glConfig2.realtimeLighting )
	{
		return;
	}

	if ( r_realtimeLightingRenderer.Get() != Util::ordinal( realtimeLightingRenderer_t::TILED ) )
	{
		/* Do not create lightTile images when the tiled renderer is not used.

		Those images are part of the tiled dynamic lighting renderer,
		it's better to not create them and save memory when such effects
		are disabled.

		Some hardware not powerful enough to supported dynamic lighting may
		even not support the related formats.

		See https://github.com/DaemonEngine/Daemon/issues/745 */

		return;
	}

	{
		int width = glConfig.vidWidth;
		int height = glConfig.vidHeight;

		int w = (width + TILE_SIZE_STEP1 - 1) >> TILE_SHIFT_STEP1;
		int h = (height + TILE_SIZE_STEP1 - 1) >> TILE_SHIFT_STEP1;

		imageParams_t imageParams = {};
		imageParams.filterType = filterType_t::FT_NEAREST;
		imageParams.wrapType = wrapTypeEnum_t::WT_ONE_CLAMP;

		imageParams.bits = IF_NOPICMIP;
		imageParams.bits |= r_highPrecisionRendering.Get() ? IF_RGBA32F : IF_RGBA16F;

		tr.depthtile1RenderImage = R_CreateImage( "_depthtile1Render", nullptr, w, h, 1, imageParams );

		w = (width + TILE_SIZE - 1) >> TILE_SHIFT;
		h = (height + TILE_SIZE - 1) >> TILE_SHIFT;

		imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

		tr.depthtile2RenderImage = R_CreateImage( "_depthtile2Render", nullptr, w, h, 1, imageParams );

		imageParams.bits = IF_NOPICMIP | IF_RGBA32UI;

		tr.lighttileRenderImage = R_Create3DImage( "_lighttileRender", nullptr, w, h, glConfig2.realtimeLightLayers, imageParams );
	}
}

static void R_CreatePortalRenderImage()
{
	int  width, height;

	width = glConfig.vidWidth;
	height = glConfig.vidHeight;

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP;
	imageParams.filterType = filterType_t::FT_NEAREST;
	imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

	tr.portalRenderImage = R_CreateImage( "_portalRender", nullptr, width, height, 1, imageParams );
}

// *INDENT-OFF*
static void R_CreateShadowMapFBOImage()
{
	if ( !glConfig2.shadowMapping )
	{
		return;
	}

	int numFactor = 1;
	int format = IF_NOPICMIP;

	// FIXME: We should test if those formats are supported.
	switch( glConfig2.shadowingMode )
	{
		case shadowingMode_t::SHADOWING_ESM16:
			format |= IF_ONECOMP16F;
			break;
		case shadowingMode_t::SHADOWING_ESM32:
			format |= IF_ONECOMP32F;
			break;
		case shadowingMode_t::SHADOWING_VSM16:
			numFactor = 2;
			format |= IF_TWOCOMP16F;
			break;
		case shadowingMode_t::SHADOWING_VSM32:
			numFactor = 2;
			format |= IF_TWOCOMP32F;
			break;
		case shadowingMode_t::SHADOWING_EVSM32:
			numFactor = 2;
			if ( r_evsmPostProcess->integer )
			{
				format |= IF_ONECOMP32F;
			}
			else
			{
				format |= IF_RGBA32F;
			}
			break;
		case shadowingMode_t::SHADOWING_NONE:
		case shadowingMode_t::SHADOWING_BLOB:
		default:
			DAEMON_ASSERT( false );
			return;
	}

	int numShadowMaps = MAX_SHADOWMAPS;

	if ( r_softShadowsPP->integer )
	{
		numShadowMaps *= numFactor;
	}

	filterType_t filter = filterType_t::FT_NEAREST;

	if( r_shadowMapLinearFilter->integer )
	{
		filter = filterType_t::FT_LINEAR;
	}

	imageParams_t imageParams = {};
	imageParams.bits = format;
	imageParams.filterType = filter;
	imageParams.wrapType = wrapTypeEnum_t::WT_ONE_CLAMP;

	for ( int i = 0; i < numShadowMaps; i++ )
	{
		int size = shadowMapResolutions[ i % MAX_SHADOWMAPS ];

		tr.shadowMapFBOImage[ i ] = R_CreateImage( va( "_shadowMapFBO%d", i ), nullptr, size, size, 1, imageParams );
		tr.shadowClipMapFBOImage[ i ] = R_CreateImage( va( "_shadowClipMapFBO%d", i ), nullptr, size, size, 1, imageParams );
	}

	// sun shadow maps
	for ( int i = 0; i < numShadowMaps; i++ )
	{
		int size = sunShadowMapResolutions[ i % MAX_SHADOWMAPS ];

		tr.sunShadowMapFBOImage[ i ] = R_CreateImage( va( "_sunShadowMapFBO%d", i ), nullptr, size, size, 1, imageParams );
		tr.sunShadowClipMapFBOImage[ i ] = R_CreateImage( va( "_sunShadowClipMapFBO%d", i ), nullptr, size, size, 1, imageParams );
	}
}

// *INDENT-ON*

// *INDENT-OFF*
static void R_CreateShadowCubeFBOImage()
{
	if ( !glConfig2.shadowMapping )
	{
		return;
	}

	int format = IF_NOPICMIP;

	// FIXME: We should test if those formats are supported.
	switch( glConfig2.shadowingMode )
	{
		case shadowingMode_t::SHADOWING_ESM16:
			format |= IF_ONECOMP16F;
			break;
		case shadowingMode_t::SHADOWING_ESM32:
			format |= IF_ONECOMP32F;
			break;
		case shadowingMode_t::SHADOWING_VSM16:
			format |= IF_TWOCOMP16F;
			break;
		case shadowingMode_t::SHADOWING_VSM32:
			format |= IF_TWOCOMP32F;
			break;
		case shadowingMode_t::SHADOWING_EVSM32:
			if ( r_evsmPostProcess->integer )
			{
				format |= IF_ONECOMP32F;
			}
			else
			{
				format |= IF_RGBA32F;
			}
			break;
		case shadowingMode_t::SHADOWING_NONE:
		case shadowingMode_t::SHADOWING_BLOB:
		default:
			DAEMON_ASSERT( false );
			return;
	}

	filterType_t filter = filterType_t::FT_NEAREST;

	if ( r_shadowMapLinearFilter->integer )
	{
		filter = filterType_t::FT_LINEAR;
	}

	imageParams_t imageParams = {};
	imageParams.bits = format;
	imageParams.filterType = filter;
	imageParams.wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;

	for ( int i = 0; i < 5; i++ )
	{
		int size = shadowMapResolutions[ i ];

		tr.shadowCubeFBOImage[ i ] = R_CreateCubeImage( va( "_shadowCubeFBO%d", i ), nullptr, size, size, imageParams );
		tr.shadowClipCubeFBOImage[ i ] = R_CreateCubeImage( va( "_shadowClipCubeFBO%d", i ), nullptr, size, size, imageParams );
	}
}

// *INDENT-ON*

// *INDENT-OFF*
static void R_CreateBlackCubeImage()
{
	constexpr int width = REF_CUBEMAP_SIZE, height = REF_CUBEMAP_SIZE;
	byte data[ 4 * width * height ];

	for ( int i = 0; i < width * height; i++ )
	{
		Vector4Set( data + 4 * i, 0, 0, 0, 255 );
	}

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP;
	imageParams.filterType = filterType_t::FT_LINEAR;
	imageParams.wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;

	const byte *dataPtrs[ 6 ] = { data, data, data, data, data, data };
	tr.blackCubeImage = R_CreateCubeImage( "_blackCube", dataPtrs, width, height, imageParams );
}

// *INDENT-ON*

// *INDENT-OFF*
static void R_CreateWhiteCubeImage()
{
	int  i;
	int  width, height;
	byte *data[ 6 ];

	width = REF_CUBEMAP_SIZE;
	height = REF_CUBEMAP_SIZE;

	for ( i = 0; i < 6; i++ )
	{
		data[ i ] = (byte*) ri.Hunk_AllocateTempMemory( width * height * 4 );
		memset( data[ i ], 0xFF, width * height * 4 );
	}

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP;
	imageParams.filterType = filterType_t::FT_LINEAR;
	imageParams.wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;

	tr.whiteCubeImage = R_CreateCubeImage( "_whiteCube", ( const byte ** ) data, width, height, imageParams );

	for ( i = 5; i >= 0; i-- )
	{
		ri.Hunk_FreeTempMemory( data[ i ] );
	}
}

// *INDENT-ON*

static void R_CreateColorGradeImage()
{
	if ( !glConfig2.colorGrading )
	{
		return;
	}

	byte *data, *ptr;
	int i, r, g, b;

	data = (byte*) ri.Hunk_AllocateTempMemory( REF_COLORGRADE_SLOTS * REF_COLORGRADEMAP_STORE_SIZE * sizeof(u8vec4_t) );

	// 255 is 15 * 17, so the colors range from 0 to 255
	for( ptr = data, i = 0; i < REF_COLORGRADE_SLOTS; i++ ) {
		glState.colorgradeSlots[ i ] = nullptr;

		for( b = 0; b < REF_COLORGRADEMAP_SIZE; b++ ) {
			for( g = 0; g < REF_COLORGRADEMAP_SIZE; g++ ) {
				for( r = 0; r < REF_COLORGRADEMAP_SIZE; r++ ) {
					*ptr++ = (byte) r * 17;
					*ptr++ = (byte) g * 17;
					*ptr++ = (byte) b * 17;
					*ptr++ = 255;
				}
			}
		}
	}

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP;
	imageParams.filterType = filterType_t::FT_LINEAR;
	imageParams.wrapType = wrapTypeEnum_t::WT_EDGE_CLAMP;

	tr.colorGradeImage = R_Create3DImage( "_colorGrade", data, REF_COLORGRADEMAP_SIZE, REF_COLORGRADEMAP_SIZE, REF_COLORGRADE_SLOTS * REF_COLORGRADEMAP_SIZE, imageParams );

	ri.Hunk_FreeTempMemory( data );
}

/*
==================
R_CreateBuiltinImages
==================
*/
void R_CreateBuiltinImages()
{
	int   x, y;
	byte  data[ DEFAULT_SIZE ][ DEFAULT_SIZE ][ 4 ];
	byte  *dataPtr = &data[0][0][0];
	byte  *out;
	float s, value;
	byte  intensity;

	R_CreateDefaultImage();

	// we use a solid white image instead of disabling texturing
	memset( data, 255, sizeof( data ) );

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP;
	imageParams.filterType = filterType_t::FT_LINEAR;
	imageParams.wrapType = wrapTypeEnum_t::WT_REPEAT;

	tr.whiteImage = R_CreateImage( "_white", ( const byte ** ) &dataPtr, 8, 8, 1, imageParams );

	// we use a solid black image instead of disabling texturing
	memset( data, 0, sizeof( data ) );
	tr.blackImage = R_CreateImage( "_black", ( const byte ** ) &dataPtr, 8, 8, 1, imageParams );

	// red
	for ( x = DEFAULT_SIZE * DEFAULT_SIZE, out = &data[0][0][0]; x; --x, out += 4 )
	{
		out[ 1 ] = out[ 2 ] = 0;
		out[ 0 ] = out[ 3 ] = 255;
	}

	tr.redImage = R_CreateImage( "_red", ( const byte ** ) &dataPtr, 8, 8, 1, imageParams );

	// green
	for ( x = DEFAULT_SIZE * DEFAULT_SIZE, out = &data[0][0][0]; x; --x, out += 4 )
	{
		out[ 0 ] = out[ 2 ] = 0;
		out[ 1 ] = out[ 3 ] = 255;
	}

	tr.greenImage = R_CreateImage( "_green", ( const byte ** ) &dataPtr, 8, 8, 1, imageParams );

	// blue
	for ( x = DEFAULT_SIZE * DEFAULT_SIZE, out = &data[0][0][0]; x; --x, out += 4 )
	{
		out[ 0 ] = out[ 1 ] = 0;
		out[ 2 ] = out[ 3 ] = 255;
	}

	tr.blueImage = R_CreateImage( "_blue", ( const byte ** ) &dataPtr, 8, 8, 1, imageParams );

	// generate a default normalmap with a fully opaque heightmap (no displacement)
	for ( x = DEFAULT_SIZE * DEFAULT_SIZE, out = &data[0][0][0]; x; --x, out += 4 )
	{
		out[ 0 ] = out[ 1 ] = 128;
		out[ 2 ] = 255;
		out[ 3 ] = 255;
	}

	imageParams.bits = IF_NOPICMIP | IF_NORMALMAP;

	tr.flatImage = R_CreateImage( "_flat", ( const byte ** ) &dataPtr, 8, 8, 1, imageParams );

	imageParams.bits = IF_NOPICMIP;
	imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

	for ( image_t * &image : tr.cinematicImage )
	{
		image = R_CreateImage( "_cinematic", ( const byte ** ) &dataPtr, 1, 1, 1, imageParams );
	}

	out = &data[ 0 ][ 0 ][ 0 ];

	for ( y = 0; y < DEFAULT_SIZE; y++ )
	{
		for ( x = 0; x < DEFAULT_SIZE; x++, out += 4 )
		{
			s = ( ( ( float ) x + 0.5f ) * ( 2.0f / DEFAULT_SIZE ) - 1.0f );

			s = Q_fabs( s ) - ( 1.0f / DEFAULT_SIZE );

			value = 1.0f - ( s * 2.0f ) + ( s * s );

			intensity = ClampByte( Q_ftol( value * 255.0f ) );

			out[ 0 ] = intensity;
			out[ 1 ] = intensity;
			out[ 2 ] = intensity;
			out[ 3 ] = intensity;
		}
	}

	tr.quadraticImage = R_CreateImage( "_quadratic", ( const byte ** ) &dataPtr, DEFAULT_SIZE, DEFAULT_SIZE, 1, imageParams );

	R_CreateRandomNormalsImage();
	R_CreateFogImage();
	R_CreateNoFalloffImage();
	R_CreateContrastRenderFBOImage();
	R_CreateBloomRenderFBOImages();
	R_CreateCurrentRenderImage();
	R_CreateDepthRenderImage();
	R_CreatePortalRenderImage();
	R_CreateShadowMapFBOImage();
	R_CreateShadowCubeFBOImage();
	R_CreateBlackCubeImage();
	R_CreateWhiteCubeImage();
	R_CreateColorGradeImage();
}

/*
===============
R_InitImages
===============
*/
void R_InitImages()
{
	Log::Debug("------- R_InitImages -------" );

	memset( r_imageHashTable, 0, sizeof( r_imageHashTable ) );
	tr.images.reserve( 4096 );
	tr.lightmaps.reserve( 128 );
	tr.deluxemaps.reserve( 128 );

	/* These are the values expected by the rest of the renderer
	(esp. tr_bsp), used for "gamma correction of the map".
	Both were set to 0 if we had neither COMPAT_ET nor COMPAT_Q3,
	it may be interesting to remember.

	Quake 3 and Tremulous values:

	  tr.overbrightBits = 0; // Software implementation.
	  tr.mapOverBrightBits = 2; // Quake 3 default.
	  tr.identityLight = 1.0f / ( 1 << tr.overbrightBits );

	Games like Quake 3 and Tremulous require tr.mapOverBrightBits
	to be set to 2. Because this engine is primarily maintained for
	Unvanquished and needs to keep compatibility with legacy Tremulous
	maps, this value is set to 2.

	Games like True Combat: Elite (Wolf:ET mod) or Urban Terror 4
	(Quake 3 mod) require tr.mapOverBrightBits to be set to 0.

	For some reasons the Wolf:ET engine sets this to 0 by default
	but the game sets it to 2 (and requires it to be 2 for maps
	looking as expected).

	The mapOverBrightBits value will be read as map entity key
	by R_LoadEntities() in tr_bsp, making possible to override
	the default value and properly render a map with another
	value than the default one.

	If this key is missing in map entity lump, there is no way
	to know the required value for mapOverBrightBits when loading
	a BSP, one may rely on arena files to do some guessing when
	loading foreign maps and games ported to the Dæmon engine may
	require to set a different default than what Unvanquished
	requires.

	Using a non-zero value for tr.mapOverBrightBits turns light
	non-linear and makes deluxe mapping buggy though.

	Mappers may port and fix maps by multiplying the lights by 2.5
	and set the mapOverBrightBits key to 0 in map entities lump.

	It will be possible to assume tr.mapOverBrightBits is 0 when
	loading maps compiled with sRGB lightmaps as there is no
	legacy map using sRGB lightmap yet, and then we will be
	able to avoid the need to explicitly set mapOverBrightBits
	to 0 in map entities. It will be required to assume that
	tr.mapOverBrightBits is 0 when loading maps compiled with
	sRGB lightmaps because otherwise the color shift computation
	will break the light computation, not only the deluxe one.

	In legacy engines, tr.overbrightBits was non-zero when
	hardware overbright bits were enabled, zero when disabled.
	This engine do not implement hardware overbright bit, so
	this is always zero, and we can remove it and simplify all
	the computations making use of it.

	Because tr.overbrightBits is always 0, tr.identityLight is
	always 1.0f. We can entirely remove it. */

	tr.mapOverBrightBits = r_overbrightDefaultExponent.Get();
	tr.legacyOverBrightClamping = r_overbrightDefaultClamp.Get();

	// create default texture and white texture
	R_CreateBuiltinImages();
}

/*
===============
R_ShutdownImages
===============
*/
void R_ShutdownImages()
{
	Log::Debug("------- R_ShutdownImages -------" );

	for ( image_t *image : tr.images )
	{
		if ( image->texture->IsResident() ) {
			image->texture->MakeNonResident();
		}

		glDeleteTextures( 1, &image->texnum );
		delete image->texture;
	}
	tr.textureManager.FreeTextures();

	tr.currenttextures.clear();

	tr.images.clear();
	tr.lightmaps.clear();
	tr.deluxemaps.clear();
	tr.cubeProbes.clear();
}

void RE_GetTextureSize( int textureID, int *width, int *height )
{
	image_t *baseImage;
	shader_t *shader;

	shader = R_GetShaderByHandle( textureID );

	ASSERT(shader != nullptr);
	if ( !shader )
	{
		return;
	}

	baseImage = shader->stages[ 0 ].bundle[ 0 ].image[ 0 ];
	if ( !baseImage )
	{
		Log::Debug( "%sRE_GetTextureSize: shader %s is missing base image",
			Color::ToString( Color::Yellow ),
			shader->name );
		return;
	}

	if ( width )
	{
		*width = baseImage->width;
	}
	if ( height )
	{
		*height = baseImage->height;
	}
}

// This code is used to upload images produced by the game (like GUI elements produced by libRocket in Unvanquished)
int numTextures = 0;

qhandle_t RE_GenerateTexture( const byte *pic, int width, int height )
{
	size_t size = width * height;

	if ( !size ) {
		Log::Warn("RE_GenerateTexture: image size %dx%d is 0", width, height );
		return 0;
	}

	R_SyncRenderThread();

	const char *name = va( "rocket%d", numTextures++ );

	imageParams_t imageParams = {};
	imageParams.bits = IF_NOPICMIP;
	imageParams.filterType = filterType_t::FT_LINEAR;
	imageParams.wrapType = wrapTypeEnum_t::WT_CLAMP;

	return RE_RegisterShaderFromImage( name, R_CreateImage( name, &pic, width, height, 1, imageParams ) );
}
