/*
 * Copyright (C) 2014-2017 Apple Inc. All rights reserved.
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

#import "config.h"
#import "MediaSessionManagerIOS.h"

#if PLATFORM(IOS_FAMILY)

#import "Logging.h"
#import "MediaPlayer.h"
#import "PlatformMediaSession.h"
#import "RuntimeApplicationChecks.h"
#import "SystemMemory.h"
#import "WebCoreThreadRun.h"
#import <AVFoundation/AVAudioSession.h>
#import <AVFoundation/AVRouteDetector.h>
#import <objc/runtime.h>
#import <pal/ios/UIKitSoftLink.h>
#import <pal/spi/ios/CelestialSPI.h>
#import <pal/spi/ios/UIKitSPI.h>
#import <wtf/BlockObjCExceptions.h>
#import <wtf/MainThread.h>
#import <wtf/RAMSize.h>
#import <wtf/RetainPtr.h>

SOFT_LINK_FRAMEWORK(AVFoundation)
SOFT_LINK_CLASS(AVFoundation, AVAudioSession)
SOFT_LINK_CONSTANT(AVFoundation, AVAudioSessionInterruptionNotification, NSString *)
SOFT_LINK_CONSTANT(AVFoundation, AVAudioSessionInterruptionTypeKey, NSString *)
SOFT_LINK_CONSTANT(AVFoundation, AVAudioSessionInterruptionOptionKey, NSString *)
SOFT_LINK_CONSTANT(AVFoundation, AVRouteDetectorMultipleRoutesDetectedDidChangeNotification, NSString *)

#if HAVE(CELESTIAL)
SOFT_LINK_PRIVATE_FRAMEWORK_OPTIONAL(Celestial)
SOFT_LINK_CLASS_OPTIONAL(Celestial, AVSystemController)
SOFT_LINK_CONSTANT_MAY_FAIL(Celestial, AVSystemController_PIDToInheritApplicationStateFrom, NSString *)
#endif

#if HAVE(MEDIA_PLAYER) && !PLATFORM(WATCHOS)
SOFT_LINK_CLASS(AVFoundation, AVRouteDetector)
#endif

#define AVAudioSession getAVAudioSessionClass()
#define AVAudioSessionInterruptionNotification getAVAudioSessionInterruptionNotification()
#define AVAudioSessionInterruptionTypeKey getAVAudioSessionInterruptionTypeKey()
#define AVAudioSessionInterruptionOptionKey getAVAudioSessionInterruptionOptionKey()

WEBCORE_EXPORT NSString* WebUIApplicationWillResignActiveNotification = @"WebUIApplicationWillResignActiveNotification";
WEBCORE_EXPORT NSString* WebUIApplicationWillEnterForegroundNotification = @"WebUIApplicationWillEnterForegroundNotification";
WEBCORE_EXPORT NSString* WebUIApplicationDidBecomeActiveNotification = @"WebUIApplicationDidBecomeActiveNotification";
WEBCORE_EXPORT NSString* WebUIApplicationDidEnterBackgroundNotification = @"WebUIApplicationDidEnterBackgroundNotification";

using namespace WebCore;

@interface WebMediaSessionHelper : NSObject {
    MediaSessionManageriOS* _callback;

#if HAVE(MEDIA_PLAYER) && !PLATFORM(WATCHOS)
    RetainPtr<AVRouteDetector> _routeDetector;
#endif
    bool _monitoringAirPlayRoutes;
    bool _startMonitoringAirPlayRoutesPending;
}

- (id)initWithCallback:(MediaSessionManageriOS*)callback;

- (void)clearCallback;
- (void)interruption:(NSNotification *)notification;
- (void)applicationWillEnterForeground:(NSNotification *)notification;
- (void)applicationWillResignActive:(NSNotification *)notification;
- (void)applicationDidEnterBackground:(NSNotification *)notification;
- (BOOL)hasWirelessTargetsAvailable;

#if HAVE(MEDIA_PLAYER) && !PLATFORM(WATCHOS)
- (void)startMonitoringAirPlayRoutes;
- (void)stopMonitoringAirPlayRoutes;
#endif

@end

namespace WebCore {

static MediaSessionManageriOS* platformMediaSessionManager = nullptr;

PlatformMediaSessionManager& PlatformMediaSessionManager::sharedManager()
{
    if (!platformMediaSessionManager)
        platformMediaSessionManager = new MediaSessionManageriOS;
    return *platformMediaSessionManager;
}

PlatformMediaSessionManager* PlatformMediaSessionManager::sharedManagerIfExists()
{
    return platformMediaSessionManager;
}

MediaSessionManageriOS::MediaSessionManageriOS()
    : MediaSessionManagerCocoa()
{
    BEGIN_BLOCK_OBJC_EXCEPTIONS
    m_objcObserver = adoptNS([[WebMediaSessionHelper alloc] initWithCallback:this]);
    END_BLOCK_OBJC_EXCEPTIONS
    resetRestrictions();
}

MediaSessionManageriOS::~MediaSessionManageriOS()
{
    BEGIN_BLOCK_OBJC_EXCEPTIONS
    [m_objcObserver clearCallback];
    m_objcObserver = nil;
    END_BLOCK_OBJC_EXCEPTIONS
}

void MediaSessionManageriOS::resetRestrictions()
{
    static const size_t systemMemoryRequiredForVideoInBackgroundTabs = 1024 * 1024 * 1024;

    LOG(Media, "MediaSessionManageriOS::resetRestrictions");

    PlatformMediaSessionManager::resetRestrictions();

    if (ramSize() < systemMemoryRequiredForVideoInBackgroundTabs) {
        LOG(Media, "MediaSessionManageriOS::resetRestrictions - restricting video in background tabs because system memory = %zul", ramSize());
        addRestriction(PlatformMediaSession::Video, BackgroundTabPlaybackRestricted);
    }

    addRestriction(PlatformMediaSession::Video, BackgroundProcessPlaybackRestricted);
    addRestriction(PlatformMediaSession::VideoAudio, ConcurrentPlaybackNotPermitted | BackgroundProcessPlaybackRestricted | SuspendedUnderLockPlaybackRestricted);
}

bool MediaSessionManageriOS::hasWirelessTargetsAvailable()
{
    BEGIN_BLOCK_OBJC_EXCEPTIONS
    return [m_objcObserver hasWirelessTargetsAvailable];
    END_BLOCK_OBJC_EXCEPTIONS
}

void MediaSessionManageriOS::configureWireLessTargetMonitoring()
{
#if HAVE(MEDIA_PLAYER) && !PLATFORM(WATCHOS)
    bool requiresMonitoring = anyOfSessions([] (PlatformMediaSession& session, size_t) {
        return session.requiresPlaybackTargetRouteMonitoring();
    });

    LOG(Media, "MediaSessionManageriOS::configureWireLessTargetMonitoring - requiresMonitoring = %s", requiresMonitoring ? "true" : "false");

    BEGIN_BLOCK_OBJC_EXCEPTIONS

    if (requiresMonitoring)
        [m_objcObserver startMonitoringAirPlayRoutes];
    else
        [m_objcObserver stopMonitoringAirPlayRoutes];

    END_BLOCK_OBJC_EXCEPTIONS
#endif
}

void MediaSessionManageriOS::providePresentingApplicationPIDIfNecessary()
{
#if HAVE(CELESTIAL)
    if (m_havePresentedApplicationPID)
        return;
    m_havePresentedApplicationPID = true;

    if (!canLoadAVSystemController_PIDToInheritApplicationStateFrom())
        return;

    NSError *error = nil;
    [[getAVSystemControllerClass() sharedAVSystemController] setAttribute:@(presentingApplicationPID()) forKey:getAVSystemController_PIDToInheritApplicationStateFrom() error:&error];
    if (error)
        WTFLogAlways("Failed to set up PID proxying: %s", error.localizedDescription.UTF8String);
#endif
}

void MediaSessionManageriOS::externalOutputDeviceAvailableDidChange()
{
    BEGIN_BLOCK_OBJC_EXCEPTIONS
    forEachSession([haveTargets = [m_objcObserver hasWirelessTargetsAvailable]] (PlatformMediaSession& session, size_t) {
        session.externalOutputDeviceAvailableDidChange(haveTargets);
    });
    END_BLOCK_OBJC_EXCEPTIONS
}

} // namespace WebCore

@implementation WebMediaSessionHelper

- (id)initWithCallback:(MediaSessionManageriOS*)callback
{
    LOG(Media, "-[WebMediaSessionHelper initWithCallback]");

    if (!(self = [super init]))
        return nil;
    
    _callback = callback;

    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center addObserver:self selector:@selector(interruption:) name:AVAudioSessionInterruptionNotification object:[AVAudioSession sharedInstance]];

    [center addObserver:self selector:@selector(applicationWillEnterForeground:) name:PAL::get_UIKit_UIApplicationWillEnterForegroundNotification() object:nil];
    [center addObserver:self selector:@selector(applicationWillEnterForeground:) name:WebUIApplicationWillEnterForegroundNotification object:nil];
    [center addObserver:self selector:@selector(applicationDidBecomeActive:) name:PAL::get_UIKit_UIApplicationDidBecomeActiveNotification() object:nil];
    [center addObserver:self selector:@selector(applicationDidBecomeActive:) name:WebUIApplicationDidBecomeActiveNotification object:nil];
    [center addObserver:self selector:@selector(applicationWillResignActive:) name:PAL::get_UIKit_UIApplicationWillResignActiveNotification() object:nil];
    [center addObserver:self selector:@selector(applicationWillResignActive:) name:WebUIApplicationWillResignActiveNotification object:nil];
    [center addObserver:self selector:@selector(applicationDidEnterBackground:) name:PAL::get_UIKit_UIApplicationDidEnterBackgroundNotification() object:nil];
    [center addObserver:self selector:@selector(applicationDidEnterBackground:) name:WebUIApplicationDidEnterBackgroundNotification object:nil];

    // Now playing won't work unless we turn on the delivery of remote control events.
    dispatch_async(dispatch_get_main_queue(), ^ {
        BEGIN_BLOCK_OBJC_EXCEPTIONS
        [[PAL::getUIApplicationClass() sharedApplication] beginReceivingRemoteControlEvents];
        END_BLOCK_OBJC_EXCEPTIONS
    });

    return self;
}

- (void)dealloc
{
    LOG(Media, "-[WebMediaSessionHelper dealloc]");

#if HAVE(MEDIA_PLAYER) && !PLATFORM(WATCHOS)
    if (!pthread_main_np()) {
        dispatch_async(dispatch_get_main_queue(), [routeDetector = WTFMove(_routeDetector)] () mutable {
            LOG(Media, "safelyTearDown - dipatched to UI thread.");
            BEGIN_BLOCK_OBJC_EXCEPTIONS
            routeDetector.get().routeDetectionEnabled = NO;
            routeDetector.clear();
            END_BLOCK_OBJC_EXCEPTIONS
        });
    } else
        _routeDetector.get().routeDetectionEnabled = NO;
#endif

    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [super dealloc];
}

- (void)clearCallback
{
    LOG(Media, "-[WebMediaSessionHelper clearCallback]");
    _callback = nil;
}

- (BOOL)hasWirelessTargetsAvailable
{
    LOG(Media, "-[WebMediaSessionHelper hasWirelessTargetsAvailable]");
#if HAVE(MEDIA_PLAYER) && !PLATFORM(WATCHOS)
    return _routeDetector.get().multipleRoutesDetected;
#else
    return NO;
#endif
}

#if HAVE(MEDIA_PLAYER) && !PLATFORM(WATCHOS)
- (void)startMonitoringAirPlayRoutes
{
    if (_monitoringAirPlayRoutes)
        return;

    _monitoringAirPlayRoutes = true;

    if (_startMonitoringAirPlayRoutesPending)
        return;

    if (_routeDetector) {
        _routeDetector.get().routeDetectionEnabled = YES;
        return;
    }

    _startMonitoringAirPlayRoutesPending = true;

    LOG(Media, "-[WebMediaSessionHelper startMonitoringAirPlayRoutes]");

    callOnWebThreadOrDispatchAsyncOnMainThread([protectedSelf = WTFMove(self)]() mutable {
        ASSERT(!protectedSelf->_routeDetector);

        if (protectedSelf->_callback) {
            BEGIN_BLOCK_OBJC_EXCEPTIONS
            protectedSelf->_routeDetector = adoptNS([allocAVRouteDetectorInstance() init]);
            protectedSelf->_routeDetector.get().routeDetectionEnabled = protectedSelf->_monitoringAirPlayRoutes;
            [[NSNotificationCenter defaultCenter] addObserver:protectedSelf selector:@selector(wirelessRoutesAvailableDidChange:) name:getAVRouteDetectorMultipleRoutesDetectedDidChangeNotification() object:protectedSelf->_routeDetector.get()];
            END_BLOCK_OBJC_EXCEPTIONS
        }

        protectedSelf->_startMonitoringAirPlayRoutesPending = false;
    });
}

- (void)stopMonitoringAirPlayRoutes
{
    if (!_monitoringAirPlayRoutes)
        return;

    LOG(Media, "-[WebMediaSessionHelper stopMonitoringAirPlayRoutes]");

    _monitoringAirPlayRoutes = false;
    _routeDetector.get().routeDetectionEnabled = NO;
}
#endif // HAVE(MEDIA_PLAYER) && !PLATFORM(WATCHOS)

- (void)interruption:(NSNotification *)notification
{
    if (!_callback || _callback->willIgnoreSystemInterruptions())
        return;

    NSUInteger type = [[[notification userInfo] objectForKey:AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
    PlatformMediaSession::EndInterruptionFlags flags = PlatformMediaSession::NoFlags;

    LOG(Media, "-[WebMediaSessionHelper interruption] - type = %i", (int)type);

    if (type == AVAudioSessionInterruptionTypeEnded && [[[notification userInfo] objectForKey:AVAudioSessionInterruptionOptionKey] unsignedIntegerValue] == AVAudioSessionInterruptionOptionShouldResume)
        flags = PlatformMediaSession::MayResumePlaying;

    callOnWebThreadOrDispatchAsyncOnMainThread([protectedSelf = WTFMove(self), type, flags]() mutable {
        auto* callback = protectedSelf->_callback;
        if (!callback)
            return;

        if (type == AVAudioSessionInterruptionTypeBegan)
            callback->beginInterruption(PlatformMediaSession::SystemInterruption);
        else
            callback->endInterruption(flags);

    });
}

- (void)applicationWillEnterForeground:(NSNotification *)notification
{
    UNUSED_PARAM(notification);

    if (!_callback || _callback->willIgnoreSystemInterruptions())
        return;

    LOG(Media, "-[WebMediaSessionHelper applicationWillEnterForeground]");

    BOOL isSuspendedUnderLock = [[[notification userInfo] objectForKey:@"isSuspendedUnderLock"] boolValue];
    callOnWebThreadOrDispatchAsyncOnMainThread([protectedSelf = WTFMove(self), isSuspendedUnderLock]() mutable {
        if (auto* callback = protectedSelf->_callback)
            callback->applicationWillEnterForeground(isSuspendedUnderLock);
    });
}

- (void)applicationDidBecomeActive:(NSNotification *)notification
{
    UNUSED_PARAM(notification);

    if (!_callback || _callback->willIgnoreSystemInterruptions())
        return;

    LOG(Media, "-[WebMediaSessionHelper applicationDidBecomeActive]");

    callOnWebThreadOrDispatchAsyncOnMainThread([protectedSelf = WTFMove(self)]() mutable {
        if (auto* callback = protectedSelf->_callback)
            callback->applicationDidBecomeActive();
    });
}

- (void)applicationWillResignActive:(NSNotification *)notification
{
    UNUSED_PARAM(notification);

    if (!_callback || _callback->willIgnoreSystemInterruptions())
        return;

    LOG(Media, "-[WebMediaSessionHelper applicationWillResignActive]");

    callOnWebThreadOrDispatchAsyncOnMainThread([protectedSelf = WTFMove(self)]() mutable {
        if (auto* callback = protectedSelf->_callback)
            callback->applicationWillBecomeInactive();
    });
}

- (void)wirelessRoutesAvailableDidChange:(NSNotification *)notification
{
    UNUSED_PARAM(notification);

    if (!_callback || !_monitoringAirPlayRoutes)
        return;

    LOG(Media, "-[WebMediaSessionHelper wirelessRoutesAvailableDidChange]");

    callOnWebThreadOrDispatchAsyncOnMainThread([protectedSelf = WTFMove(self)]() mutable {
        if (auto* callback = protectedSelf->_callback)
            callback->externalOutputDeviceAvailableDidChange();
    });
}

- (void)applicationDidEnterBackground:(NSNotification *)notification
{
    if (!_callback || _callback->willIgnoreSystemInterruptions())
        return;

    LOG(Media, "-[WebMediaSessionHelper applicationDidEnterBackground]");

    BOOL isSuspendedUnderLock = [[[notification userInfo] objectForKey:@"isSuspendedUnderLock"] boolValue];
    callOnWebThreadOrDispatchAsyncOnMainThread([protectedSelf = WTFMove(self), isSuspendedUnderLock]() mutable {
        if (auto* callback = protectedSelf->_callback)
            callback->applicationDidEnterBackground(isSuspendedUnderLock);
    });
}
@end

#endif // PLATFORM(IOS_FAMILY)
