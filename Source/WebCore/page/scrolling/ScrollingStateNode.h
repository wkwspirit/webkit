/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
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

#if ENABLE(ASYNC_SCROLLING) || USE(COORDINATED_GRAPHICS)

#include "GraphicsLayer.h"
#include "ScrollingCoordinator.h"
#include <stdint.h>
#include <wtf/RefCounted.h>
#include <wtf/TypeCasts.h>
#include <wtf/Vector.h>

namespace WTF {
class TextStream;
}

namespace WebCore {

class GraphicsLayer;
class ScrollingStateTree;

// Used to allow ScrollingStateNodes to refer to layers in various contexts:
// a) Async scrolling, main thread: ScrollingStateNode holds onto a GraphicsLayer, and uses m_layerID
//    to detect whether that GraphicsLayer's underlying PlatformLayer changed.
// b) Threaded scrolling, commit to scrolling thread: ScrollingStateNode wraps a PlatformLayer, which
//    can be passed to the Scrolling Thread
// c) Remote scrolling UI process, where LayerRepresentation wraps just a PlatformLayerID.
class LayerRepresentation {
public:
    enum Type {
        EmptyRepresentation,
        GraphicsLayerRepresentation,
        PlatformLayerRepresentation,
        PlatformLayerIDRepresentation
    };

    LayerRepresentation()
        : m_graphicsLayer(nullptr)
        , m_layerID(0)
        , m_representation(EmptyRepresentation)
    { }

    LayerRepresentation(GraphicsLayer* graphicsLayer)
        : m_graphicsLayer(graphicsLayer)
        , m_layerID(graphicsLayer ? graphicsLayer->primaryLayerID() : 0)
        , m_representation(GraphicsLayerRepresentation)
    { }

    LayerRepresentation(PlatformLayer* platformLayer)
        : m_typelessPlatformLayer(makePlatformLayerTypeless(platformLayer))
        , m_layerID(0)
        , m_representation(PlatformLayerRepresentation)
    {
        retainPlatformLayer(m_typelessPlatformLayer);
    }

    LayerRepresentation(GraphicsLayer::PlatformLayerID layerID)
        : m_graphicsLayer(nullptr)
        , m_layerID(layerID)
        , m_representation(PlatformLayerIDRepresentation)
    {
    }

    LayerRepresentation(const LayerRepresentation& other)
        : m_typelessPlatformLayer(other.m_typelessPlatformLayer)
        , m_layerID(other.m_layerID)
        , m_representation(other.m_representation)
    {
        if (m_representation == PlatformLayerRepresentation)
            retainPlatformLayer(m_typelessPlatformLayer);
    }

    ~LayerRepresentation()
    {
        if (m_representation == PlatformLayerRepresentation)
            releasePlatformLayer(m_typelessPlatformLayer);
    }

    operator GraphicsLayer*() const
    {
        ASSERT(m_representation == GraphicsLayerRepresentation);
        return m_graphicsLayer;
    }

    operator PlatformLayer*() const
    {
        ASSERT(m_representation == PlatformLayerRepresentation);
        return makePlatformLayerTyped(m_typelessPlatformLayer);
    }
    
    GraphicsLayer::PlatformLayerID layerID() const
    {
        return m_layerID;
    }

    operator GraphicsLayer::PlatformLayerID() const
    {
        ASSERT(m_representation != PlatformLayerRepresentation);
        return m_layerID;
    }

    LayerRepresentation& operator=(const LayerRepresentation& other)
    {
        m_typelessPlatformLayer = other.m_typelessPlatformLayer;
        m_layerID = other.m_layerID;
        m_representation = other.m_representation;

        if (m_representation == PlatformLayerRepresentation)
            retainPlatformLayer(m_typelessPlatformLayer);

        return *this;
    }

    bool operator==(const LayerRepresentation& other) const
    {
        if (m_representation != other.m_representation)
            return false;
        switch (m_representation) {
        case EmptyRepresentation:
            return true;
        case GraphicsLayerRepresentation:
            return m_graphicsLayer == other.m_graphicsLayer
                && m_layerID == other.m_layerID;
        case PlatformLayerRepresentation:
            return m_typelessPlatformLayer == other.m_typelessPlatformLayer;
        case PlatformLayerIDRepresentation:
            return m_layerID == other.m_layerID;
        }
        ASSERT_NOT_REACHED();
        return true;
    }
    
    LayerRepresentation toRepresentation(Type representation) const
    {
        switch (representation) {
        case EmptyRepresentation:
            return LayerRepresentation();
        case GraphicsLayerRepresentation:
            ASSERT(m_representation == GraphicsLayerRepresentation);
            return *this;
        case PlatformLayerRepresentation:
            return m_graphicsLayer ? m_graphicsLayer->platformLayer() : nullptr;
        case PlatformLayerIDRepresentation:
            return LayerRepresentation(m_layerID);
        }
        return LayerRepresentation();
    }

    bool representsGraphicsLayer() const { return m_representation == GraphicsLayerRepresentation; }
    bool representsPlatformLayerID() const { return m_representation == PlatformLayerIDRepresentation; }
    
private:
    WEBCORE_EXPORT static void retainPlatformLayer(void* typelessPlatformLayer);
    WEBCORE_EXPORT static void releasePlatformLayer(void* typelessPlatformLayer);
    WEBCORE_EXPORT static PlatformLayer* makePlatformLayerTyped(void* typelessPlatformLayer);
    WEBCORE_EXPORT static void* makePlatformLayerTypeless(PlatformLayer*);

