/*
===========================================================================

Daemon BSD Source Code
Copyright (c) 2024 Daemon Developers
All rights reserved.

This file is part of the Daemon BSD Source Code (Daemon Source Code).

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in the
	  documentation and/or other materials provided with the distribution.
	* Neither the name of the Daemon developers nor the
	  names of its contributors may be used to endorse or promote products
	  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL DAEMON DEVELOPERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

===========================================================================
*/

/* processSurfaces_cp.glsl */

#insert common_cp
#insert material_cp

// Keep this to 64 because we don't want extra shared mem etc. to be allocated, and to minimize wasted lanes
layout (local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#define SurfaceCommandBatch uvec4

layout(std430, binding = BIND_SURFACE_COMMANDS) readonly restrict buffer surfaceCommandsSSBO {
	SurfaceCommand surfaceCommands[];
};

layout(std430, binding = BIND_CULLED_COMMANDS) writeonly restrict buffer culledCommandsSSBO {
	GLIndirectCommand culledCommands[];
};

layout(std140, binding = BIND_SURFACE_BATCHES) uniform ub_SurfaceBatches {
	SurfaceCommandBatch surfaceBatches[MAX_SURFACE_COMMAND_BATCHES];
};

layout (binding = BIND_COMMAND_COUNTERS_ATOMIC) uniform atomic_uint atomicCommandCounters[MAX_COMMAND_COUNTERS * MAX_VIEWFRAMES];

uniform uint u_Frame;
uniform uint u_ViewID;
uniform uint u_SurfaceCommandsOffset;

void AddDrawCommand( in uint commandID, in uvec2 materialID ) {
	SurfaceCommand command = surfaceCommands[commandID + u_SurfaceCommandsOffset];

	#if defined(HAVE_subgroup_stream_compaction)
		const uint count = subgroupBallotBitCount( subgroupBallot( command.enabled ) );
		// Exclusive scan so we can determine the offset for each lane without any synchronization
		const uint subgroupOffset = subgroupExclusiveAdd( command.enabled ? 1 : 0 );
		
		uint atomicCmdID = 0;
		// Once per subgroup
		if( subgroupElect() ) {
			atomicCmdID = atomicCounterAddARB( atomicCommandCounters[materialID.x
		                                                 + MAX_COMMAND_COUNTERS * ( MAX_VIEWS * u_Frame + u_ViewID )], count );
		}

		atomicCmdID = subgroupBroadcastFirst( atomicCmdID );
	#endif
	
	if( command.enabled ) {
		GLIndirectCommand indirectCommand;
		indirectCommand.count = command.drawCommand.count;
		indirectCommand.instanceCount = 1;
		indirectCommand.firstIndex = command.drawCommand.firstIndex;
		indirectCommand.baseVertex = 0;
		indirectCommand.baseInstance = command.drawCommand.baseInstance;
		
		// materialID.x is the global ID of the material
		// materialID.y is the offset for the memory allocated to the material's culled commands
		#if defined(HAVE_subgroup_stream_compaction)
			culledCommands[atomicCmdID + subgroupOffset + materialID.y * MAX_COMMAND_COUNTERS + u_SurfaceCommandsOffset] = indirectCommand;
		#else
			const uint atomicCmdID = atomicCounterIncrement( atomicCommandCounters[materialID.x
		                                                     + MAX_COMMAND_COUNTERS * ( MAX_VIEWS * u_Frame + u_ViewID )] );
			culledCommands[atomicCmdID + materialID.y * MAX_COMMAND_COUNTERS + u_SurfaceCommandsOffset] = indirectCommand;
		#endif
	}
}

void main() {
	const uint globalGroupID = GLOBAL_GROUP_ID;
	const uint globalInvocationID = GLOBAL_INVOCATION_ID;

	// Each surfaceBatch encompasses 64 surfaceCommands with the same material, padded to 64 as necessary
	const uvec2 materialID = UVEC2_FROM_UVEC4_ARRAY( surfaceBatches, globalGroupID );

	AddDrawCommand( globalInvocationID, materialID );
}
