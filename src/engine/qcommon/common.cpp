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

// common.c -- misc functions used in client and server

#include "qcommon/q_shared.h"
#include "qcommon/sys.h"
#include "q_unicode.h"
#include "qcommon.h"

#include "common/Defs.h"

#include "client/keys.h"
#include "framework/Application.h"
#include "framework/BaseCommands.h"
#include "framework/CommandSystem.h"
#include "framework/CvarSystem.h"
#include "framework/LogSystem.h"
#include "framework/System.h"
#include "sys/sys_events.h"
#include <common/FileSystem.h>

cvar_t *com_speeds;
cvar_t *com_timescale;
cvar_t *com_dropsim; // 0.0 to 1.0, simulated packet drops

Cvar::Cvar<bool> cvar_demo_timedemo(
    "demo.timedemo",
    "Whether to show timing statistics at the end of a demo",
    Cvar::CHEAT | Cvar::TEMPORARY,
    false
);
cvar_t *com_sv_running;
cvar_t *com_cl_running;
cvar_t *com_version;

cvar_t *com_unfocused;
cvar_t *com_minimized;


// com_speeds times
int      time_game;
int      time_frontend; // renderer frontend time
int      time_backend; // renderer backend time

int      com_frameTime;
int      com_frameMsec;
int      com_frameNumber;

bool com_fullyInitialized;

void     Com_WriteConfig_f();
void     Com_WriteBindings_f();

//============================================================================

// *INDENT-OFF*
//bani - moved
void CL_ShutdownCGame();

//============================================================================

void Info_Print( const char *s )
{
	char key[ 8192 ];
	char value[ 8192 ];
	char *o;
	int  l;

	if ( *s == '\\' )
	{
		s++;
	}

	while ( *s )
	{
		o = key;

		while ( *s && *s != '\\' )
		{
			*o++ = *s++;
		}

		l = o - key;

		if ( l < 20 )
		{
			memset( o, ' ', 20 - l );
			key[ 20 ] = 0;
		}
		else
		{
			*o = 0;
		}

		Log::defaultLogger.WithoutSuppression().Notice( key );

		if ( !*s )
		{
			Log::defaultLogger.WithoutSuppression().Notice( "MISSING VALUE" );
			return;
		}

		o = value;
		s++;

		while ( *s && *s != '\\' )
		{
			*o++ = *s++;
		}

		*o = 0;

		if ( *s )
		{
			s++;
		}

		Log::defaultLogger.WithoutSuppression().Notice( value );
	}
}

/*
==============================================================================
Global common state
==============================================================================
*/

void SetCheatMode(bool allowed)
{
	Cvar::SetCheatsAllowed(allowed);
}

//The server gives the sv_cheats cvar to the client, on 'off' it prevents the user from changing Cvar::CHEAT cvars
Cvar::Callback<Cvar::Cvar<bool>> cvar_cheats("sv_cheats", "can cheats be used in the current game", Cvar::SYSTEMINFO | Cvar::ROM, true, SetCheatMode);

bool Com_AreCheatsAllowed()
{
	return cvar_cheats.Get();
}

bool Com_IsClient()
{
    auto config = Application::GetTraits();
    return config.isClient || config.isTTYClient;
}

bool Com_IsDedicatedServer()
{
    return !Com_IsClient();
}

bool Com_ServerRunning()
{
	return com_sv_running->integer;
}

/*
=================
Com_Allocate_Aligned

Aligned Memory Allocations for Posix and Win32
=================
*/
MALLOC_LIKE void *Com_Allocate_Aligned( size_t alignment, size_t size )
{
#ifdef _WIN32
	return _aligned_malloc( size, alignment );
#else
	void *ptr;
	if( !posix_memalign( &ptr, alignment, size ) )
		return ptr;
	else
		return nullptr;
#endif
}

