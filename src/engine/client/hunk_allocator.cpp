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

#include "common/Common.h"

#include "engine/client/client.h"
#include "engine/qcommon/qcommon.h"
#include "framework/CvarSystem.h"

/*
==============================================================================

Goals:
        reproducible without history effects -- no out of memory errors on weird map to map changes
        allow restarting of the client without fragmentation
        minimize total pages in use at run time
        minimize total pages needed during load time

  Single block of memory with stack allocators coming from both ends towards the middle.

  One side is designated the temporary memory allocator.

  Temporary memory can be allocated and freed in any order.

  A highwater mark is kept of the most in use at any time.

  When there is no temporary memory allocated, the permanent and temp sides
  can be switched, allowing the already touched temp memory to be used for
  permanent storage.

  Temp memory must never be allocated on two ends at once, or fragmentation
  could occur.

  If we have any in-use temp memory, additional temp allocations must come from
  that side.

  If not, we can choose to make either side the new temp side and push future
  permanent allocations to the other side.  Permanent allocations should be
  kept on the side that has the current greatest wasted highwater mark.

==============================================================================
*/

cvar_t *com_hunkused; // Ridah
static Cvar::Range<Cvar::Cvar<int>> com_hunkMegs(
	"com_hunkMegs", "megabytes of memory to allocate for renderer", Cvar::NONE, 512, 256, 2047);

static const int HUNK_MAGIC      = 0x89537892;
static const int HUNK_FREE_MAGIC = 0x89537893;

struct hunkHeader_t
{
	int magic;
	int size;
};

struct hunkUsed_t
{
	int permanent;
	int temp;
	int tempHighwater;
};

static hunkUsed_t  hunk_low, hunk_high;
static hunkUsed_t  *hunk_permanent, *hunk_temp;

static byte        *s_hunkData = nullptr;
static int         s_hunkTotal;

/*
=================
Com_Meminfo_f
=================
*/
static void Com_Meminfo_f()
{
	Log::Notice( "%9i bytes (%6.2f MB) total hunk", s_hunkTotal, s_hunkTotal / Square( 1024.f ) );
	Log::Notice( "" );
	Log::Notice( "%9i bytes (%6.2f MB) low permanent", hunk_low.permanent, hunk_low.permanent / Square( 1024.f ) );

	if ( hunk_low.temp != hunk_low.permanent )
	{
		Log::Notice( "%9i bytes (%6.2f MB) low temp", hunk_low.temp, hunk_low.temp / Square( 1024.f ) );
	}

	Log::Notice( "%9i bytes (%6.2f MB) low tempHighwater", hunk_low.tempHighwater, hunk_low.tempHighwater / Square( 1024.f ) );
	Log::Notice( "" );
	Log::Notice( "%9i bytes (%6.2f MB) high permanent", hunk_high.permanent, hunk_high.permanent / Square( 1024.f ) );

	if ( hunk_high.temp != hunk_high.permanent )
	{
		Log::Notice( "%9i bytes (%6.2f MB) high temp", hunk_high.temp, hunk_high.temp / Square( 1024.f ) );
	}

	Log::Notice( "%9i bytes (%6.2f MB) high tempHighwater", hunk_high.tempHighwater, hunk_high.tempHighwater / Square( 1024.f ) );
	Log::Notice( "" );
	Log::Notice( "%9i bytes (%6.2f MB) total hunk in use", hunk_low.permanent + hunk_high.permanent,
	            ( hunk_low.permanent + hunk_high.permanent ) / Square( 1024.f ) );
	int unused = 0;

	if ( hunk_low.tempHighwater > hunk_low.permanent )
	{
		unused += hunk_low.tempHighwater - hunk_low.permanent;
	}

	if ( hunk_high.tempHighwater > hunk_high.permanent )
	{
		unused += hunk_high.tempHighwater - hunk_high.permanent;
	}

	Log::Notice( "%9i bytes (%6.2f MB) unused highwater", unused, unused / Square( 1024.f ) );
}

/*
=================
Hunk_Init

This should be called once, on application startup.
=================
*/
void Hunk_Init()
{
	// allocate the stack based hunk allocator
	Cvar::AddFlags(com_hunkMegs.Name(), Cvar::INIT);
	s_hunkTotal = com_hunkMegs.Get() * 1024 * 1024;

	// cacheline aligned
	s_hunkData = ( byte * ) Com_Allocate_Aligned( 64, s_hunkTotal );

	if ( !s_hunkData )
	{
		Sys::Error( "Hunk data failed to allocate %iMB", com_hunkMegs.Get() );
	}

	Hunk_Clear();

	Cmd_AddCommand( "meminfo", Com_Meminfo_f );

	com_hunkused = Cvar_Get( "com_hunkused", "0", 0 );
}