    union {
        GraphicsLayer* m_graphicsLayer;
        void* m_typelessPlatformLayer;
    };

    GraphicsLayer::PlatformLayerID m_layerID;
    Type m_representation;
};

class ScrollingStateNode : public RefCounted<ScrollingStateNode> {
    WTF_MAKE_FAST_ALLOCATED;
public:
    ScrollingStateNode(ScrollingNodeType, ScrollingStateTree&, ScrollingNodeID);
    virtual ~ScrollingStateNode();
    
    ScrollingNodeType nodeType() const { return m_nodeType; }

    bool isFixedNode() const { return m_nodeType == ScrollingNodeType::Fixed; }
    bool isStickyNode() const { return m_nodeType == ScrollingNodeType::Sticky; }
    bool isScrollingNode() const { return isFrameScrollingNode() || isOverflowScrollingNode(); }
    bool isFrameScrollingNode() const { return m_nodeType == ScrollingNodeType::MainFrame || m_nodeType == ScrollingNodeType::Subframe; }
    bool isFrameHostingNode() const { return m_nodeType == ScrollingNodeType::FrameHosting; }
    bool isOverflowScrollingNode() const { return m_nodeType == ScrollingNodeType::Overflow; }

    virtual Ref<ScrollingStateNode> clone(ScrollingStateTree& adoptiveTree) = 0;
    Ref<ScrollingStateNode> cloneAndReset(ScrollingStateTree& adoptiveTree);
    void cloneAndResetChildren(ScrollingStateNode&, ScrollingStateTree& adoptiveTree);

    // FIXME: using an OptionSet<> for these and derived class bits would simplify code.
    enum {
        Layer = 0,
        ChildNodes,
        NumStateNodeBits // This must remain at the last position.
    };
    typedef uint64_t ChangedProperties;

    bool hasChangedProperties() const { return m_changedProperties; }
    bool hasChangedProperty(unsigned propertyBit) const { return m_changedProperties & (static_cast<ChangedProperties>(1) << propertyBit); }
    void resetChangedProperties() { m_changedProperties = 0; }
    void setPropertyChanged(unsigned propertyBit);
    virtual void setAllPropertiesChanged();

    ChangedProperties changedProperties() const { return m_changedProperties; }
    void setChangedProperties(ChangedProperties changedProperties) { m_changedProperties = changedProperties; }
    
    virtual void reconcileLayerPositionForViewportRect(const LayoutRect& /*viewportRect*/, ScrollingLayerPositionAction);

    const LayerRepresentation& layer() const { return m_layer; }
    WEBCORE_EXPORT void setLayer(const LayerRepresentation&);

    ScrollingStateTree& scrollingStateTree() const { return m_scrollingStateTree; }

    ScrollingNodeID scrollingNodeID() const { return m_nodeID; }

    ScrollingStateNode* parent() const { return m_parent; }
    void setParent(ScrollingStateNode* parent) { m_parent = parent; }
    ScrollingNodeID parentNodeID() const { return m_parent ? m_parent->scrollingNodeID() : 0; }

    Vector<RefPtr<ScrollingStateNode>>* children() const { return m_children.get(); }
    std::unique_ptr<Vector<RefPtr<ScrollingStateNode>>> takeChildren() { return WTFMove(m_children); }

    void appendChild(Ref<ScrollingStateNode>&&);
    void insertChild(Ref<ScrollingStateNode>&&, size_t index);

    // Note that node ownership is via the parent, so these functions can trigger node deletion.
    void removeFromParent();
    void removeChildAtIndex(size_t index);
    void removeChild(ScrollingStateNode&);

    size_t indexOfChild(ScrollingStateNode&) const;

    String scrollingStateTreeAsText(ScrollingStateTreeAsTextBehavior = ScrollingStateTreeAsTextBehaviorNormal) const;

protected:
    ScrollingStateNode(const ScrollingStateNode&, ScrollingStateTree&);

    virtual void dumpProperties(WTF::TextStream&, ScrollingStateTreeAsTextBehavior) const;

    inline void setPropertyChangedBit(unsigned propertyBit);

private:
    void dump(WTF::TextStream&, ScrollingStateTreeAsTextBehavior) const;

    const ScrollingNodeType m_nodeType;
    const ScrollingNodeID m_nodeID;
    ChangedProperties m_changedProperties { 0 };

    ScrollingStateTree& m_scrollingStateTree;

    ScrollingStateNode* m_parent { nullptr };
    std::unique_ptr<Vector<RefPtr<ScrollingStateNode>>> m_children;

    LayerRepresentation m_layer;
};

void ScrollingStateNode::setPropertyChangedBit(unsigned propertyBit)
{
    m_changedProperties |= (static_cast<ChangedProperties>(1) << propertyBit);
}

} // namespace WebCore

#define SPECIALIZE_TYPE_TRAITS_SCROLLING_STATE_NODE(ToValueTypeName, predicate) \
SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::ToValueTypeName) \
    static bool isType(const WebCore::ScrollingStateNode& node) { return node.predicate; } \
SPECIALIZE_TYPE_TRAITS_END()

#endif // ENABLE(ASYNC_SCROLLING) || USE(COORDINATED_GRAPHICS)