/*
=================
Com_Free_Aligned

Free Aligned Memory for Posix and Win32
=================
*/
void Com_Free_Aligned( void *ptr )
{
#ifdef _WIN32
	_aligned_free( ptr );
#else
	free( ptr );
#endif
}


/*
===================================================================

EVENTS

===================================================================
*/

/*
========================================================================
EVENT LOOP
========================================================================
*/

static const int MAX_QUEUED_EVENTS  = 1024;
static const int MASK_QUEUED_EVENTS = ( MAX_QUEUED_EVENTS - 1 );

static std::unique_ptr<Sys::EventBase> eventQueue[ MAX_QUEUED_EVENTS ];
static int        eventHead = 0;
static int        eventTail = 0;
static byte       sys_packetReceived[ MAX_MSGLEN ];

/*
================
Com_QueueEvent
================
*/
void Com_QueueEvent( std::unique_ptr<Sys::EventBase> event )
{
	if ( eventHead - eventTail >= MAX_QUEUED_EVENTS )
	{
		Log::Notice( "Com_QueueEvent: overflow" );
		eventTail++;
	}

	eventQueue[ eventHead & MASK_QUEUED_EVENTS ] = std::move( event );
	eventHead++;
}

/*
================
Com_GetEvent

Returns nullptr if there are no more events.
================
*/
std::unique_ptr<Sys::EventBase> Com_GetEvent()
{
	// return if we have data
	if ( eventHead > eventTail )
	{
		eventTail++;
		return std::move(eventQueue[( eventTail - 1 ) & MASK_QUEUED_EVENTS ]);
	}

	// check for tty/curses console commands
	if ( char* s = CON_Input() )
	{
		Com_QueueEvent( Util::make_unique<Sys::ConsoleInputEvent>( s ) );
	}

	// check for network packets
	msg_t netmsg;
	netadr_t adr;
	MSG_Init( &netmsg, sys_packetReceived, sizeof( sys_packetReceived ) );
	adr.type = netadrtype_t::NA_UNSPEC;

	if ( Sys_GetPacket( &adr, &netmsg ) )
	{
		Com_QueueEvent( Util::make_unique<Sys::PacketEvent>(
			adr, &netmsg.data[ netmsg.readcount ], netmsg.cursize - netmsg.readcount ) );
	}

	// return if we have data
	if ( eventHead > eventTail )
	{
		eventTail++;
		return std::move(eventQueue[( eventTail - 1 ) & MASK_QUEUED_EVENTS ]);
	}

	return nullptr;
}

/*
=================
Com_RunAndTimeServerPacket
=================
*/
static void Com_RunAndTimeServerPacket( const netadr_t *evFrom, msg_t *buf )
{
	int t1, t2, msec;

	t1 = 0;

	if ( com_speeds->integer )
	{
		t1 = Sys::Milliseconds();
	}

	SV_PacketEvent( *evFrom, buf );

	if ( com_speeds->integer )
	{
		t2 = Sys::Milliseconds();
		msec = t2 - t1;

		if ( com_speeds->integer == 3 )
		{
			Log::Notice( "SV_PacketEvent time: %i", msec );
		}
	}
}

static void HandleConsoleInputEvent(const Sys::ConsoleInputEvent& event)
{
	if (Str::IsPrefix("/", event.text) || Str::IsPrefix("\\", event.text)) {
		//make sure, explicit commands are not getting handled with com_consoleCommand
		Cmd::BufferCommandTextAfter(Str::StringRef(event.text).substr(1), true);
	} else {
		/*
		 * when there was no command prefix, execute the command prefixed by com_consoleCommand
		 * if the cvar is empty, it will interpret the text as command directly
		 * (and will so for BUILD_SERVER)
		 *
		 * the additional space gets trimmed by the parser
		 */
		Cmd::BufferCommandTextAfter(Str::Format("%s %s", com_consoleCommand.Get(), event.text));
	}
}

