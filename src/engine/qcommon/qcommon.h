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

// qcommon.h -- definitions common between client and server, but not game.or ref modules
#ifndef QCOMMON_H_
#define QCOMMON_H_

#include "common/cm/cm_public.h"
#include "cvar.h"
#include "common/Defs.h"
#include "net_types.h"

//============================================================================

//
// msg.c
//

void MSG_Init( msg_t *buf, byte *data, int length );
void MSG_InitOOB( msg_t *buf, byte *data, int length );
void MSG_Clear( msg_t *buf );
void MSG_WriteData( msg_t *buf, const void *data, int length );
void MSG_Bitstream( msg_t *buf );
void MSG_Uncompressed( msg_t *buf );

// TTimo
// copy a msg_t in case we need to store it as is for a bit
// (as I needed this to keep an msg_t from a static var for later use)
// sets data buffer as MSG_Init does prior to do the copy
void MSG_Copy( msg_t *buf, byte *data, int length, msg_t *src );

struct usercmd_t;

struct entityState_t;

void  MSG_WriteBits( msg_t *msg, int value, int bits );

void  MSG_WriteChar( msg_t *sb, int c );
void  MSG_WriteByte( msg_t *sb, int c );
void  MSG_WriteShort( msg_t *sb, int c );
void  MSG_WriteLong( msg_t *sb, int c );
void  MSG_WriteFloat( msg_t *sb, float f );
void  MSG_WriteString( msg_t *sb, const char *s );
void  MSG_WriteBigString( msg_t *sb, const char *s );

void  MSG_BeginReading( msg_t *sb );
void  MSG_BeginReadingOOB( msg_t *sb );
void  MSG_BeginReadingUncompressed( msg_t *msg );

int   MSG_ReadBits( msg_t *msg, int bits );

int   MSG_ReadByte( msg_t *sb );
int   MSG_ReadShort( msg_t *sb );
int   MSG_ReadLong( msg_t *sb );
float MSG_ReadFloat( msg_t *sb );
char  *MSG_ReadString( msg_t *sb );
char  *MSG_ReadBigString( msg_t *sb );
char  *MSG_ReadStringLine( msg_t *sb );
void  MSG_ReadData( msg_t *sb, void *buffer, int size );

void  MSG_WriteDeltaUsercmd( msg_t *msg, usercmd_t *from, usercmd_t *to );
void  MSG_ReadDeltaUsercmd( msg_t *msg, usercmd_t *from, usercmd_t *to );

void  MSG_WriteDeltaEntity( msg_t *msg, entityState_t *from, entityState_t *to, bool force );
void  MSG_ReadDeltaEntity( msg_t *msg, const entityState_t *from, entityState_t *to, int number );

void MSG_InitNetcodeTables(NetcodeTable playerStateTable, int playerStateSize);
void  MSG_WriteDeltaPlayerstate( msg_t *msg, const OpaquePlayerState *from, const OpaquePlayerState *to );
void  MSG_ReadDeltaPlayerstate( msg_t *msg, const OpaquePlayerState *from, OpaquePlayerState *to );

//============================================================================

/*
==============================================================

NET

==============================================================
*/

#define NET_ENABLEV4        0x01
#define NET_ENABLEV6        0x02
// if this flag is set, always attempt IPv6 connections instead of IPv4 if a v6 address is found.
#define NET_PRIOV6          0x04
// disables IPv6 multicast support if set.
#define NET_DISABLEMCAST    0x08

