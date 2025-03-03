/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 * Copyright (C) 2015 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "InspectorOverlay.h"
#include "PageScriptDebugServer.h"
#include <JavaScriptCore/InspectorAgentRegistry.h>
#include <JavaScriptCore/InspectorEnvironment.h>
#include <wtf/Forward.h>
#include <wtf/Noncopyable.h>
#include <wtf/text/WTFString.h>

namespace Inspector {
class BackendDispatcher;
class FrontendChannel;
class FrontendRouter;
class InspectorAgent;
}

namespace WebCore {

class DOMWrapperWorld;
class Frame;
class GraphicsContext;
class InspectorClient;
class InspectorDOMAgent;
class InspectorFrontendClient;
class InspectorInstrumentation;
class InspectorPageAgent;
class InstrumentingAgents;
class Node;
class Page;
class WebInjectedScriptManager;
struct PageAgentContext;

class InspectorController final : public Inspector::InspectorEnvironment {
    WTF_MAKE_NONCOPYABLE(InspectorController);
    WTF_MAKE_FAST_ALLOCATED;
public:
    InspectorController(Page&, InspectorClient*);
    virtual ~InspectorController();

    void inspectedPageDestroyed();

    bool enabled() const;
    Page& inspectedPage() const;

    WEBCORE_EXPORT void show();

    WEBCORE_EXPORT void setInspectorFrontendClient(InspectorFrontendClient*);
    unsigned inspectionLevel() const;
    void didClearWindowObjectInWorld(Frame&, DOMWrapperWorld&);

    WEBCORE_EXPORT void dispatchMessageFromFrontend(const String& message);

    bool hasLocalFrontend() const;
    bool hasRemoteFrontend() const;

    WEBCORE_EXPORT void connectFrontend(Inspector::FrontendChannel&, bool isAutomaticInspection = false, bool immediatelyPause = false);
    WEBCORE_EXPORT void disconnectFrontend(Inspector::FrontendChannel&);
    WEBCORE_EXPORT void disconnectAllFrontends();

    void inspect(Node*);
    WEBCORE_EXPORT void drawHighlight(GraphicsContext&) const;
    WEBCORE_EXPORT void getHighlight(Highlight&, InspectorOverlay::CoordinateSystem) const;
    void hideHighlight();
    Node* highlightedNode() const;

    WEBCORE_EXPORT void setIndicating(bool);

    WEBCORE_EXPORT void didComposite(Frame&);

    bool isUnderTest() const { return m_isUnderTest; }
    WEBCORE_EXPORT void setIsUnderTest(bool);
    WEBCORE_EXPORT void evaluateForTestInFrontend(const String& script);

    InspectorClient* inspectorClient() const { return m_inspectorClient; }
    InspectorFrontendClient* inspectorFrontendClient() const { return m_inspectorFrontendClient; }
    InspectorPageAgent* pageAgent() const { return m_pageAgent; }

    // InspectorEnvironment
    bool developerExtrasEnabled() const override;
    bool canAccessInspectedScriptState(JSC::ExecState*) const override;
    Inspector::InspectorFunctionCallHandler functionCallHandler() const override;
    Inspector::InspectorEvaluateHandler evaluateHandler() const override;
    void frontendInitialized() override;
    Ref<WTF::Stopwatch> executionStopwatch() override;
    PageScriptDebugServer& scriptDebugServer() override;
    JSC::VM& vm() override;

private:
    friend class InspectorInstrumentation;

    PageAgentContext pageAgentContext();
    void createLazyAgents();

    Ref<InstrumentingAgents> m_instrumentingAgents;
    std::unique_ptr<WebInjectedScriptManager> m_injectedScriptManager;
    Ref<Inspector::FrontendRouter> m_frontendRouter;
    Ref<Inspector::BackendDispatcher> m_backendDispatcher;
    std::unique_ptr<InspectorOverlay> m_overlay;
    Ref<WTF::Stopwatch> m_executionStopwatch;
    PageScriptDebugServer m_scriptDebugServer;
    Inspector::AgentRegistry m_agents;

    Page& m_page;
    InspectorClient* m_inspectorClient;
    InspectorFrontendClient* m_inspectorFrontendClient { nullptr };

    Inspector::InspectorAgent* m_inspectorAgent { nullptr };
    InspectorDOMAgent* m_domAgent { nullptr };
    InspectorPageAgent* m_pageAgent { nullptr };

    bool m_isUnderTest { false };
    bool m_isAutomaticInspection { false };
    bool m_pauseAfterInitialization = { false };
    bool m_didCreateLazyAgents { false };
};

} // namespace WebCore