static void HandlePacketEvent(const Sys::PacketEvent& event)
{
	// this cvar allows simulation of connections that
	// drop a lot of packets.  Note that loopback connections
	// don't go through here at all.
	if ( com_dropsim->value > 0 )
	{
		static int seed;

		if ( Q_random( &seed ) < com_dropsim->value )
		{
			return; // drop this packet
		}
	}
	msg_t buf;
	byte bufData[ MAX_MSGLEN ];
	MSG_Init( &buf, bufData, sizeof( bufData ) );

	// we must copy the contents of the message out, because
	// the event buffers are only large enough to hold the
	// exact payload, but channel messages need to be large
	// enough to hold fragment reassembly
	if ( event.data.size() > static_cast<size_t>(buf.maxsize) )
	{
		Log::Notice( "Com_EventLoop: oversize packet" );
		return;
	}

	buf.cursize = event.data.size();
	memcpy( buf.data, event.data.data(), buf.cursize );

	if ( com_sv_running->integer )
	{
		Com_RunAndTimeServerPacket( &event.adr, &buf );
	}
	else
	{
		CL_PacketEvent( event.adr, &buf );
	}
}

/*
=================
Com_EventLoop

=================
*/

void Com_EventLoop()
{
	netadr_t   evFrom;
	byte       bufData[ MAX_MSGLEN ];
	msg_t      buf;

	int        mouseX = 0, mouseY = 0;
	bool       hadMouseEvent = false;

	MSG_Init( &buf, bufData, sizeof( bufData ) );

	while (true)
	{
		auto ev = Com_GetEvent();

		// if no more events are available
		if ( !ev )
		{
			if ( hadMouseEvent ){
				CL_MouseEvent( mouseX, mouseY );
			}

			// manually send packet events for the loopback channel
			while ( NET_GetLoopPacket( netsrc_t::NS_CLIENT, &evFrom, &buf ) )
			{
				CL_PacketEvent( evFrom, &buf );
			}

			while ( NET_GetLoopPacket( netsrc_t::NS_SERVER, &evFrom, &buf ) )
			{
				// if the server just shut down, flush the events
				if ( com_sv_running->integer )
				{
					Com_RunAndTimeServerPacket( &evFrom, &buf );
				}
			}

			return;
		}

		switch ( ev->type )
		{
			default:
				Sys::Error( "Com_EventLoop: bad event type %s", Util::enum_str(ev->type) );

			case sysEventType_t::SE_CONSOLE_KEY:
				CL_ConsoleKeyEvent();
				break;

			case sysEventType_t::SE_KEY:
			{
				auto& keyEvent = ev->Cast<Sys::KeyEvent>();
				if ( keyEvent.down )
				{
					if ( keyEvent.repeat )
					{
						CL_KeyRepeatEvent( keyEvent.key1 );
						CL_KeyRepeatEvent( keyEvent.key2 );
					}
					else
					{
						CL_KeyDownEvent( keyEvent.key1, keyEvent.key2, keyEvent.time );
					}
				}
				else
				{
					CL_KeyUpEvent( keyEvent.key1, keyEvent.time );
					CL_KeyUpEvent( keyEvent.key2, keyEvent.time );
				}
				break;
			}

			case sysEventType_t::SE_CHAR:
				CL_CharEvent( ev->Cast<Sys::CharEvent>().ch );
				break;

			case sysEventType_t::SE_MOUSE:
			{
				hadMouseEvent = true;
				auto& mouseEvent = ev->Cast<Sys::MouseEvent>();
				mouseX += mouseEvent.dx;
				mouseY += mouseEvent.dy;
				break;
			}

			case sysEventType_t::SE_MOUSE_POS:
			{
				auto& mouseEvent = ev->Cast<Sys::MousePosEvent>();
				CL_MousePosEvent( mouseEvent.x, mouseEvent.y );
				break;
			}

			case sysEventType_t::SE_FOCUS:
				CL_FocusEvent( ev->Cast<Sys::FocusEvent>().focus );
				break;

			case sysEventType_t::SE_JOYSTICK_AXIS:
			{
				auto& joystickEvent = ev->Cast<Sys::JoystickEvent>();
				CL_JoystickEvent( joystickEvent.axis, joystickEvent.value );
				break;
			}

			case sysEventType_t::SE_CONSOLE:
				HandleConsoleInputEvent(ev->Cast<Sys::ConsoleInputEvent>());
				break;

			case sysEventType_t::SE_PACKET:
				HandlePacketEvent(ev->Cast<Sys::PacketEvent>());
				break;
		}
	}
}

