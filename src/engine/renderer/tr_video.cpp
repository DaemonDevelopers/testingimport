/*
===========================================================================

Daemon GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

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

// tr_video.cpp: ROQ video playback on in-game surfaces
//
// This is a greatly cut-down version of the ROQ support in Quake 3. Cinematic mode has
// been removed, along with a lot of dead or broken code. If something with in-game videos
// works with Q3 but not Daemon, one may consult the ioq3 implementation:
// https://github.com/ioquake/ioq3/blob/main/code/client/cl_cin.c
// Variable and function names have been kept close to the original, even when the names
// don't make much sense, to assist in the comparison.
//
// Some of the old limitations on in-game videos are kept (See
// https://openarena.fandom.com/wiki/RoQ for more information):
// * Must be 30 FPS
// * Sound is not supported
//
// However, unlike the original implementation, in Daemon you may have:
// * Sizes other than 256x256 (as long as they are divisible by 16)
// * Multiple videos playing concurrently
//
// As videoMap is a rarely-used feature, security is prioritized over performance here. All
// array accesses use bounds checking. The Q3 implementation had many buffer overflow
// vulnerabilities.

#include "common/Common.h"

#include "tr_local.h"
#include "framework/CommandSystem.h"

static Log::Logger logger("renderer.video");

constexpr int MAX_DIMENSION = 4096;
constexpr int MAX_FRAME_DATA_SIZE = 65536;

// Commands
#define ROQ_QUAD_INFO      0x1001
#define ROQ_CODEBOOK       0x1002
#define ROQ_QUAD_VQ        0x1011
#define ROQ_QUAD_JPEG      0x1012
#define ROQ_QUAD_HANG      0x1013
#define ROQ_PACKET         0x1030
#define ZA_SOUND_MONO      0x1020
#define ZA_SOUND_STEREO    0x1021

constexpr int SAMPLES_PER_PIXEL = 4; // bytes of data per pixel
constexpr int ROQ_FPS = 30;

static std::array<int, 256> ROQ_YY_tab;
static std::array<int, 256> ROQ_UB_tab;
static std::array<int, 256> ROQ_UG_tab;
static std::array<int, 256> ROQ_VG_tab;
static std::array<int, 256> ROQ_VR_tab;

enum class roqStatus_t
{
	DEAD, // failed to load or stopped due to error
	PLAYING,
	LOOPED,
};

struct roqVideo_t
{
	roqStatus_t status;
	std::string fileName;

	// entire file. this seems stupid but there is no PakPath API that doesn't read the entire file
	std::string entireFile;
	// number of bytes "read" from the file
	size_t RoQPlayed;

	// size info from ROQ_QUAD_INFO
	unsigned int xsize, ysize;

	// stuff which is derived purely from xsize/ysize
	int samplesPerLine;
	std::vector<int> qStatus;

	bool dirty;
	int startTime;
	int numQuads; // number of frames decoded

	std::vector<byte> doneBuf, activeBuf; // previous/next frame image

	std::array<int, 256> mcomp; // "motion compensation"

	// "codebook" data
	std::array<unsigned int, 256 * 4> vq2;
	std::array<unsigned int, 512 * 2 * 4> vq4;
	std::array<unsigned int, 512 * 2 * 16> vq8;
};

class BadROQException : public std::runtime_error
{
public:
	BadROQException( Str::StringRef what ) : runtime_error( what.c_str() ) {}
};

static std::vector<roqVideo_t> cinTable;

static Cvar::Cvar<bool> cl_inGameVideo(
	"r_video_enabled", "allow video playback on in-game surfaces", Cvar::NONE, true);
static Cvar::Cvar<int> maxFrameSkip(
	"r_video_maxFrameSkip", "max frames not renderered before pausing a video", Cvar::NONE, 30);

// Fake I/O operation. The result is stored into a vector to make it easier to
// do bounds-checked reads (C++20 std::span would be better)
static std::vector<byte> Read( roqVideo_t &video, size_t count )
{
	if ( count > video.entireFile.size() - video.RoQPlayed )
	{
		throw BadROQException( "file truncated" );
	}

	auto start = video.entireFile.begin() + video.RoQPlayed;
	video.RoQPlayed += count;
	return std::vector<byte>( start, start + count );
}

static void StopVideo( roqVideo_t &video )
{
	logger.Verbose( "stopping video %s due to error", video.fileName );
	video.entireFile.clear();
	video.status = roqStatus_t::DEAD;
}

void CIN_CloseAllVideos()
{
	cinTable.clear();
}

static void checked_memcpy(
	const char *srcBegin, size_t srcSize, size_t srcOffset,
	char *dstBegin, size_t dstSize, size_t dstOffset,
	size_t count )
{
	if ( srcOffset > srcSize
		|| count > srcSize - srcOffset
		|| dstOffset > dstSize
		|| count > dstSize - dstOffset )
	{
		throw BadROQException( "checked_memcpy out of bounds" );
	}

	memcpy( dstBegin + dstOffset, srcBegin + srcOffset, count );
}

template<typename SrcT, typename DstT>
static void checked_memcpy( const SrcT &src, size_t srcOffset, DstT &dst, size_t dstOffset, size_t nbytes )
{
	if ( src.size() == 0 || dst.size() == 0 )
	{
		Sys::Error( "ROQ checked_memcpy: zero-length buffer" );
	}

	size_t srcSize = ( std::end( src ) - std::begin( src ) ) * sizeof( src.at( 0 ) );
	size_t dstSize = ( std::end( dst ) - std::begin( dst ) ) * sizeof( dst.at( 0 ) );
	checked_memcpy(
		reinterpret_cast<const char *>( &src.at( 0 ) ), srcSize, srcOffset,
		reinterpret_cast<char *>( &dst.at( 0 ) ), dstSize, dstOffset, nbytes );
}

static void move8_32( roqVideo_t &video, unsigned doneOffset, unsigned activeOffset )
{
	for ( int i = 0; i < 8; ++i )
	{
		checked_memcpy( video.doneBuf, doneOffset, video.activeBuf, activeOffset, 32 );
		doneOffset += video.samplesPerLine;
		activeOffset += video.samplesPerLine;
	}
}

static void move4_32( roqVideo_t &video, unsigned doneOffset, unsigned activeOffset )
{
	for ( int i = 0; i < 4; ++i )
	{
		checked_memcpy( video.doneBuf, doneOffset, video.activeBuf, activeOffset, 16 );
		doneOffset += video.samplesPerLine;
		activeOffset += video.samplesPerLine;
	}
}

static void blit8_32( roqVideo_t &video, unsigned vq8Offset, unsigned bufOffset )
{
	for ( int i = 0; i < 8; ++i )
	{
		checked_memcpy( video.vq8, vq8Offset, video.activeBuf, bufOffset, 32 );
		vq8Offset += 32;
		bufOffset += video.samplesPerLine;
	}
}

static void blit4_32( roqVideo_t &video, unsigned vq4Offset, unsigned bufOffset )
{
	for ( int i = 0; i < 4; ++i )
	{
		checked_memcpy( video.vq4, vq4Offset, video.activeBuf, bufOffset, 16 );
		vq4Offset += 16;
		bufOffset += video.samplesPerLine;
	}
}

static void blit2_32( roqVideo_t &video, unsigned vq2Offset, unsigned bufOffset )
{
	checked_memcpy( video.vq2, vq2Offset, video.activeBuf, bufOffset, 8 );
	checked_memcpy(
		video.vq2, vq2Offset + 8, video.activeBuf, bufOffset + video.samplesPerLine, 8 );
}

// In this function the file.at( d ) reads may go out of bounds with bad data
// We use the bounds-checked at() so that it crashes right away rather than allowing memory corruption
static void blitVQQuad32fs( roqVideo_t &video, const std::vector<byte> &file )
{
	unsigned newd = 0;
	unsigned short celdata = 0;
	unsigned int index = 0;
	unsigned d = 0;

	do
	{
		if ( !newd )
		{
			newd = 7;
			celdata = file.at( d ) + file.at( d + 1 ) * 256;
			d += 2;
		}
		else
		{
			newd--;
		}

		unsigned short code = celdata & 0xc000;
		celdata <<= 2;

		switch ( code )
		{
			case 0x8000: // vq code
				blit8_32( video, file.at( d ) * 256, video.qStatus.at( index ) );
				d++;
				index += 5;
				break;

			case 0xc000: // drop
				index++; // skip 8x8

				for ( int i = 0; i < 4; i++ )
				{
					if ( !newd )
					{
						newd = 7;
						celdata = file.at( d ) + file.at( d + 1 ) * 256;
						d += 2;
					}
					else
					{
						newd--;
					}

					code = ( unsigned short )( celdata & 0xc000 );
					celdata <<= 2;

					switch ( code ) // code in top two bits of code
					{
						case 0x8000: // 4x4 vq code
							blit4_32( video, file.at( d ) * 64, video.qStatus.at( index ) );
							d++;
							break;

						case 0xc000: // 2x2 vq code
							blit2_32( video, file.at( d ) * 16, video.qStatus.at( index ) );
							d++;
							blit2_32( video, file.at( d ) * 16, video.qStatus.at( index ) + 8 );
							d++;
							blit2_32( video, file.at( d ) * 16,
								video.qStatus.at( index ) + video.samplesPerLine * 2 );
							d++;
							blit2_32( video, file.at( d ) * 16,
								video.qStatus.at( index ) + video.samplesPerLine * 2 + 8 );
							d++;
							break;

						case 0x4000: // motion compensation
							move4_32( video, video.qStatus.at( index ) + video.mcomp.at( file.at( d ) ),
								video.qStatus.at( index ) );
							d++;
							break;
					}

					index++;
				}

				break;

			case 0x4000: // motion compensation
				move8_32( video, video.qStatus.at( index ) + video.mcomp.at( file.at( d ) ),
					video.qStatus.at( index ) );
				d++;
				index += 5;
				break;

			case 0x0000:
				index += 5;
				break;
		}
	}
	while ( index < video.qStatus.size() );
}

static void ROQ_GenYUVTables()
{
	float t_ub = ( 1.77200f * 0.5f ) * ( float )( 1 << 6 ) + 0.5f;
	float t_vr = ( 1.40200f * 0.5f ) * ( float )( 1 << 6 ) + 0.5f;
	float t_ug = ( 0.34414f * 0.5f ) * ( float )( 1 << 6 ) + 0.5f;
	float t_vg = ( 0.71414f * 0.5f ) * ( float )( 1 << 6 ) + 0.5f;

	for ( int i = 0; i < 256; i++ )
	{
		float x = ( float )( 2 * i - 255 );

		ROQ_UB_tab.at( i ) = ( int )( ( t_ub * x ) + ( 1 << 5 ) );
		ROQ_VR_tab.at( i ) = ( int )( ( t_vr * x ) + ( 1 << 5 ) );
		ROQ_UG_tab.at( i ) = ( int )( ( -t_ug * x ) );
		ROQ_VG_tab.at( i ) = ( int )( ( -t_vg * x ) + ( 1 << 5 ) );
		ROQ_YY_tab.at( i ) = ( i << 6 ) | ( i >> 2 );
	}
}

static unsigned int yuv_to_rgb24( int y, int u, int v )
{
	int YY = ROQ_YY_tab.at( y );

	int r = ( YY + ROQ_VR_tab.at( v ) ) >> 6;
	int g = ( YY + ROQ_UG_tab.at( u ) + ROQ_VG_tab.at( v ) ) >> 6;
	int b = ( YY + ROQ_UB_tab.at( u ) ) >> 6;

	r = Math::Clamp( r, 0, 255 );
	g = Math::Clamp( g, 0, 255 );
	b = Math::Clamp( b, 0, 255 );

	return LittleLong( ( r ) | ( g << 8 ) | ( b << 16 ) | ( 255 << 24 ) );
}

static void decodeCodeBook(
	roqVideo_t &video, const std::vector<byte> &file, unsigned short roq_flags )
{
	int two;
	int four;

	if ( !roq_flags )
	{
		two = four = 256;
	}
	else
	{
		two = roq_flags >> 8;

		if ( !two ) { two = 256; }

		four = roq_flags & 0xff;
	}

	four *= 2;

	ASSERT( two >= 1 && two <= 256 );
	ASSERT( four >= 0 && four <= 512 );

	size_t firstReadSize = 6 * two;
	if ( file.size() < firstReadSize )
	{
		throw BadROQException( "not enough data for codebook" );
	}

	size_t input = 0;
	static_assert( decltype(video.vq2)().size() >= 4U * 256, "vq2 too short for following writes" );

	for ( size_t ib = 0; ib < 4U * two; ib += 4 )
	{
		int y0 = file.at( input++ );
		int y1 = file.at( input++ );
		int y2 = file.at( input++ );
		int y3 = file.at( input++ );
		int cr = file.at( input++ );
		int cb = file.at( input++ );
		video.vq2.at( ib + 0 ) = yuv_to_rgb24( y0, cr, cb );
		video.vq2.at( ib + 1 ) = yuv_to_rgb24( y1, cr, cb );
		video.vq2.at( ib + 2 ) = yuv_to_rgb24( y2, cr, cb );
		video.vq2.at( ib + 3 ) = yuv_to_rgb24( y3, cr, cb );
	}

	ASSERT_EQ( input, firstReadSize );

	if ( file.size() == firstReadSize )
	{
		// only vq2 is populated
		// TODO: detect an error if vq4/vq8 are used later
		return;
	}

	size_t totalReadSize = firstReadSize + 2 * four;
	if ( file.size() != totalReadSize )
	{
		throw BadROQException( "wrong data size for codebook" );
	}

	size_t ic = 0;
	size_t id = 0;
	static_assert( decltype(video.vq4)().size() >= 512 * 2 * 4, "vq4 too short for following writes" );
	static_assert( decltype(video.vq8)().size() >= 512 * 2 * 16, "vq8 too short for following writes" );

	for ( int i = 0; i < four; i++ )
	{
		size_t ia = file.at( input++ ) * 4;
		size_t ib = file.at( input++ ) * 4;
		static_assert( decltype(video.vq2)().size() >= 255 * 4 + 2 * 2, "vq2 too short for following reads" );

		for ( int j = 0; j < 2; j++ )
		{
			// "VQ2TO4"
			video.vq4.at( ic + 0 ) = video.vq2.at( ia );
			video.vq8.at( id + 0 ) = video.vq2.at( ia );
			video.vq8.at( id + 1 ) = video.vq2.at( ia );
			video.vq4.at( ic + 1 ) = video.vq2.at( ia + 1 );
			video.vq8.at( id + 2 ) = video.vq2.at( ia + 1 );
			video.vq8.at( id + 3 ) = video.vq2.at( ia + 1 );
			video.vq4.at( ic + 2 ) = video.vq2.at( ib );
			video.vq8.at( id + 4 ) = video.vq2.at( ib );
			video.vq8.at( id + 5 ) = video.vq2.at( ib );
			video.vq4.at( ic + 3 ) = video.vq2.at( ib + 1 );
			video.vq8.at( id + 6 ) = video.vq2.at( ib + 1 );
			video.vq8.at( id + 7 ) = video.vq2.at( ib + 1 );
			video.vq8.at( id + 8 ) = video.vq2.at( ia );
			video.vq8.at( id + 9 ) = video.vq2.at( ia );
			video.vq8.at( id + 10 ) = video.vq2.at( ia + 1 );
			video.vq8.at( id + 11 ) = video.vq2.at( ia + 1 );
			video.vq8.at( id + 12 ) = video.vq2.at( ib );
			video.vq8.at( id + 13 ) = video.vq2.at( ib );
			video.vq8.at( id + 14 ) = video.vq2.at( ib + 1 );
			video.vq8.at( id + 15 ) = video.vq2.at( ib + 1 );
			ia += 2;
			ib += 2;
			ic += 4;
			id += 16;
		}
	}

	ASSERT_EQ( input, totalReadSize );
}

static void quad( roqVideo_t &video, int x, int y )
{
	video.qStatus.push_back( y * video.samplesPerLine + x * SAMPLES_PER_PIXEL );
}

static void recurseQuad( roqVideo_t &video, int startX, int startY )
{
	quad( video, startX, startY ); // 8x8
	quad( video, startX, startY ); // 4x4
	quad( video, startX + 4, startY ); // 4x4
	quad( video, startX, startY + 4 ); // 4x4
	quad( video, startX + 4, startY + 4 ); // 4x4
	quad( video, startX + 8, startY ); // 8x8
	quad( video, startX + 8, startY ); // 4x4
	quad( video, startX + 12, startY ); // 4x4
	quad( video, startX + 8, startY + 4 ); // 4x4
	quad( video, startX + 12, startY + 4 ); // 4x4
	quad( video, startX, startY + 8 ); // 8x8
	quad( video, startX, startY + 8 ); // 4x4
	quad( video, startX + 4, startY + 8 ); // 4x4
	quad( video, startX, startY + 12 ); // 4x4
	quad( video, startX + 4, startY + 12 ); // 4x4
	quad( video, startX + 8, startY + 8 ); // 8x8
	quad( video, startX + 8, startY + 8 ); // 4x4
	quad( video, startX + 12, startY + 8 ); // 4x4
	quad( video, startX + 8, startY + 12 ); // 4x4
	quad( video, startX + 12, startY + 12 ); // 4x4
}

static void setupQuad( roqVideo_t &video )
{
	unsigned numQuadCels = ( video.xsize / 16 ) * ( video.ysize / 16 ) * 20;
	video.qStatus.reserve( numQuadCels );

	for ( int y = 0; y < ( int ) video.ysize; y += 16 )
	{
		for ( int x = 0; x < ( int ) video.xsize; x += 16 )
		{
			recurseQuad( video, x, y );
		}
	}

	ASSERT_EQ( numQuadCels, video.qStatus.size() );
}

static void readQuadInfo( roqVideo_t &video, const std::vector<byte> &file )
{
	video.xsize = file.at( 0 ) + file.at( 1 ) * 256;
	video.ysize = file.at( 2 ) + file.at( 3 ) * 256;
	logger.Verbose( "%s dimensions: height %s width %s",
		video.fileName, video.ysize, video.xsize );

	if ( video.xsize <= 0 || video.ysize <= 0 ||
		video.xsize > MAX_DIMENSION || video.ysize > MAX_DIMENSION ||
		video.xsize & 15 || video.ysize & 15 )
	{
		throw BadROQException( "invalid dimensions" );
	}

	video.samplesPerLine = video.xsize * SAMPLES_PER_PIXEL;

	video.activeBuf.resize( video.samplesPerLine * video.ysize );
	video.doneBuf.resize( video.samplesPerLine * video.ysize );
}

static void RoQPrepMcomp( roqVideo_t &video, int xoff, int yoff )
{
	int i = video.samplesPerLine;
	int j = SAMPLES_PER_PIXEL;

	for ( int y = 0; y < 16; y++ )
	{
		int temp2 = ( y + yoff - 8 ) * i;

		for ( int x = 0; x < 16; x++ )
		{
			int temp = ( x + xoff - 8 ) * j;
			video.mcomp.at( ( x * 16 ) + y ) = - ( temp2 + temp );
		}
	}
}

// Global initialization
static void initRoQ()
{
	ROQ_GenYUVTables();
}

static void RoQ_init( roqVideo_t &video )
{
	video.startTime = backEnd.refdef.time;
	video.numQuads = -1;
	video.status = roqStatus_t::PLAYING;
}

// TODO: make it loop with exact timing
static void RoQReset( roqVideo_t &video )
{
	logger.Debug( "restarting video %s", video.fileName );
	video.RoQPlayed = 0;
	video.dirty = false;
	Read( video, 8 ); // skip file header
	RoQ_init( video );

	// zero buffers
	ResetStruct( video.mcomp );
	ResetStruct( video.vq2 );
	ResetStruct( video.vq4 );
	ResetStruct( video.vq8 );
	std::fill( video.activeBuf.begin(), video.activeBuf.end(), 0 );
	std::fill( video.doneBuf.begin(), video.doneBuf.end(), 0 );
}

static void RoQInterrupt( roqVideo_t &video )
{
	const std::vector<byte> header = Read( video, 8 ); // Per-command header

	// Command number
	unsigned int roq_id = header.at( 0 ) + header.at( 1 ) * 256;

	// Number of bytes of data for this command
	unsigned int RoQFrameSize =
		header.at( 2 ) + header.at( 3 ) * 256 + header.at( 4 ) * 65536;
	if ( RoQFrameSize > MAX_FRAME_DATA_SIZE || !RoQFrameSize )
	{
		throw BadROQException( "bad frame data size" );
	}

	int roq_flags = header.at( 6 ) + header.at( 7 ) * 256;
	int roqF0 = ( signed char ) header.at( 7 );
	int roqF1 = ( signed char ) header.at( 6 );

	const std::vector<byte> file = Read( video, RoQFrameSize );

	switch ( roq_id )
	{
		case ROQ_QUAD_VQ:
			if ( video.numQuads < 0 )
			{
				throw BadROQException( "ROQ_QUAD_INFO must appear before first frame" );
			}

			RoQPrepMcomp( video, roqF0, roqF1 );
			blitVQQuad32fs( video, file );
			std::swap( video.doneBuf, video.activeBuf );
			video.numQuads++;
			video.dirty = true;
			break;

		case ROQ_CODEBOOK:
			decodeCodeBook( video, file, ( unsigned short ) roq_flags );
			break;

		case ROQ_QUAD_INFO:
			if ( video.numQuads != -1 )
			{
				throw BadROQException( "ROQ_QUAD_INFO may not appear twice" );
			}

			if ( video.xsize == 0 ) // first playthrough
			{
				readQuadInfo( video, file );
				setupQuad( video );
			}

			video.numQuads = 0;
			break;

		case ROQ_PACKET:
			throw BadROQException( "ROQ_PACKET not implemented" );

		case ROQ_QUAD_HANG:
			throw BadROQException( "ROQ_QUAD_HANG not implemented" );

		case ZA_SOUND_MONO:
		case ZA_SOUND_STEREO:
			logger.Notice( "sound not allowed in in-game video" );
			break;

		case ROQ_QUAD_JPEG:
			break; // Also does nothing in ioq3. Supposedly intended as keyframe

		case 0x1084:
			video.status = roqStatus_t::LOOPED;
			return;

		default:
			throw BadROQException( "unknown roq_id command" );
	}

	if ( video.RoQPlayed >= video.entireFile.size() )
	{
		video.status = roqStatus_t::LOOPED;
	}
}

/*
==================
CIN_RunCinematic

Fetch and decompress the pending frame
==================
*/
bool CIN_RunCinematic( videoHandle_t handle )
{
	if ( !cl_inGameVideo.Get() )
	{
		return false;
	}

	roqVideo_t &video = cinTable.at( handle );

	if ( video.status <= roqStatus_t::DEAD )
	{
		return false;
	}

	int desiredFrames = static_cast<int>(
		static_cast<double>( backEnd.refdef.time - video.startTime ) * ( 0.001 * ROQ_FPS ) );

	if ( std::abs( desiredFrames - video.numQuads ) > maxFrameSkip.Get() )
	{
		logger.Debug( "unpaused video %s", video.fileName );
		desiredFrames = video.numQuads + 1;
		video.startTime =
			backEnd.refdef.time - static_cast<int>( desiredFrames * ( 1000.0f / ROQ_FPS ) );
	}

	desiredFrames = std::max( 1, desiredFrames );

	while ( video.numQuads < desiredFrames && video.status == roqStatus_t::PLAYING )
	{
		try
		{
			RoQInterrupt( video );
		}
		catch ( const BadROQException &error )
		{
			logger.Warn( "error during ROQ video playback of %s: %s", video.fileName, error.what() );
			StopVideo( video );
			return false;
		}
	}

	if ( video.status == roqStatus_t::LOOPED )
	{
		if ( video.numQuads < 1 )
		{
			logger.Warn( "video %s has no frames", video.fileName );
			StopVideo( video );
		}
		else
		{
			RoQReset( video );
		}
	}

	return video.status == roqStatus_t::PLAYING;
}

