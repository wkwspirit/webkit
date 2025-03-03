/*
 * Copyright (C) 2011 Ericsson AB. All rights reserved.
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013-2018 Apple Inc. All rights reserved.
 * Copyright (C) 2013 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Ericsson nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
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

#if ENABLE(MEDIA_STREAM)

#include "CaptureDevice.h"
#include "Image.h"
#include "MediaConstraints.h"
#include "MediaSample.h"
#include "PlatformLayer.h"
#include "RealtimeMediaSourceCapabilities.h"
#include "RealtimeMediaSourceFactory.h"
#include <wtf/RecursiveLockAdapter.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/Vector.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/WTFString.h>

namespace WTF {
class MediaTime;
}

namespace WebCore {

class AudioStreamDescription;
class FloatRect;
class GraphicsContext;
class MediaStreamPrivate;
class OrientationNotifier;
class PlatformAudioData;
class RealtimeMediaSourceSettings;
class RemoteVideoSample;

struct CaptureSourceOrError;

class WEBCORE_EXPORT RealtimeMediaSource : public ThreadSafeRefCounted<RealtimeMediaSource>, public CanMakeWeakPtr<RealtimeMediaSource> {
public:
    class Observer {
    public:
        virtual ~Observer();

        // Source state changes.
        virtual void sourceStarted() { }
        virtual void sourceStopped() { }
        virtual void sourceMutedChanged() { }
        virtual void sourceSettingsChanged() { }

        // Observer state queries.
        virtual bool preventSourceFromStopping() { return false; }

        // Called on the main thread.
        virtual void videoSampleAvailable(MediaSample&) { }

        // May be called on a background thread.
        virtual void audioSamplesAvailable(const MediaTime&, const PlatformAudioData&, const AudioStreamDescription&, size_t /*numberOfFrames*/) { }
    };

    virtual ~RealtimeMediaSource() = default;

    const String& hashedId() const;
    String deviceIDHashSalt() const;

    const String& persistentID() const { return m_persistentID; }

    enum class Type { None, Audio, Video };
    Type type() const { return m_type; }

    bool isProducingData() const { return m_isProducingData; }
    void start();
    void stop();
    void requestToEnd(Observer& callingObserver);

    bool muted() const { return m_muted; }
    void setMuted(bool);

    bool captureDidFail() const { return m_captureDidFailed; }

    virtual bool interrupted() const { return m_interrupted; }
    virtual void setInterrupted(bool, bool);

    const String& name() const { return m_name; }
    void setName(String&& name) { m_name = WTFMove(name); }

    unsigned fitnessScore() const { return m_fitnessScore; }

    WEBCORE_EXPORT void addObserver(Observer&);
    WEBCORE_EXPORT void removeObserver(Observer&);

    const IntSize size() const;
    void setSize(const IntSize&);

    const IntSize intrinsicSize() const;
    void setIntrinsicSize(const IntSize&);

    double frameRate() const { return m_frameRate; }
    void setFrameRate(double);

    double aspectRatio() const { return m_aspectRatio; }
    void setAspectRatio(double);

    RealtimeMediaSourceSettings::VideoFacingMode facingMode() const { return m_facingMode; }
    void setFacingMode(RealtimeMediaSourceSettings::VideoFacingMode);

    double volume() const { return m_volume; }
    void setVolume(double);

    int sampleRate() const { return m_sampleRate; }
    void setSampleRate(int);
    virtual Optional<Vector<int>> discreteSampleRates() const;

    int sampleSize() const { return m_sampleSize; }
    void setSampleSize(int);
    virtual Optional<Vector<int>> discreteSampleSizes() const;

    bool echoCancellation() const { return m_echoCancellation; }
    void setEchoCancellation(bool);

    virtual const RealtimeMediaSourceCapabilities& capabilities() = 0;
    virtual const RealtimeMediaSourceSettings& settings() = 0;

    struct ApplyConstraintsError {
        String badConstraint;
        String message;
    };
    using ApplyConstraintsHandler = CompletionHandler<void(Optional<ApplyConstraintsError>&&)>;
    virtual void applyConstraints(const MediaConstraints&, ApplyConstraintsHandler&&);
    Optional<ApplyConstraintsError> applyConstraints(const MediaConstraints&);

    bool supportsConstraints(const MediaConstraints&, String&);
    bool supportsConstraint(const MediaConstraint&);

    virtual bool isIsolated() const { return false; }

    virtual bool isCaptureSource() const { return false; }
    virtual CaptureDevice::DeviceType deviceType() const { return CaptureDevice::DeviceType::Unknown; }

    virtual void monitorOrientation(OrientationNotifier&) { }

    virtual void captureFailed();

    virtual bool isIncomingAudioSource() const { return false; }
    virtual bool isIncomingVideoSource() const { return false; }

    void setIsRemote(bool isRemote) { m_isRemote = isRemote; }
    bool isRemote() const { return m_isRemote; }

    // Testing only
    virtual void delaySamples(Seconds) { };
    void setInterruptedForTesting(bool);