/*
================
Com_Milliseconds

Can be used for profiling, but will be journaled accurately
================
*/
int Com_Milliseconds()
{
    return Sys::Milliseconds();
}

//============================================================================

void Com_In_Restart_f()
{
	IN_Restart();
}

/*
=================
Com_Init

This does initializations that only occur once in the lifetime of the program.
Stuff that is initialized e.g. each time the cgame restarts is done elsewhere.
=================
*/
void Com_Init()
{
	char              *s;
	int               qport;

	Trans_LoadDefaultLanguage();

	// if any archived cvars are modified after this, we will trigger a writing
	// of the config file
	cvar_modifiedFlags &= ~CVAR_ARCHIVE_BITS;

	//
	// init commands and vars
	//
	com_timescale = Cvar_Get( "timescale", "1", CVAR_CHEAT | CVAR_SYSTEMINFO );
	com_dropsim = Cvar_Get( "com_dropsim", "0", CVAR_CHEAT );
	com_speeds = Cvar_Get( "com_speeds", "0", 0 );

	com_sv_running = Cvar_Get( "sv_running", "0", CVAR_ROM );
	com_cl_running = Cvar_Get( "cl_running", "0", CVAR_ROM );

	com_unfocused = Cvar_Get( "com_unfocused", "0", CVAR_ROM );
	com_minimized = Cvar_Get( "com_minimized", "0", CVAR_ROM );

	Cmd_AddCommand( "writeconfig", Com_WriteConfig_f );
#ifdef BUILD_GRAPHICAL_CLIENT
	Cmd_AddCommand( "writebindings", Com_WriteBindings_f );
#endif

	s = va( "%s %s %s %s", Q3_VERSION, PLATFORM_STRING, XSTRING(ARCH_STRING), __DATE__ );
	com_version = Cvar_Get( "version", s, CVAR_ROM | CVAR_SERVERINFO );

	Cmd_AddCommand( "in_restart", Com_In_Restart_f );

	// Pick a qport value that is nice and random.
	// As machines get faster, Com_Milliseconds() can't be used
	// anymore, as it results in a smaller and smaller range of
	// qport values.
	Sys::GenRandomBytes( &qport, sizeof( int ) );
	Netchan_Init( qport & 0xffff );

	SV_Init();

	CL_Init();

	// set com_frameTime so that if a map is started on the
	// command line it will still be able to count on com_frameTime
	// being random enough for a serverid
	com_frameTime = Com_Milliseconds();

	NET_Init();

	com_fullyInitialized = true;
	Log::Notice( "--- Common Initialization Complete ---" );
}

//==================================================================

void Com_WriteConfigToFile( const char *filename, void (*writeConfig)( fileHandle_t ) )
{
	fileHandle_t f;
	char         tmp[ MAX_QPATH ];

	Com_sprintf( tmp, sizeof( tmp ), "config/%s", filename );

	f = FS_FOpenFileWriteViaTemporary( tmp );

	if ( !f )
	{
		Log::Notice( "Couldn't write %s.", filename );
		return;
	}

	FS_Printf( f, "// generated by " PRODUCT_NAME ", do not modify\n" );
	writeConfig( f );
	FS_FCloseFile( f );
}

