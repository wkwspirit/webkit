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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#import "JSScript.h"
#import "SourceCode.h"

#if JSC_OBJC_API_ENABLED

NS_ASSUME_NONNULL_BEGIN

namespace JSC {
class CachedBytecode;
class Identifier;
class JSSourceCode;
};

namespace WTF {
class String;
};

@interface JSScript(Internal)

- (instancetype)init;
- (unsigned)hash;
- (const WTF::String&)source;
- (nullable const JSC::CachedBytecode*)cachedBytecode;
- (JSC::JSSourceCode*)jsSourceCode;
// FIXME: Remove this once we require sourceURL upon creation: https://bugs.webkit.org/show_bug.cgi?id=194909
- (JSC::JSSourceCode*)forceRecreateJSSourceCode;
- (BOOL)writeCache:(String&)error;
- (void)setSourceURL:(NSURL *)url;

@end

NS_ASSUME_NONNULL_END

#endif // JSC_OBJC_API_ENABLED
