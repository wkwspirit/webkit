/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(WEBGPU)

#include "WHLSLNativeFunctionDeclaration.h"
#include "WHLSLNativeTypeDeclaration.h"
#include <cstring>
#include <wtf/HashSet.h>
#include <wtf/StdLibExtras.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

namespace WHLSL {

class Intrinsics {
public:
    Intrinsics();

    void add(AST::NativeFunctionDeclaration&);
    void add(AST::NativeTypeDeclaration&);

    AST::NativeTypeDeclaration& voidType() const
    {
        ASSERT(m_voidType);
        return *m_voidType;
    }

    AST::NativeTypeDeclaration& boolType() const
    {
        ASSERT(m_boolType);
        return *m_boolType;
    }

    AST::NativeTypeDeclaration& intType() const
    {
        ASSERT(m_intType);
        return *m_intType;
    }

    AST::NativeTypeDeclaration& uintType() const
    {
        ASSERT(m_uintType);
        return *m_uintType;
    }

    AST::NativeTypeDeclaration& samplerType() const
    {
        ASSERT(m_samplerType);
        return *m_samplerType;
    }

    AST::NativeTypeDeclaration& floatType() const
    {
        ASSERT(m_floatType);
        return *m_floatType;
    }

    AST::NativeTypeDeclaration& float2Type() const
    {
        ASSERT(m_vectorFloat[0]);
        return *m_vectorFloat[0];
    }

    AST::NativeTypeDeclaration& float3Type() const
    {
        ASSERT(m_vectorFloat[1]);
        return *m_vectorFloat[1];
    }

    AST::NativeTypeDeclaration& float4Type() const
    {
        ASSERT(m_vectorFloat[2]);
        return *m_vectorFloat[2];
    }

    AST::NativeFunctionDeclaration& ddx() const
    {
        ASSERT(m_ddx);
        return *m_ddx;
    }

    AST::NativeFunctionDeclaration& ddy() const
    {
        ASSERT(m_ddy);
        return *m_ddy;
    }

    AST::NativeFunctionDeclaration& allMemoryBarrier() const
    {
        ASSERT(m_allMemoryBarrier);
        return *m_allMemoryBarrier;
    }

    AST::NativeFunctionDeclaration& deviceMemoryBarrier() const
    {
        ASSERT(m_deviceMemoryBarrier);
        return *m_deviceMemoryBarrier;
    }

    AST::NativeFunctionDeclaration& groupMemoryBarrier() const
    {
        ASSERT(m_groupMemoryBarrier);
        return *m_groupMemoryBarrier;
    }

private:
    bool addPrimitive(AST::NativeTypeDeclaration&);
    bool addVector(AST::NativeTypeDeclaration&);
    bool addMatrix(AST::NativeTypeDeclaration&);
    bool addFullTexture(AST::NativeTypeDeclaration&, AST::TypeReference&);
    bool addDepthTexture(AST::NativeTypeDeclaration&, AST::TypeReference&);
    void addTexture(AST::NativeTypeDeclaration&);

    HashSet<const AST::NativeTypeDeclaration*> m_textureSet;

    AST::NativeTypeDeclaration* m_voidType { nullptr };
    AST::NativeTypeDeclaration* m_boolType { nullptr };
    AST::NativeTypeDeclaration* m_ucharType { nullptr };
    AST::NativeTypeDeclaration* m_ushortType { nullptr };
    AST::NativeTypeDeclaration* m_uintType { nullptr };
    AST::NativeTypeDeclaration* m_charType { nullptr };
    AST::NativeTypeDeclaration* m_shortType { nullptr };
    AST::NativeTypeDeclaration* m_intType { nullptr };
    AST::NativeTypeDeclaration* m_halfType { nullptr };
    AST::NativeTypeDeclaration* m_floatType { nullptr };
    AST::NativeTypeDeclaration* m_atomicIntType { nullptr };
    AST::NativeTypeDeclaration* m_atomicUintType { nullptr };
    AST::NativeTypeDeclaration* m_samplerType { nullptr };

    AST::NativeTypeDeclaration* m_vectorBool[3] { 0 };
    AST::NativeTypeDeclaration* m_vectorUchar[3] { 0 };
    AST::NativeTypeDeclaration* m_vectorUshort[3] { 0 };
    AST::NativeTypeDeclaration* m_vectorUint[3] { 0 };
    AST::NativeTypeDeclaration* m_vectorChar[3] { 0 };
    AST::NativeTypeDeclaration* m_vectorShort[3] { 0 };
    AST::NativeTypeDeclaration* m_vectorInt[3] { 0 };
    AST::NativeTypeDeclaration* m_vectorHalf[3] { 0 };
    AST::NativeTypeDeclaration* m_vectorFloat[3] { 0 };

    AST::NativeTypeDeclaration* m_matrixHalf[3][3] {{ 0 }};
    AST::NativeTypeDeclaration* m_matrixFloat[3][3] {{ 0 }};

    static constexpr const char* m_textureTypeNames[] = { "Texture1D", "RWTexture1D", "Texture1DArray", "RWTexture1DArray", "Texture2D", "RWTexture2D", "Texture2DArray", "RWTexture2DArray", "Texture3D", "RWTexture3D", "TextureCube" };

    static constexpr const char* m_textureInnerTypeNames[] = { "uchar", "ushort",  "uint", "char", "short", "int", "half", "float" };

    AST::NativeTypeDeclaration* m_fullTextures[WTF_ARRAY_LENGTH(m_textureTypeNames)][WTF_ARRAY_LENGTH(m_textureInnerTypeNames)][4] {{{ 0 }}};

    static constexpr const char* m_depthTextureInnerTypes[] =  { "half", "float" };

    AST::NativeTypeDeclaration* m_textureDepth2D[WTF_ARRAY_LENGTH(m_depthTextureInnerTypes)] { 0 };
    AST::NativeTypeDeclaration* m_rwTextureDepth2D[WTF_ARRAY_LENGTH(m_depthTextureInnerTypes)] { 0 };
    AST::NativeTypeDeclaration* m_textureDepth2DArray[WTF_ARRAY_LENGTH(m_depthTextureInnerTypes)] { 0 };
    AST::NativeTypeDeclaration* m_rwTextureDepth2DArray[WTF_ARRAY_LENGTH(m_depthTextureInnerTypes)] { 0 };
    AST::NativeTypeDeclaration* m_textureDepthCube[WTF_ARRAY_LENGTH(m_depthTextureInnerTypes)] { 0 };

    AST::NativeFunctionDeclaration* m_ddx { nullptr };
    AST::NativeFunctionDeclaration* m_ddy { nullptr };
    AST::NativeFunctionDeclaration* m_allMemoryBarrier { nullptr };
    AST::NativeFunctionDeclaration* m_deviceMemoryBarrier { nullptr };
    AST::NativeFunctionDeclaration* m_groupMemoryBarrier { nullptr };
};

} // namespace WHLSL

} // namespace WebCore

#endif // ENABLE(WEBGPU)