// right type anyway || ( DUAL && proto enabled && ( other proto disabled || appropriate IPv6 pref ) )
#define NET_IS_IPv4( type ) ( ( type ) == netadrtype_t::NA_IP  || ( ( type ) == netadrtype_t::NA_IP_DUAL && ( net_enabled->integer & NET_ENABLEV4 ) && ( ( ~net_enabled->integer & NET_ENABLEV6) || ( ~net_enabled->integer & NET_PRIOV6 ) ) ) )
#define NET_IS_IPv6( type ) ( ( type ) == netadrtype_t::NA_IP6 || ( ( type ) == netadrtype_t::NA_IP_DUAL && ( net_enabled->integer & NET_ENABLEV6 ) && ( ( ~net_enabled->integer & NET_ENABLEV4) || (  net_enabled->integer & NET_PRIOV6 ) ) ) )
// if NA_IP_DUAL, get the preferred type (falling back on NA_IP)
#define NET_TYPE( type )    ( NET_IS_IPv4( type ) ? netadrtype_t::NA_IP : NET_IS_IPv6( type ) ? netadrtype_t::NA_IP6 : ( ( type ) == netadrtype_t::NA_IP_DUAL ) ? netadrtype_t::NA_IP : ( type ) )

#define PACKET_BACKUP       32 // number of old messages that must be kept on client and
// server for delta compression and ping estimation
#define PACKET_MASK         ( PACKET_BACKUP - 1 )

#define MAX_PACKET_USERCMDS 32 // max number of usercmd_t in a packet

#define PORT_ANY            0

// RF, increased this, seems to keep causing problems when set to 64, especially when loading
// a savegame, which is hard to fix on that side, since we can't really spread out a loadgame
// among several frames
//#define   MAX_RELIABLE_COMMANDS   64          // max string commands buffered for restransmit
//#define   MAX_RELIABLE_COMMANDS   128         // max string commands buffered for restransmit
#define MAX_RELIABLE_COMMANDS 256 // bigger!

// maximum length of an IPv6 address string including trailing '\0'
#define NET_ADDR_STR_MAX_LEN 48

// maximum length of an formatted IPv6 address string including port and trailing '\0'
// format [%s]:%hu - 48 for %s (address), 3 for []: and 5 for %hu (port number, max value 65535)
#define NET_ADDR_W_PORT_STR_MAX_LEN ( NET_ADDR_STR_MAX_LEN + 3 + 5 )

extern cvar_t       *net_enabled;

void       NET_Init();
void       NET_Shutdown();
void       NET_Restart_f();
void       NET_EnableNetworking( bool serverMode );
void       NET_DisableNetworking();

void       NET_SendPacket( netsrc_t sock, int length, const void *data, const netadr_t& to );

bool   NET_CompareAdr( const netadr_t& a, const netadr_t& b );
bool   NET_CompareBaseAdr( const netadr_t& a, const netadr_t& b );
bool   NET_IsLocalAddress( const netadr_t& adr );
// DEPRECATED: Use Net::AddressToString
const char *NET_AdrToString( const netadr_t& a );
int        NET_StringToAdr( const char *s, netadr_t *a, netadrtype_t family );
bool   NET_GetLoopPacket( netsrc_t sock, netadr_t *net_from, msg_t *net_message );
void       NET_JoinMulticast6();
void       NET_LeaveMulticast6();

void       NET_Sleep( int msec );

//----(SA)  increased for larger submodel entity counts
#define MAX_MSGLEN           32768 // max length of a message, which may
//#define   MAX_MSGLEN              16384       // max length of a message, which may
// be fragmented into multiple packets
#define MAX_DOWNLOAD_WINDOW  8 // max of eight download frames
#define MAX_DOWNLOAD_BLKSIZE 2048 // 2048 byte block chunks

/*
Netchan handles packet fragmentation and out of order / duplicate suppression
*/

struct netchan_t
{
    netsrc_t sock;

    int      dropped; // between last packet and previous

    netadr_t remoteAddress;
    int      qport; // qport value to write when transmitting

    // sequencing variables
    int incomingSequence;
    int outgoingSequence;

    // incoming fragment assembly buffer
    int  fragmentSequence;
    int  fragmentLength;
    byte fragmentBuffer[ MAX_MSGLEN ];

    // outgoing fragment buffer
    // we need to space out the sending of large fragmented messages
    bool unsentFragments;
    int      unsentFragmentStart;
    int      unsentLength;
    byte     unsentBuffer[ MAX_MSGLEN ];
};