/*
===============
Com_WriteConfiguration

Writes key bindings and archived cvars to config file if modified
===============
*/
void Com_WriteConfiguration()
{
	// if we are quiting without fully initializing, make sure
	// we don't write out anything
	if ( !com_fullyInitialized )
	{
		return;
	}

#if defined(BUILD_GRAPHICAL_CLIENT) || defined(BUILD_TTY_CLIENT)
	if ( cvar_modifiedFlags & CVAR_ARCHIVE_BITS )
	{
		cvar_modifiedFlags &= ~CVAR_ARCHIVE_BITS;

		Com_WriteConfigToFile( CONFIG_NAME, Cvar_WriteVariables );
	}
#endif

#ifdef BUILD_GRAPHICAL_CLIENT
	if ( bindingsModified )
	{
		bindingsModified = false;

		Com_WriteConfigToFile( KEYBINDINGS_NAME, Keyboard::WriteBindings );
	}
#endif
}

/*
===============
Com_WriteConfig_f

Write the config file to a specific name
===============
*/
void Com_WriteConfig_f()
{
	char filename[ MAX_QPATH ];

	if ( Cmd_Argc() != 2 )
	{
		Cmd_PrintUsage("<filename>", nullptr);
		return;
	}

	Q_strncpyz( filename, Cmd_Argv( 1 ), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".cfg" );
	Log::Notice( "Writing %s.", filename );
	Com_WriteConfigToFile( filename, Cvar_WriteVariables );
}

/*
===============
Com_WriteBindings_f

Write the key bindings file to a specific name
===============
*/
#ifdef BUILD_GRAPHICAL_CLIENT
void Com_WriteBindings_f()
{
	char filename[ MAX_QPATH ];

	if ( Cmd_Argc() != 2 )
	{
		Cmd_PrintUsage("<filename>", nullptr);
		return;
	}

	Q_strncpyz( filename, Cmd_Argv( 1 ), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".cfg" );
	Log::Notice( "Writing %s.", filename );
	Com_WriteConfigToFile( filename, Keyboard::WriteBindings );
}
#endif

/*
================
Com_ModifyMsec
================
*/
static Cvar::Cvar<int> fixedtime("common.fixedFrameTime", "in milliseconds, forces the frame time, 0 for no effect", Cvar::CHEAT, 0);

int Com_ModifyMsec( int msec )
{
	int clampTime;

	//
	// modify time for debugging values
	//
	if ( fixedtime.Get() )
	{
		msec = fixedtime.Get();
	}
	else if ( com_timescale->value )
	{
		msec *= com_timescale->value;
	}

	// don't let it scale below 1 msec
	if ( msec < 1 && com_timescale->value )
	{
		msec = 1;
	}

	if ( Com_IsDedicatedServer() )
	{
		// dedicated servers don't want to clamp for a much longer
		// period, because it would mess up all the client's views
		// of time.
		if ( msec > 500 && msec < 500000 )
		{
			Log::Warn( "Hitch: %i msec frame time", msec );
		}

		clampTime = 5000;
	}
	else if ( !Com_ServerRunning() )
	{
		// clients of remote servers do not want to clamp time, because
		// it would skew their view of the server's time temporarily
		clampTime = 5000;
	}
	else
	{
		// for local single player gaming
		// we may want to clamp the time to prevent players from
		// flying off edges when something hitches.
		clampTime = 200;
	}

	if ( msec > clampTime )
	{
		msec = clampTime;
	}

	return msec;
}

/*
=================
Com_Frame
=================
*/

//TODO 0 for the same value as common.maxFPS
static Cvar::Cvar<int> maxfps("common.framerate.max", "the max framerate, 0 for unlimited", Cvar::NONE, 125);
static Cvar::Cvar<int> maxfpsUnfocused("common.framerate.maxUnfocused", "the max framerate when the game is unfocused, 0 for unlimited", Cvar::NONE, 0);
static Cvar::Cvar<int> maxfpsMinimized("common.framerate.maxMinimized", "the max framerate when the game is minimized, 0 for unlimited", Cvar::NONE, 0);

