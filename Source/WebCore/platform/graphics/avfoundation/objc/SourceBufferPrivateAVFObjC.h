/*
 * Copyright (C) 2013-2017 Apple Inc. All rights reserved.
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

#if ENABLE(MEDIA_SOURCE) && USE(AVFOUNDATION)

#include "SourceBufferPrivate.h"
#include <dispatch/group.h>
#include <wtf/Box.h>
#include <wtf/Deque.h>
#include <wtf/HashMap.h>
#include <wtf/LoggerHelper.h>
#include <wtf/MediaTime.h>
#include <wtf/OSObjectPtr.h>
#include <wtf/RefPtr.h>
#include <wtf/RetainPtr.h>
#include <wtf/Vector.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/AtomicString.h>
#include <wtf/threads/BinarySemaphore.h>

OBJC_CLASS AVAsset;
OBJC_CLASS AVStreamDataParser;
OBJC_CLASS AVSampleBufferAudioRenderer;
OBJC_CLASS AVSampleBufferDisplayLayer;
OBJC_CLASS NSData;
OBJC_CLASS NSError;
OBJC_CLASS NSObject;
OBJC_CLASS WebAVStreamDataParserListener;
OBJC_CLASS WebAVSampleBufferErrorListener;

typedef struct opaqueCMSampleBuffer *CMSampleBufferRef;
typedef const struct opaqueCMFormatDescription *CMFormatDescriptionRef;

namespace WebCore {

class CDMInstance;
class CDMInstanceFairPlayStreamingAVFObjC;
class CDMSessionMediaSourceAVFObjC;
class MediaSourcePrivateAVFObjC;
class TimeRanges;
class AudioTrackPrivate;
class VideoTrackPrivate;
class AudioTrackPrivateMediaSourceAVFObjC;
class VideoTrackPrivateMediaSourceAVFObjC;
class WebCoreDecompressionSession;
class SharedBuffer;

class SourceBufferPrivateAVFObjCErrorClient {
public:
    virtual ~SourceBufferPrivateAVFObjCErrorClient() = default;
    virtual void layerDidReceiveError(AVSampleBufferDisplayLayer *, NSError *, bool& shouldIgnore) = 0;
    ALLOW_NEW_API_WITHOUT_GUARDS_BEGIN
    virtual void rendererDidReceiveError(AVSampleBufferAudioRenderer *, NSError *, bool& shouldIgnore) = 0;
    ALLOW_NEW_API_WITHOUT_GUARDS_END
};

class SourceBufferPrivateAVFObjC final
    : public SourceBufferPrivate
    , public CanMakeWeakPtr<SourceBufferPrivateAVFObjC>
#if !RELEASE_LOG_DISABLED
    , private LoggerHelper
#endif
{
public:
    static Ref<SourceBufferPrivateAVFObjC> create(MediaSourcePrivateAVFObjC*);
    virtual ~SourceBufferPrivateAVFObjC();

    void clearMediaSource() { m_mediaSource = nullptr; }

    // AVStreamDataParser delegate methods
    void didParseStreamDataAsAsset(AVAsset*);
    void didFailToParseStreamDataWithError(NSError*);
    void didProvideMediaDataForTrackID(int trackID, CMSampleBufferRef, const String& mediaType, unsigned flags);
    void didReachEndOfTrackWithTrackID(int trackID, const String& mediaType);
    void willProvideContentKeyRequestInitializationDataForTrackID(int trackID);
    void didProvideContentKeyRequestInitializationDataForTrackID(NSData*, int trackID, Box<BinarySemaphore>);

    bool processCodedFrame(int trackID, CMSampleBufferRef, const String& mediaType);

    bool hasVideo() const;
    bool hasSelectedVideo() const;
    bool hasAudio() const;

    void trackDidChangeEnabled(VideoTrackPrivateMediaSourceAVFObjC*);
    void trackDidChangeEnabled(AudioTrackPrivateMediaSourceAVFObjC*);

    void willSeek();
    MediaTime fastSeekTimeForMediaTime(const MediaTime&, const MediaTime& negativeThreshold, const MediaTime& positiveThreshold);
    FloatSize naturalSize();

    int protectedTrackID() const { return m_protectedTrackID; }
    AVStreamDataParser* parser() const { return m_parser.get(); }
    void setCDMSession(CDMSessionMediaSourceAVFObjC*);
    void setCDMInstance(CDMInstance*);
    void attemptToDecrypt();
    bool waitingForKey() const { return m_waitingForKey; }

    void flush();

    void registerForErrorNotifications(SourceBufferPrivateAVFObjCErrorClient*);
    void unregisterForErrorNotifications(SourceBufferPrivateAVFObjCErrorClient*);
    void layerDidReceiveError(AVSampleBufferDisplayLayer *, NSError *);
    void outputObscuredDueToInsufficientExternalProtectionChanged(bool);
    ALLOW_NEW_API_WITHOUT_GUARDS_BEGIN
    void rendererDidReceiveError(AVSampleBufferAudioRenderer *, NSError *);
    ALLOW_NEW_API_WITHOUT_GUARDS_END

    void setVideoLayer(AVSampleBufferDisplayLayer*);
    void setDecompressionSession(WebCoreDecompressionSession*);

    void bufferWasConsumed();

#if !RELEASE_LOG_DISABLED
    const Logger& logger() const final { return m_logger.get(); }
    const char* logClassName() const override { return "SourceBufferPrivateAVFObjC"; }
    const void* logIdentifier() const final { return m_logIdentifier; }
    WTFLogChannel& logChannel() const final;
    const Logger& sourceBufferLogger() const final { return m_logger.get(); }
    const void* sourceBufferLogIdentifier() final { return logIdentifier(); }
#endif

private:
    explicit SourceBufferPrivateAVFObjC(MediaSourcePrivateAVFObjC*);

    // SourceBufferPrivate overrides
    void setClient(SourceBufferPrivateClient*) final;
    void append(Vector<unsigned char>&&) final;
    void abort() final;
    void resetParserState() final;
    void removedFromMediaSource() final;
    MediaPlayer::ReadyState readyState() const final;
    void setReadyState(MediaPlayer::ReadyState) final;
    void flush(const AtomicString& trackID) final;
    void enqueueSample(Ref<MediaSample>&&, const AtomicString& trackID) final;
    bool isReadyForMoreSamples(const AtomicString& trackID) final;
    void setActive(bool) final;
    void notifyClientWhenReadyForMoreSamples(const AtomicString& trackID) final;
    bool canSwitchToType(const ContentType&) final;

    void didBecomeReadyForMoreSamples(int trackID);
    void appendCompleted();
    void destroyParser();
    void destroyRenderers();

    void flushVideo();
    ALLOW_NEW_API_WITHOUT_GUARDS_BEGIN
    void flush(AVSampleBufferAudioRenderer *);
    ALLOW_NEW_API_WITHOUT_GUARDS_END

    WeakPtr<SourceBufferPrivateAVFObjC> createWeakPtr() { return m_weakFactory.createWeakPtr(*this); }

    Vector<RefPtr<VideoTrackPrivateMediaSourceAVFObjC>> m_videoTracks;
    Vector<RefPtr<AudioTrackPrivateMediaSourceAVFObjC>> m_audioTracks;
    Vector<SourceBufferPrivateAVFObjCErrorClient*> m_errorClients;

    WeakPtrFactory<SourceBufferPrivateAVFObjC> m_weakFactory;
    WeakPtrFactory<SourceBufferPrivateAVFObjC> m_appendWeakFactory;

    RetainPtr<AVStreamDataParser> m_parser;
    RetainPtr<AVAsset> m_asset;
    RetainPtr<AVSampleBufferDisplayLayer> m_displayLayer;
    ALLOW_NEW_API_WITHOUT_GUARDS_BEGIN
    HashMap<int, RetainPtr<AVSampleBufferAudioRenderer>> m_audioRenderers;
    ALLOW_NEW_API_WITHOUT_GUARDS_END
    RetainPtr<WebAVStreamDataParserListener> m_delegate;
    RetainPtr<WebAVSampleBufferErrorListener> m_errorListener;
    RetainPtr<NSError> m_hdcpError;
    Box<BinarySemaphore> m_hasSessionSemaphore;
    OSObjectPtr<dispatch_group_t> m_isAppendingGroup;
    RefPtr<WebCoreDecompressionSession> m_decompressionSession;

    MediaSourcePrivateAVFObjC* m_mediaSource;
    SourceBufferPrivateClient* m_client { nullptr };
#if ENABLE(LEGACY_ENCRYPTED_MEDIA)
    WeakPtr<CDMSessionMediaSourceAVFObjC> m_session { nullptr };
#endif
#if ENABLE(ENCRYPTED_MEDIA) && HAVE(AVCONTENTKEYSESSION)
    RefPtr<CDMInstanceFairPlayStreamingAVFObjC> m_cdmInstance;
    Vector<Ref<SharedBuffer>> m_keyIDs;
#endif

    Optional<FloatSize> m_cachedSize;
    FloatSize m_currentSize;
    bool m_parsingSucceeded { true };
    bool m_parserStateWasReset { false };
    bool m_discardSamplesUntilNextInitializationSegment { false };
    bool m_waitingForKey { true };
    int m_enabledVideoTrackID { -1 };
    int m_protectedTrackID { -1 };
    uint64_t m_mapID;

#if !RELEASE_LOG_DISABLED
    Ref<const Logger> m_logger;
    const void* m_logIdentifier;
#endif
};

}

#endif // ENABLE(MEDIA_SOURCE) && USE(AVFOUNDATION)