void     Netchan_Init( int qport );
void     Netchan_Setup( netsrc_t sock, netchan_t *chan, const netadr_t& adr, int qport );

void     Netchan_Transmit( netchan_t *chan, int length, const byte *data );
void     Netchan_TransmitNextFragment( netchan_t *chan );

bool Netchan_Process( netchan_t *chan, msg_t *msg );

/*
==============================================================

PROTOCOL

==============================================================
*/

// sent by the server, printed on connection screen, works for all clients
// (restrictions: does not handle \n, no more than 256 chars)
#define PROTOCOL_MISMATCH_ERROR      "ERROR: Protocol Mismatch Between Client and Server. \
The server you are attempting to join is running an incompatible version of the game."

// long version used by the client in diagnostic window
#define PROTOCOL_MISMATCH_ERROR_LONG "ERROR: Protocol Mismatch Between Client and Server.\n\n\
The server you attempted to join is running an incompatible version of the game.\n\
You or the server may be running older versions of the game."

#define PROTOCOL_VERSION       86

#define URI_SCHEME             GAMENAME_STRING "://"
#define URI_SCHEME_LENGTH      ( ARRAY_LEN( URI_SCHEME ) - 1 )

#define PORT_MASTER             27950
#define PORT_SERVER             27960
#define NUM_SERVER_PORTS        4 // broadcast scan this many ports after
// PORT_SERVER so a single machine can
// run multiple servers
// the svc_strings[] array in cl_parse.c should mirror this
//
// server to client
//
enum svc_ops_e
{
  svc_bad,
  svc_nop,
  svc_gamestate,
  svc_gamestatePartial,
  svc_configstring, // [short] [string] only in gamestate messages
  svc_baseline, // only in gamestate messages
  svc_serverCommand, // [string] to be executed by client game module
  svc_download, // [short] size [size bytes]
  svc_snapshot,
  svc_EOF
};

//
// client to server
//
enum clc_ops_e
{
  clc_bad,
  clc_nop,
  clc_move, // [usercmd_t]
  clc_moveNoDelta, // [usercmd_t]
  clc_clientCommand, // [string] message
  clc_EOF,
};

/*
==============================================================

CMD

Command text buffering and command execution

==============================================================
*/

/*

Any number of commands can be added in a frame, from several different sources.
Most commands come from either keybindings or console line input, but entire text
files can be execed.

*/

//===========================================================================

/*

Command execution takes a null terminated string, breaks it into tokens,
then searches for a command or variable that matches the first token.

*/

using xcommand_t = void (*)();
using xcommand_arg_t = void (*)(int);

void     Cmd_AddCommand( const char *cmd_name, xcommand_t function );

// called by the init functions of other parts of the program to
// register commands and functions to call for them.
// The cmd_name is referenced later, so it should not be in temp memory
// if function is nullptr, the command will be forwarded to the server
// as a clc_clientCommand instead of executed locally

void Cmd_RemoveCommand( const char *cmd_name );

using completionFunc_t = void (*)(char *args, int argNum);

// callback with each valid string

void Cmd_PrintUsage( const char *syntax, const char *description );
int  Cmd_Argc();
const char *Cmd_Argv( int arg );
char *Cmd_Args();
char *Cmd_ArgsFrom( int arg );

// these all share an output buffer
const char *Cmd_QuoteString( const char *in );
const char *Cmd_UnquoteString( const char *in );

// The functions that execute commands get their parameters with these
// functions. Cmd_Argv () will return an empty string, not a nullptr
// if arg >= argc, so string operations are always safe.

void Cmd_TokenizeString( const char *text );
void Cmd_SaveCmdContext();
void Cmd_RestoreCmdContext();

/*
==============================================================

FILESYSTEM

No stdio calls should be used by any part of the game, because
we need to deal with all sorts of directory and separator char
issues.
==============================================================
*/

char **FS_ListFiles( const char *directory, const char *extension, int *numfiles );

