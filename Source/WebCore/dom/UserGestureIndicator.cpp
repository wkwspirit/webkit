/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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

#include "config.h"
#include "UserGestureIndicator.h"

#include "Document.h"
#include "Frame.h"
#include "ResourceLoadObserver.h"
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>

namespace WebCore {

static RefPtr<UserGestureToken>& currentToken()
{
    ASSERT(isMainThread());
    static NeverDestroyed<RefPtr<UserGestureToken>> token;
    return token;
}

UserGestureToken::~UserGestureToken()
{
    for (auto& observer : m_destructionObservers)
        observer(*this);
}

UserGestureIndicator::UserGestureIndicator(Optional<ProcessingUserGestureState> state, Document* document, UserGestureType gestureType, ProcessInteractionStyle processInteractionStyle)
    : m_previousToken { currentToken() }
{
    ASSERT(isMainThread());

    if (state)
        currentToken() = UserGestureToken::create(state.value(), gestureType);

    if (document && currentToken()->processingUserGesture()) {
        document->updateLastHandledUserGestureTimestamp(MonotonicTime::now());
        if (processInteractionStyle == ProcessInteractionStyle::Immediate)
            ResourceLoadObserver::shared().logUserInteractionWithReducedTimeResolution(document->topDocument());
        document->topDocument().setUserDidInteractWithPage(true);
        if (auto* frame = document->frame()) {
            if (!frame->hasHadUserInteraction()) {
                for (; frame; frame = frame->tree().parent())
                    frame->setHasHadUserInteraction();
            }
        }
    }
}

UserGestureIndicator::UserGestureIndicator(RefPtr<UserGestureToken> token)
{
    // Silently ignore UserGestureIndicators on non main threads.
    if (!isMainThread())
        return;

    // It is only safe to use currentToken() on the main thread.
    m_previousToken = currentToken();

    if (token)
        currentToken() = token;
}

UserGestureIndicator::~UserGestureIndicator()
{
    if (!isMainThread())
        return;
    
    if (auto token = currentToken())
        token->resetDOMPasteAccess();

    currentToken() = m_previousToken;
}

RefPtr<UserGestureToken> UserGestureIndicator::currentUserGesture()
{
    if (!isMainThread())
        return nullptr;

    return currentToken();
}

bool UserGestureIndicator::processingUserGesture()
{
    if (!isMainThread())
        return false;

    return currentToken() ? currentToken()->processingUserGesture() : false;
}

bool UserGestureIndicator::processingUserGestureForMedia()
{
    if (!isMainThread())
        return false;

    return currentToken() ? currentToken()->processingUserGestureForMedia() : false;
}

}