static Cvar::Cvar<int> watchdogThreshold("common.watchdogTime", "seconds of server running without a map after which common.watchdogCmd is executed", Cvar::NONE, 60);
static Cvar::Cvar<std::string> watchdogCmd("common.watchdogCmd", "the command triggered by the watchdog, empty for /quit", Cvar::NONE, "");

static Cvar::Cvar<bool> showTraceStats("common.showTraceStats", "are physics traces stats printed each frame", Cvar::CHEAT, false);

void Com_Frame()
{
	int             msec, minMsec;
	static int      lastTime = 0;
	//int             key;

	int             timeBeforeFirstEvents;
	int             timeBeforeServer;
	int             timeBeforeEvents;
	int             timeBeforeClient;
	int             timeAfter;

	static int      watchdogTime = 0;
	static bool watchWarn = false;

	// bk001204 - init to zero.
	//  also:  might be clobbered by `longjmp' or `vfork'
	timeBeforeFirstEvents = 0;
	timeBeforeServer = 0;
	timeBeforeEvents = 0;
	timeBeforeClient = 0;
	timeAfter = 0;

	// Check to make sure we don't have any http data waiting
	// comment this out until I get things going better under win32
	// old net chan encryption key
	//key = 0x87243987;

	// write config file if anything changed
	Com_WriteConfiguration();

	//
	// main event loop
	//
	timeBeforeFirstEvents = Sys::Milliseconds();

	if ( timeBeforeFirstEvents < 0 || timeBeforeFirstEvents > 0x7F000000 )
	{
		Sys::Error( "Shutting down to prevent time overflow" );
	}

	// we may want to spin here if things are going too fast
	if ( !cvar_demo_timedemo.Get() )
	{
		if ( Com_IsDedicatedServer() )
		{
			minMsec = SV_FrameMsec();
		}
		else
		{
			int max;

			if ( com_minimized->integer && maxfpsMinimized.Get() != 0 )
			{
				max = maxfpsMinimized.Get();
			}
			else if ( com_unfocused->integer && maxfpsUnfocused.Get() != 0 )
			{
				max = maxfpsUnfocused.Get();
			}
			else
			{
				max = maxfps.Get();
			}

			// A positive maxfps caps the fps to the given number, with an implicit
			// cap at 333fps to avoid bugs. Above 333fps minMsec is less than 3.
			// At 1 or 2 minMsec, the game still runs but exhibits various issues
			// such as first-person weapon model flickering, or client having
			// connection issues with server.
			if ( max > 0 )
			{
				minMsec = std::max( 1000 / max, 3 );
			}
			// A zero maxfps unlocks fps but still cap it to 333 to avoid bugs.
			else if ( max == 0 )
			{
				minMsec = 3;
			}
			// A negative maxfps really unlocks fps (and bugs).
			else
			{
				minMsec = 1;
			}
		}
	}
	else
	{
		// It looks like demo played with cvar_demo_timedemo enabled
		// are not affected by the rendering bugs related to having
		// minMsec being lower than 3.
		minMsec = 0;
	}

	Com_EventLoop();

	// It must be called at least once.
	IN_Frame();

	com_frameTime = Sys::Milliseconds();

	// lastTime can be greater than com_frameTime on first frame.
	lastTime = std::min( lastTime, com_frameTime );

	msec = com_frameTime - lastTime;

	// For framerates up to 250fps, sleep until 1ms is remaining
	// use extra margin of 2ms when looking for an higher framerate.
	int margin = minMsec > 3 ? 1 : 2;

	while ( msec < minMsec )
	{
		// Never sleep more than 50ms.
		// Never sleep when there is only “margin” left or less remaining.
		int sleep = std::min( std::max( minMsec - msec - margin, 0 ), 50 );

		if ( sleep )
		{
			// Give cycles back to the OS.
			Sys::SleepFor( std::chrono::milliseconds( sleep ) );
		}

		Com_EventLoop();

		IN_Frame();

		com_frameTime = Sys::Milliseconds();

		msec = com_frameTime - lastTime;
	}

	IN_FrameEnd();

	Keyboard::BufferDeferredBinds();
	Cmd::ExecuteCommandBuffer();

	lastTime = com_frameTime;

	// mess with msec if needed
	com_frameMsec = msec;
	msec = Com_ModifyMsec( msec );

	//
	// server side
	//
	if ( com_speeds->integer )
	{
		timeBeforeServer = Sys::Milliseconds();
	}

	SV_Frame( msec );

	//
	// client system
	//
	// run event loop a second time to get server to client packets
	// without a frame of latency
	//
	if ( com_speeds->integer )
	{
		timeBeforeEvents = Sys::Milliseconds();
	}

	Com_EventLoop();
	Cmd::DelayFrame();
	Cmd::ExecuteCommandBuffer();

	if ( com_speeds->integer )
	{
		timeBeforeClient = Sys::Milliseconds();
	}

	CL_Frame( msec );

	if ( com_speeds->integer )
	{
		timeAfter = Sys::Milliseconds();
	}

	//
	// watchdog
	//
	if ( Com_IsDedicatedServer() && !Com_ServerRunning() && watchdogThreshold.Get() != 0 )
	{
		if ( watchdogTime == 0 )
		{
			watchdogTime = Sys::Milliseconds();
		}
		else
		{
			if ( !watchWarn && Sys::Milliseconds() - watchdogTime > ( watchdogThreshold.Get() - 4 ) * 1000 )
			{
				Log::Warn("watchdog will trigger in 4 seconds" );
				watchWarn = true;
			}
			else if ( Sys::Milliseconds() - watchdogTime > watchdogThreshold.Get() * 1000 )
			{
				Log::Notice( "Idle server with no map — triggering watchdog" );
				watchdogTime = 0;
				watchWarn = false;

				if ( watchdogCmd.Get().empty() )
				{
					Cmd::BufferCommandText("quit");
				}
				else
				{
					Cmd::BufferCommandText(watchdogCmd.Get());
				}
			}
		}
	}
	else if ( Com_IsDedicatedServer() && Com_ServerRunning() )
	{
		watchdogTime = 0;
		watchWarn = false;
	}

	//
	// report timing information
	//
	if ( com_speeds->integer )
	{
		int all, sv, sev, cev, cl;

		all = timeAfter - timeBeforeServer;
		sv = timeBeforeEvents - timeBeforeServer;
		sev = timeBeforeServer - timeBeforeFirstEvents;
		cev = timeBeforeClient - timeBeforeEvents;
		cl = timeAfter - timeBeforeClient;
		sv -= time_game;
		cl -= time_frontend + time_backend;

		Log::Notice( "frame:%i all:%3i sv:%3i sev:%3i cev:%3i cl:%3i gm:%3i rf:%3i bk:%3i",
		            com_frameNumber, all, sv, sev, cev, cl, time_game, time_frontend, time_backend );
	}

	//
	// trace optimization tracking
	//
	if ( showTraceStats.Get() )
	{
		extern int c_traces, c_brush_traces, c_patch_traces, c_trisoup_traces;
		extern int c_pointcontents;

		Log::Notice( "%4i traces  (%ib %ip %it) %4i points", c_traces, c_brush_traces, c_patch_traces, c_trisoup_traces,
		            c_pointcontents );
		c_traces = 0;
		c_brush_traces = 0;
		c_patch_traces = 0;
		c_trisoup_traces = 0;
		c_pointcontents = 0;
	}

	// old net chan encryption key
	//key = lastTime * 0x87243987;

	com_frameNumber++;
}