// directory should not have either a leading or trailing /
// if extension is "/", only subdirectories will be returned
// the returned files will not include any directories or /

void         FS_FreeFileList( char **list );

int          FS_GetFileList( const char *path, const char *extension, char *listbuf, int bufsize );
int          FS_GetFileListRecursive( const char* path, const char* extension, char* listBuf, int bufSize );

fileHandle_t FS_FOpenFileWrite( const char *qpath );
fileHandle_t FS_FOpenFileAppend( const char *filename );

fileHandle_t FS_FOpenFileWriteViaTemporary( const char *qpath );

// will properly create any needed paths and deal with separator character issues

fileHandle_t FS_SV_FOpenFileWrite( const char *filename );
void         FS_SV_Rename( const char *from, const char *to );
int          FS_FOpenFileRead( const char *qpath, fileHandle_t *file );

namespace FS { enum class Owner; }
void FS_SetOwner( fileHandle_t f, FS::Owner owner );
void FS_CheckOwnership( fileHandle_t f, FS::Owner owner );
void FS_CloseAllForOwner( FS::Owner owner );

int FS_Delete( const char *filename );  // only works inside the 'save' directory (for deleting savegames/images)

int FS_Write( const void *buffer, int len, fileHandle_t f );

int FS_Read( void *buffer, int len, fileHandle_t f );

// properly handles partial reads and reads from other dlls

int FS_FCloseFile( fileHandle_t f ); // !0 on error (but errno isn't valid)

// note: you can't just fclose from another DLL, due to MS libc issues

int  FS_ReadFile( const char *qpath, void **buffer );

// returns the length of the file
// a null buffer will just return the file length without loading,
// as a quick check for existence. -1 length == not present
// A 0 byte will always be appended at the end, so string ops are safe.
// the buffer should be considered read-only, because it may be cached
// for other uses.

void FS_ForceFlush( fileHandle_t f );

// forces flush on files we're writing to.

void FS_FreeFile( void *buffer );

// frees the memory returned by FS_ReadFile

void FS_WriteFile( const char *qpath, const void *buffer, int size );

// writes a complete file, creating any subdirectories needed

int FS_filelength( fileHandle_t f );

// doesn't work for files that are opened from a pack file

int FS_FTell( fileHandle_t f );

// where are we?

void FS_Flush( fileHandle_t f );

void FS_Printf( fileHandle_t f, const char *fmt, ... ) PRINTF_LIKE(2);

// like fprintf

int FS_Game_FOpenFileByMode( const char *qpath, fileHandle_t *f, fsMode_t mode );

// opens a file for reading, writing, or appending depending on the value of mode

int FS_Seek( fileHandle_t f, long offset, fsOrigin_t origin );

// seek on a file (doesn't work for zip files!!!!!!!!)

const char* FS_LoadedPaks();

// Returns a space separated string containing all loaded dpk/pk3 files.

bool FS_LoadPak( const Str::StringRef name );
void     FS_LoadBasePak();
bool     FS_LoadServerPaks( const char* paks, bool isDemo );

// shutdown and restart the filesystem so changes to fs_gamedir can take effect

void FS_DeletePaksWithBadChecksum();
bool FS_ComparePaks(char* neededpaks, int len);

/*
==============================================================

INPUT

==============================================================
*/

void IN_Init(void *windowData);
void IN_Frame();
void IN_FrameEnd();
void IN_Restart();
void IN_Shutdown();
bool IN_IsNumLockOn();
void IN_DropInputsForFrame();
void IN_CenterMouse();
bool IN_IsKeyboardLayoutInfoAvailable();

/*
==============================================================

DOWNLOAD

==============================================================
*/

enum class dlStatus_t
{
  DL_CONTINUE,
  DL_DONE,
  DL_FAILED
};

int        DL_BeginDownload( const char *localName, const char *remoteName, int basePathLen );
dlStatus_t DL_DownloadLoop();

void       DL_Shutdown();

/*
==============================================================

Edit fields and command line history/completion

==============================================================
*/