static void Load( roqVideo_t &video )
{
	const std::vector<byte> file = Read( video, 8 ); // File header

	unsigned short RoQID = ( unsigned short )( file.at( 0 ) ) + ( unsigned short )( file.at( 1 ) ) * 256;
	if ( RoQID != 0x1084 )
	{
		throw BadROQException( "file format appears not to be ROQ" );
	}

	int roqFPS = file.at( 6 ) + file.at( 7 ) * 256;
	if ( roqFPS != 0 && roqFPS != ROQ_FPS )
	{
		logger.Warn( "%s: FPS must be 30", video.fileName );
	}

	RoQ_init( video );
	logger.Verbose( "roq video '%s' started", video.fileName );
}

/*
==================
CIN_PlayCinematic

Initialize the video and get a handle
==================
*/
videoHandle_t CIN_PlayCinematic( std::string name )
{
	if ( name.find_first_of( "/\\" ) == name.npos )
	{
		name = "video/" + name;
	}

	if ( !Str::IsISuffix( ".roq", name ) )
	{
		name += ".roq";
	}

	for ( size_t i = 0; i < cinTable.size(); i++)
	{
		if ( Str::IsIEqual( cinTable.at( i ).fileName, name ) )
		{
			// failures are cached too
			return static_cast<videoHandle_t>( i );
		}
	}

	if ( cinTable.size() >= MAX_IN_GAME_VIDEOS )
	{
		logger.Warn( "exceeded limit of %d videos in level", MAX_IN_GAME_VIDEOS );
		return -1;
	}

	auto handle = static_cast<videoHandle_t>( cinTable.size() );

	if ( handle == 0 )
	{
		initRoQ();
	}

	cinTable.emplace_back();
	roqVideo_t &video = cinTable.back();
	video.fileName = std::move( name );

	if ( !cl_inGameVideo.Get() )
	{
		video.status = roqStatus_t::DEAD;
		return handle;
	}

	try
	{
		video.entireFile = FS::PakPath::ReadFile( video.fileName );
	}
	catch ( const std::system_error &error )
	{
		logger.Warn( "couldn't open video %s: %s", video.fileName, error.what() );
		StopVideo( video );
		return handle;
	}

	try
	{
		Load( video );
	}
	catch ( const BadROQException &error )
	{
		logger.Warn( "couldn't load video %s: %s", video.fileName, error.what() );
		StopVideo( video );
		return handle;
	}

	logger.Verbose( "successfully opened video %s", video.fileName );
	return handle;
}

void CIN_UploadCinematic( videoHandle_t handle )
{
	const roqVideo_t &video = cinTable.at( handle );

	if ( video.status != roqStatus_t::PLAYING ||
		video.xsize == 0 || video.ysize == 0 ||
		video.doneBuf.size() != video.xsize * video.ysize * SAMPLES_PER_PIXEL )
	{
		Sys::Drop( "CIN_UploadCinematic called with video in bad state" );
	}

	RE_UploadCinematic( video.xsize, video.ysize, video.doneBuf.data(), handle, video.dirty );
}