protected:
    RealtimeMediaSource(Type, String&& name, String&& deviceID = { }, String&& hashSalt = { });

    void scheduleDeferredTask(WTF::Function<void()>&&);

    virtual void beginConfiguration() { }
    virtual void commitConfiguration() { }

    bool selectSettings(const MediaConstraints&, FlattenedConstraint&, String&);
    double fitnessDistance(const MediaConstraint&);
    void applyConstraint(const MediaConstraint&);
    void applyConstraints(const FlattenedConstraint&);
    bool supportsSizeAndFrameRate(Optional<IntConstraint> width, Optional<IntConstraint> height, Optional<DoubleConstraint>, String&, double& fitnessDistance);

    virtual bool supportsSizeAndFrameRate(Optional<int> width, Optional<int> height, Optional<double>);
    virtual void setSizeAndFrameRate(Optional<int> width, Optional<int> height, Optional<double>);

    void notifyMutedObservers() const;
    void notifyMutedChange(bool muted);
    void notifySettingsDidChangeObservers(OptionSet<RealtimeMediaSourceSettings::Flag>);

    void initializeVolume(double volume) { m_volume = volume; }
    void initializeSampleRate(int sampleRate) { m_sampleRate = sampleRate; }
    void initializeEchoCancellation(bool echoCancellation) { m_echoCancellation = echoCancellation; }

    void videoSampleAvailable(MediaSample&);
    void audioSamplesAvailable(const MediaTime&, const PlatformAudioData&, const AudioStreamDescription&, size_t);

private:
    virtual void startProducingData() { }
    virtual void stopProducingData() { }
    virtual void settingsDidChange(OptionSet<RealtimeMediaSourceSettings::Flag>) { }

    virtual void hasEnded() { }

    void forEachObserver(const WTF::Function<void(Observer&)>&) const;

    bool m_muted { false };

    String m_idHashSalt;
    String m_hashedID;
    String m_persistentID;
    Type m_type;
    String m_name;
    mutable RecursiveLock m_observersLock;
    HashSet<Observer*> m_observers;
    IntSize m_size;
    IntSize m_intrinsicSize;
    double m_frameRate { 30 };
    double m_aspectRatio { 0 };
    double m_volume { 1 };
    double m_sampleRate { 0 };
    double m_sampleSize { 0 };
    double m_fitnessScore { 0 };
    RealtimeMediaSourceSettings::VideoFacingMode m_facingMode { RealtimeMediaSourceSettings::User};

    bool m_pendingSettingsDidChangeNotification { false };
    bool m_echoCancellation { false };
    bool m_isProducingData { false };
    bool m_interrupted { false };
    bool m_captureDidFailed { false };
    bool m_isRemote { false };
    bool m_isEnded { false };
};

struct CaptureSourceOrError {
    CaptureSourceOrError() = default;
    CaptureSourceOrError(Ref<RealtimeMediaSource>&& source) : captureSource(WTFMove(source)) { }
    CaptureSourceOrError(String&& message) : errorMessage(WTFMove(message)) { }
    
    operator bool()  const { return !!captureSource; }
    Ref<RealtimeMediaSource> source() { return captureSource.releaseNonNull(); }
    
    RefPtr<RealtimeMediaSource> captureSource;
    String errorMessage;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