#define MAX_EDIT_LINE 256
struct field_t
{
    int  cursor;
    int  scroll;
    int  widthInChars;
    char buffer[ MAX_EDIT_LINE ];
};

/*
==============================================================

MISC

==============================================================
*/

// TTimo
// centralized and cleaned, that's the max string you can send to a Log::Notice / Com_DPrintf (above gets truncated)
#define MAXPRINTMSG 4096

// DEPRECATED: Use InfoMap
void       Info_Print( const char *s );

// *INDENT-OFF*

#define    PrintBanner(text) Log::Notice("----- %s -----", text );

// *INDENT-ON*
int        Com_Milliseconds();
unsigned   Com_BlockChecksum( const void *buffer, int length );
char       *Com_MD5File( const char *filename, int length );
void       Com_MD5Buffer( const char *pubkey, int size, char *buffer, int bufsize );

bool       Com_AreCheatsAllowed();
bool       Com_IsClient();
bool       Com_IsDedicatedServer();
bool       Com_ServerRunning();

// checks for and removes command line "+set var arg" constructs
// if match is nullptr, all set commands will be executed, otherwise
// only a set with the exact name.  Only used during startup.

extern cvar_t       *com_speeds;
extern cvar_t       *com_timescale;
extern cvar_t       *com_sv_running;
extern cvar_t       *com_cl_running;
extern cvar_t       *com_version;

extern Cvar::Cvar<std::string> com_consoleCommand;

extern Cvar::Cvar<bool> com_ansiColor;

extern cvar_t       *com_unfocused;
extern cvar_t       *com_minimized;

// com_speeds times
extern int          time_game;
extern int          time_frontend;
extern int          time_backend; // renderer backend time

extern int          com_frameTime;
extern int          com_frameMsec;

// Use malloc instead of the zone allocator...

// Allocations uncategorized as to whether they really need zeroing
inline MALLOC_LIKE void* Z_Malloc(size_t size)
{
  void* p = calloc(size, 1);
  if (!p && size) Sys::Error("Z_Malloc: Out of memory");
  return p;
}
// Allocates unitialized memory like malloc
inline MALLOC_LIKE void* Z_AllocUninit(size_t size)
{
    void* p = malloc(size);
    if (!p && size) Sys::Error("Z_AllocUninit: Out of memory");
    return p;
}
// Allocates zeroed memory like calloc
inline MALLOC_LIKE void* Z_Calloc(size_t size)
{
    void* p = calloc(size, 1);
    if (!p && size) Sys::Error("Z_Calloc: Out of memory");
    return p;
}

inline ALLOCATOR char* CopyString(const char* str)
{
  return strdup(str);
}
inline void Z_Free(void* ptr)
{
  free(ptr);
}

#ifndef BUILD_SERVER
void Hunk_Init();
void     Hunk_Clear();
void Hunk_Shutdown();
void *Hunk_Alloc( int size, ha_pref preference );
void   *Hunk_AllocateTempMemory( int size );
void   Hunk_FreeTempMemory( void *buf );
#endif

// commandLine should not include the executable name (argv[0])
void   Com_Init();
void   Com_Frame();

/*
==============================================================

CLIENT / SERVER SYSTEMS

==============================================================
*/

//
// client interface
//
void CL_InitKeyCommands();

// the keyboard binding interface must be setup before execing
// config files, but the rest of client startup will happen later

void     CL_Init();
void     CL_Disconnect( bool showMainMenu );
void     CL_Shutdown();
void     CL_Frame( int msec );
void     CL_ConsoleKeyEvent();
namespace Keyboard { class Key; }
void     CL_KeyDownEvent( const Keyboard::Key& key1, const Keyboard::Key& key2, unsigned time );
void     CL_KeyRepeatEvent( const Keyboard::Key& key );
void     CL_KeyUpEvent( const Keyboard::Key& key, unsigned time );

void     CL_CharEvent( int c );

// char events are for field typing, not game control

