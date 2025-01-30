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
// GeometryCache.cpp

#include "GeometryCache.h"

#include "tr_local.h"

GeometryCache geometryCache;

void GeometryCache::Bind() {
	VAO.Bind();
}

void GeometryCache::InitGLBuffers() {
	inputVBO.GenBuffer();
	VBO.GenBuffer();
	IBO.GenBuffer();

	VAO.GenVAO();
}

void GeometryCache::FreeGLBuffers() {
	inputVBO.DelBuffer();
	VBO.DelBuffer();
	IBO.DelBuffer();

	VAO.DelVAO();
}

void GeometryCache::Free() {
}

void GeometryCache::AllocBuffers() {
	VBO.BufferData( mapVerticesNumber * 8, nullptr, GL_STATIC_DRAW );

	IBO.BufferData( mapIndicesNumber, nullptr, GL_STATIC_DRAW );
}

void GeometryCache::AddMapGeometry( const uint32_t verticesNumber, const uint32_t indicesNumber,
	const vertexAttributeSpec_t* attrBegin, const vertexAttributeSpec_t* attrEnd,
	const glIndex_t* indices ) {
	mapVerticesNumber = verticesNumber;
	mapIndicesNumber = indicesNumber;

	VAO.Bind();

	AllocBuffers();

	VAO.SetAttrs( attrBegin, attrEnd );

	VAO.SetVertexBuffer( VBO, 0 );
	VAO.SetIndexBuffer( IBO );
	
	VBO.BufferStorage( mapVerticesNumber * 8, 1, nullptr );
	VBO.MapAll();
	uint32_t* VBOVerts = VBO.GetData();
	for ( const vertexAttributeSpec_t* spec = attrBegin; spec < attrEnd; spec++ ) {
		vboAttributeLayout_t& attr = VAO.attrs[spec->attrIndex];

		CopyVertexAttribute( attr, *spec, mapVerticesNumber, ( byte* ) VBOVerts );
	}
	VBO.UnmapBuffer();

	IBO.BufferStorage( mapIndicesNumber, 1, nullptr );
	IBO.MapAll();
	uint32_t* IBOIndices = IBO.GetData();
	memcpy( IBOIndices, indices, mapIndicesNumber * sizeof( uint32_t ) );
	IBO.UnmapBuffer();

	glBindVertexArray( backEnd.currentVAO );
}