void Hunk_Clear()
{
	hunk_low.permanent = 0;
	hunk_low.temp = 0;
	hunk_low.tempHighwater = 0;

	hunk_high.permanent = 0;
	hunk_high.temp = 0;
	hunk_high.tempHighwater = 0;

	hunk_permanent = &hunk_low;
	hunk_temp = &hunk_high;

	Cvar_Set( "com_hunkused", va( "%i", hunk_low.permanent + hunk_high.permanent ) );

	Log::Debug( "Hunk_Clear: reset the hunk ok" );
}

void Hunk_Shutdown()
{
	Com_Free_Aligned( s_hunkData );
}

static void Hunk_SwapBanks()
{
	hunkUsed_t *swap;

	// can't swap banks if there is any temp already allocated
	if ( hunk_temp->temp != hunk_temp->permanent )
	{
		return;
	}

	// if we have a larger highwater mark on this side, start making
	// our permanent allocations here and use the other side for temp
	if ( hunk_temp->tempHighwater - hunk_temp->permanent > hunk_permanent->tempHighwater - hunk_permanent->permanent )
	{
		swap = hunk_temp;
		hunk_temp = hunk_permanent;
		hunk_permanent = swap;
	}
}

/*
=================
Hunk_Alloc

Allocate permanent (until the hunk is cleared) memory
=================
*/
void           *Hunk_Alloc( int size, ha_pref)
{
	void *buf;

	if ( s_hunkData == nullptr )
	{
		Sys::Error( "Hunk_Alloc: Hunk memory system not initialized" );
	}

	Hunk_SwapBanks();

	// round to cacheline
	size = ( size + 31 ) & ~31;

	if ( hunk_low.temp + hunk_high.temp + size > s_hunkTotal )
	{
		Sys::Drop( "Hunk_Alloc failed on %i", size );
	}

	if ( hunk_permanent == &hunk_low )
	{
		buf = ( void * )( s_hunkData + hunk_permanent->permanent );
		hunk_permanent->permanent += size;
	}
	else
	{
		hunk_permanent->permanent += size;
		buf = ( void * )( s_hunkData + s_hunkTotal - hunk_permanent->permanent );
	}

	hunk_permanent->temp = hunk_permanent->permanent;

	memset( buf, 0, size );

	// Ridah, update the com_hunkused cvar in increments, so we don't update it too often, since this cvar call isn't very efficent
	if ( ( hunk_low.permanent + hunk_high.permanent ) > com_hunkused->integer + 2500 )
	{
		Cvar_Set( "com_hunkused", va( "%i", hunk_low.permanent + hunk_high.permanent ) );
	}

	return buf;
}

/*
=================
Hunk_AllocateTempMemory

This is used by the file loading system.
Multiple files can be loaded in temporary memory.
When the files-in-use count reaches zero, all temp memory will be deleted
=================
*/
void           *Hunk_AllocateTempMemory( int size )
{
	void         *buf;
	hunkHeader_t *hdr;

	Hunk_SwapBanks();

	size = PAD( size, sizeof( intptr_t ) ) + sizeof( hunkHeader_t );

	if ( hunk_temp->temp + hunk_permanent->permanent + size > s_hunkTotal )
	{
		Sys::Drop( "Hunk_AllocateTempMemory: failed on %i", size );
	}

	if ( hunk_temp == &hunk_low )
	{
		buf = ( void * )( s_hunkData + hunk_temp->temp );
		hunk_temp->temp += size;
	}
	else
	{
		hunk_temp->temp += size;
		buf = ( void * )( s_hunkData + s_hunkTotal - hunk_temp->temp );
	}

	if ( hunk_temp->temp > hunk_temp->tempHighwater )
	{
		hunk_temp->tempHighwater = hunk_temp->temp;
	}

	hdr = ( hunkHeader_t * ) buf;
	buf = ( void * )( hdr + 1 );

	hdr->magic = HUNK_MAGIC;
	hdr->size = size;

	// don't bother clearing, because we are going to load a file over it
	return buf;
}

/*
==================
Hunk_FreeTempMemory
==================
*/
void Hunk_FreeTempMemory( void *buf )
{
	hunkHeader_t *hdr;

	hdr = ( ( hunkHeader_t * ) buf ) - 1;

	if ( hdr->magic != (int) HUNK_MAGIC )
	{
		if ( Sys::IsDebuggerAttached() ) BREAKPOINT();

		Sys::Error( "Hunk_FreeTempMemory: bad magic" );
	}

	hdr->magic = HUNK_FREE_MAGIC;

	// this only works if the files are freed in stack order,
	// otherwise the memory will stay around until Hunk_Clear
	if ( hunk_temp == &hunk_low )
	{
		if ( hdr == ( void * )( s_hunkData + hunk_temp->temp - hdr->size ) )
		{
			hunk_temp->temp -= hdr->size;
		}
		else
		{
			Log::Notice( "Hunk_FreeTempMemory: not the final block" );
		}
	}
	else
	{
		if ( hdr == ( void * )( s_hunkData + s_hunkTotal - hunk_temp->temp ) )
		{
			hunk_temp->temp -= hdr->size;
		}
		else
		{
			Log::Notice( "Hunk_FreeTempMemory: not the final block" );
		}
	}
}
