/*
===========================================================================

Daemon BSD Source Code
Copyright (c) 2025 Daemon Developers
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
// GeometryCache.h

#ifndef GEOMETRY_CACHE_H
#define GEOMETRY_CACHE_H

#include "gl_shader.h"
#include "Material.h"

class GeometryCache {
	public:
	void Bind();

	void InitGLBuffers();
	void FreeGLBuffers();

	void Free();

	void AllocBuffers();
	void AddMapGeometry( const uint32_t verticesNumber, const uint32_t indicesNumber,
		const vertexAttributeSpec_t* attrBegin,
		const vertexAttributeSpec_t* attrEnd,
		const glIndex_t* indices );

	private:
	uint32_t mapVerticesNumber;
	uint32_t mapIndicesNumber;

	GLVAO VAO = GLVAO( 0 );

	GLBuffer inputVBO = GLBuffer( "geometryCacheInputVBO", Util::ordinal( BufferBind::GEOMETRY_CACHE_INPUT_VBO ), GL_MAP_WRITE_BIT, GL_MAP_INVALIDATE_RANGE_BIT );
	GLBuffer VBO = GLBuffer( "geometryCacheVBO", Util::ordinal( BufferBind::GEOMETRY_CACHE_VBO ), GL_MAP_WRITE_BIT, GL_MAP_FLUSH_EXPLICIT_BIT );
	GLBuffer IBO = GLBuffer( "geometryCacheIBO", Util::ordinal( BufferBind::UNUSED ), GL_MAP_WRITE_BIT, GL_MAP_INVALIDATE_RANGE_BIT );
};

extern GeometryCache geometryCache;

#endif // GEOMETRY_CACHE_H