void CL_MouseEvent( int dx, int dy );
void CL_MousePosEvent( int dx, int dy);
void CL_FocusEvent( bool focus );


void CL_JoystickEvent( int axis, int value );

void CL_PacketEvent( const netadr_t& from, msg_t *msg );

void CL_ConsolePrint( std::string text );

void CL_MapLoading();

// do a screen update before starting to load a map
// when the server is going to load a new map, the entire hunk
// will be cleared, so the client must shutdown cgame, ui, and
// the renderer

void CL_ForwardCommandToServer( const char *string );

// adds the current command line as a clc_clientCommand to the client message.
// things like godmode, noclip, etc, are commands directed to the server,
// so when they are typed in at the console, they will need to be forwarded.

void CL_ShutdownAll();

// shutdown all the client stuff

// AVI files have the start of pixel lines 4 byte-aligned
#define AVI_LINE_PADDING 4

//
// server interface
//
void     SV_Init();
void     SV_Shutdown( const char *finalmsg );
void     SV_QuickShutdown( const char *finalmsg );
void     SV_Frame( int msec );
void     SV_PacketEvent( const netadr_t& from, msg_t *msg );
int      SV_FrameMsec();

/*
==============================================================

NON-PORTABLE SYSTEM SERVICES

==============================================================
*/

enum class joystickAxis_t
{
  AXIS_SIDE,
  AXIS_FORWARD,
  AXIS_UP,
  AXIS_ROLL,
  AXIS_YAW,
  AXIS_PITCH,
  MAX_JOYSTICK_AXIS
};

enum class sysEventType_t
{
  SE_CONSOLE_KEY,
  SE_KEY,
  SE_CHAR,
  SE_MOUSE,
  SE_MOUSE_POS,
  SE_JOYSTICK_AXIS,
  SE_CONSOLE, // terminal input
  SE_PACKET,
  SE_FOCUS,
};

namespace Sys {
    class EventBase;
}
void       Com_QueueEvent( std::unique_ptr<Sys::EventBase> event );
void       Com_EventLoop();

// Curses Console
void         CON_Shutdown();
void         CON_Init();
void         CON_Init_TTY();
char         *CON_Input();
void         CON_Print( const char *message );

/* This is based on the Adaptive Huffman algorithm described in Sayood's Data
 * Compression book.  The ranks are not actually stored, but implicitly defined
 * by the location of a node within a doubly-linked list */

#define NYT           HMAX /* NYT = Not Yet Transmitted */
#define INTERNAL_NODE ( HMAX + 1 )

struct node_t
{
    node_t *left, *right, *parent; /* tree structure */

    node_t *next, *prev; /* doubly-linked list */

    node_t **head; /* highest ranked node in block */

    int             weight;
    int             symbol;
};

#define HMAX 256 /* Maximum symbol */

struct huff_t
{
    int    blocNode;
    int    blocPtrs;

    node_t *tree;
    node_t *lhead;
    node_t *ltail;
    node_t *loc[ HMAX + 1 ];
    node_t **freelist;

    node_t nodeList[ 768 ];
    node_t *nodePtrs[ 768 ];
};

struct huffman_t
{
    huff_t compressor;
    huff_t decompressor;
};

void             Huff_Compress( msg_t *buf, int offset );
void             Huff_Decompress( msg_t *buf, int offset );
void             Huff_Init( huffman_t *huff );
void             Huff_addRef( huff_t *huff, byte ch );
int              Huff_Receive( node_t *node, int *ch, byte *fin );
void             Huff_transmit( huff_t *huff, int ch, byte *fout );
void             Huff_offsetReceive( node_t *node, int *ch, byte *fin, int *offset );
void             Huff_offsetTransmit( huff_t *huff, int ch, byte *fout, int *offset );
void             Huff_putBit( int bit, byte *fout, int *offset );
int              Huff_getBit( byte *fout, int *offset );

void Trans_LoadDefaultLanguage();
#endif // QCOMMON_H_
