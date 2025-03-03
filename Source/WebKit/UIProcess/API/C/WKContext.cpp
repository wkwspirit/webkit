/*
 * Copyright (C) 2010, 2011, 2012 Apple Inc. All rights reserved.
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
#include "WKContextPrivate.h"

#include "APIArray.h"
#include "APIClient.h"
#include "APIDownloadClient.h"
#include "APILegacyContextHistoryClient.h"
#include "APINavigationData.h"
#include "APIProcessPoolConfiguration.h"
#include "APIURLRequest.h"
#include "AuthenticationChallengeProxy.h"
#include "DownloadProxy.h"
#include "WKAPICast.h"
#include "WKArray.h"
#include "WKContextConfigurationRef.h"
#include "WKRetainPtr.h"
#include "WKString.h"
#include "WebCertificateInfo.h"
#include "WebContextInjectedBundleClient.h"
#include "WebProcessPool.h"
#include <wtf/RefPtr.h>
#include <wtf/text/WTFString.h>

// Supplements
#include "WebCookieManagerProxy.h"
#include "WebGeolocationManagerProxy.h"
#include "WebNotificationManagerProxy.h"

namespace API {
template<> struct ClientTraits<WKContextDownloadClientBase> {
    typedef std::tuple<WKContextDownloadClientV0, WKContextDownloadClientV1> Versions;
};
template<> struct ClientTraits<WKContextHistoryClientBase> {
    typedef std::tuple<WKContextHistoryClientV0> Versions;
};
}

WKTypeID WKContextGetTypeID()
{
    return WebKit::toAPI(WebKit::WebProcessPool::APIType);
}

WKContextRef WKContextCreate()
{
    auto configuration = API::ProcessPoolConfiguration::createWithLegacyOptions();
    return WebKit::toAPI(&WebKit::WebProcessPool::create(configuration).leakRef());
}

WKContextRef WKContextCreateWithInjectedBundlePath(WKStringRef pathRef)
{
    auto configuration = API::ProcessPoolConfiguration::createWithLegacyOptions();
    configuration->setInjectedBundlePath(WebKit::toWTFString(pathRef));

    return WebKit::toAPI(&WebKit::WebProcessPool::create(configuration).leakRef());
}

WKContextRef WKContextCreateWithConfiguration(WKContextConfigurationRef configuration)
{
    RefPtr<API::ProcessPoolConfiguration> apiConfiguration = WebKit::toImpl(configuration);
    if (!apiConfiguration)
        apiConfiguration = API::ProcessPoolConfiguration::create();
    return WebKit::toAPI(&WebKit::WebProcessPool::create(*apiConfiguration).leakRef());
}

void WKContextSetClient(WKContextRef contextRef, const WKContextClientBase* wkClient)
{
    WebKit::toImpl(contextRef)->initializeClient(wkClient);
}

void WKContextSetInjectedBundleClient(WKContextRef contextRef, const WKContextInjectedBundleClientBase* wkClient)
{
    WebKit::toImpl(contextRef)->setInjectedBundleClient(std::make_unique<WebKit::WebContextInjectedBundleClient>(wkClient));
}

void WKContextSetHistoryClient(WKContextRef contextRef, const WKContextHistoryClientBase* wkClient)
{
    class HistoryClient final : public API::Client<WKContextHistoryClientBase>, public API::LegacyContextHistoryClient {
    public:
        explicit HistoryClient(const WKContextHistoryClientBase* client)
        {
            initialize(client);
        }

    private:
        void didNavigateWithNavigationData(WebKit::WebProcessPool& processPool, WebKit::WebPageProxy& page, const WebKit::WebNavigationDataStore& navigationDataStore, WebKit::WebFrameProxy& frame) override
        {
            if (!m_client.didNavigateWithNavigationData)
                return;

            RefPtr<API::NavigationData> navigationData = API::NavigationData::create(navigationDataStore);
            m_client.didNavigateWithNavigationData(WebKit::toAPI(&processPool), WebKit::toAPI(&page), WebKit::toAPI(navigationData.get()), WebKit::toAPI(&frame), m_client.base.clientInfo);
        }

        void didPerformClientRedirect(WebKit::WebProcessPool& processPool, WebKit::WebPageProxy& page, const String& sourceURL, const String& destinationURL, WebKit::WebFrameProxy& frame) override
        {
            if (!m_client.didPerformClientRedirect)
                return;

            m_client.didPerformClientRedirect(WebKit::toAPI(&processPool), WebKit::toAPI(&page), WebKit::toURLRef(sourceURL.impl()), WebKit::toURLRef(destinationURL.impl()), WebKit::toAPI(&frame), m_client.base.clientInfo);
        }

        void didPerformServerRedirect(WebKit::WebProcessPool& processPool, WebKit::WebPageProxy& page, const String& sourceURL, const String& destinationURL, WebKit::WebFrameProxy& frame) override
        {
            if (!m_client.didPerformServerRedirect)
                return;

            m_client.didPerformServerRedirect(WebKit::toAPI(&processPool), WebKit::toAPI(&page), WebKit::toURLRef(sourceURL.impl()), WebKit::toURLRef(destinationURL.impl()), WebKit::toAPI(&frame), m_client.base.clientInfo);
        }

        void didUpdateHistoryTitle(WebKit::WebProcessPool& processPool, WebKit::WebPageProxy& page, const String& title, const String& url, WebKit::WebFrameProxy& frame) override
        {
            if (!m_client.didUpdateHistoryTitle)
                return;

            m_client.didUpdateHistoryTitle(WebKit::toAPI(&processPool), WebKit::toAPI(&page), WebKit::toAPI(title.impl()), WebKit::toURLRef(url.impl()), WebKit::toAPI(&frame), m_client.base.clientInfo);
        }

        void populateVisitedLinks(WebKit::WebProcessPool& processPool) override
        {
            if (!m_client.populateVisitedLinks)
                return;

            m_client.populateVisitedLinks(WebKit::toAPI(&processPool), m_client.base.clientInfo);
        }

        bool addsVisitedLinks() const override
        {
            return m_client.populateVisitedLinks;
        }
    };

    WebKit::WebProcessPool& processPool = *WebKit::toImpl(contextRef);
    processPool.setHistoryClient(std::make_unique<HistoryClient>(wkClient));

    bool addsVisitedLinks = processPool.historyClient().addsVisitedLinks();

    for (auto& process : processPool.processes()) {
        for (auto& page : process->pages())
            page->setAddsVisitedLinks(addsVisitedLinks);
    }
}

void WKContextSetDownloadClient(WKContextRef contextRef, const WKContextDownloadClientBase* wkClient)
{
    class DownloadClient final : public API::Client<WKContextDownloadClientBase>, public API::DownloadClient {
    public:
        explicit DownloadClient(const WKContextDownloadClientBase* client)
        {
            initialize(client);
        }
    private:
        void didStart(WebKit::WebProcessPool& processPool, WebKit::DownloadProxy& downloadProxy) final
        {
            if (!m_client.didStart)
                return;

            m_client.didStart(WebKit::toAPI(&processPool), WebKit::toAPI(&downloadProxy), m_client.base.clientInfo);
        }

        void didReceiveAuthenticationChallenge(WebKit::WebProcessPool& processPool, WebKit::DownloadProxy& downloadProxy, WebKit::AuthenticationChallengeProxy& authenticationChallengeProxy) final
        {
            if (!m_client.didReceiveAuthenticationChallenge)
                return;

            m_client.didReceiveAuthenticationChallenge(WebKit::toAPI(&processPool), WebKit::toAPI(&downloadProxy), WebKit::toAPI(&authenticationChallengeProxy), m_client.base.clientInfo);
        }

        void didReceiveResponse(WebKit::WebProcessPool& processPool, WebKit::DownloadProxy& downloadProxy, const WebCore::ResourceResponse& response) final
        {
            if (!m_client.didReceiveResponse)
                return;

            m_client.didReceiveResponse(WebKit::toAPI(&processPool), WebKit::toAPI(&downloadProxy), WebKit::toAPI(API::URLResponse::create(response).ptr()), m_client.base.clientInfo);
        }

        void didReceiveData(WebKit::WebProcessPool& processPool, WebKit::DownloadProxy& downloadProxy, uint64_t length) final
        {
            if (!m_client.didReceiveData)
                return;

            m_client.didReceiveData(WebKit::toAPI(&processPool), WebKit::toAPI(&downloadProxy), length, m_client.base.clientInfo);
        }

        void decideDestinationWithSuggestedFilename(WebKit::WebProcessPool& processPool, WebKit::DownloadProxy& downloadProxy, const String& filename, Function<void(WebKit::AllowOverwrite, WTF::String)>&& completionHandler) final
        {
            if (!m_client.decideDestinationWithSuggestedFilename)
                return completionHandler(WebKit::AllowOverwrite::No, { });

            bool allowOverwrite = false;
            WKRetainPtr<WKStringRef> destination(AdoptWK, m_client.decideDestinationWithSuggestedFilename(WebKit::toAPI(&processPool), WebKit::toAPI(&downloadProxy), WebKit::toAPI(filename.impl()), &allowOverwrite, m_client.base.clientInfo));
            completionHandler(allowOverwrite ? WebKit::AllowOverwrite::Yes : WebKit::AllowOverwrite::No, WebKit::toWTFString(destination.get()));
        }

        void didCreateDestination(WebKit::WebProcessPool& processPool, WebKit::DownloadProxy& downloadProxy, const String& path) final
        {
            if (!m_client.didCreateDestination)
                return;

            m_client.didCreateDestination(WebKit::toAPI(&processPool), WebKit::toAPI(&downloadProxy), WebKit::toAPI(path.impl()), m_client.base.clientInfo);
        }

        void didFinish(WebKit::WebProcessPool& processPool, WebKit::DownloadProxy& downloadProxy) final
        {
            if (!m_client.didFinish)
                return;

            m_client.didFinish(WebKit::toAPI(&processPool), WebKit::toAPI(&downloadProxy), m_client.base.clientInfo);
        }

        void didFail(WebKit::WebProcessPool& processPool, WebKit::DownloadProxy& downloadProxy, const WebCore::ResourceError& error) final
        {
            if (!m_client.didFail)
                return;

            m_client.didFail(WebKit::toAPI(&processPool), WebKit::toAPI(&downloadProxy), WebKit::toAPI(error), m_client.base.clientInfo);
        }
        
        void didCancel(WebKit::WebProcessPool& processPool, WebKit::DownloadProxy& downloadProxy) final
        {
            if (!m_client.didCancel)
                return;
            
            m_client.didCancel(WebKit::toAPI(&processPool), WebKit::toAPI(&downloadProxy), m_client.base.clientInfo);
        }
        
        void processDidCrash(WebKit::WebProcessPool& processPool, WebKit::DownloadProxy& downloadProxy) final
        {
            if (!m_client.processDidCrash)
                return;
            
            m_client.processDidCrash(WebKit::toAPI(&processPool), WebKit::toAPI(&downloadProxy), m_client.base.clientInfo);
        }

        void willSendRequest(WebKit::WebProcessPool& processPool, WebKit::DownloadProxy& downloadProxy, WebCore::ResourceRequest&& request, const WebCore::ResourceResponse&, CompletionHandler<void(WebCore::ResourceRequest&&)>&& completionHandler) final
        {
            if (m_client.didReceiveServerRedirect)
                m_client.didReceiveServerRedirect(WebKit::toAPI(&processPool), WebKit::toAPI(&downloadProxy), WebKit::toURLRef(request.url().string().impl()), m_client.base.clientInfo);

            completionHandler(WTFMove(request));
        }
    };

    WebKit::toImpl(contextRef)->setDownloadClient(std::make_unique<DownloadClient>(wkClient));
}

void WKContextSetConnectionClient(WKContextRef contextRef, const WKContextConnectionClientBase* wkClient)
{
    WebKit::toImpl(contextRef)->initializeConnectionClient(wkClient);
}

WKDownloadRef WKContextDownloadURLRequest(WKContextRef contextRef, WKURLRequestRef requestRef)
{
    return WebKit::toAPI(WebKit::toImpl(contextRef)->download(0, WebKit::toImpl(requestRef)->resourceRequest()));
}

WKDownloadRef WKContextResumeDownload(WKContextRef contextRef, WKDataRef resumeData, WKStringRef path)
{
    return WebKit::toAPI(WebKit::toImpl(contextRef)->resumeDownload(nullptr, WebKit::toImpl(resumeData), WebKit::toWTFString(path)));
}

void WKContextSetInitializationUserDataForInjectedBundle(WKContextRef contextRef,  WKTypeRef userDataRef)
{
    WebKit::toImpl(contextRef)->setInjectedBundleInitializationUserData(WebKit::toImpl(userDataRef));
}

void WKContextPostMessageToInjectedBundle(WKContextRef contextRef, WKStringRef messageNameRef, WKTypeRef messageBodyRef)
{
    WebKit::toImpl(contextRef)->postMessageToInjectedBundle(WebKit::toImpl(messageNameRef)->string(), WebKit::toImpl(messageBodyRef));
}

void WKContextGetGlobalStatistics(WKContextStatistics* statistics)
{
    const WebKit::WebProcessPool::Statistics& webContextStatistics = WebKit::WebProcessPool::statistics();

    statistics->wkViewCount = webContextStatistics.wkViewCount;
    statistics->wkPageCount = webContextStatistics.wkPageCount;
    statistics->wkFrameCount = webContextStatistics.wkFrameCount;
}

void WKContextAddVisitedLink(WKContextRef contextRef, WKStringRef visitedURL)
{
    String visitedURLString = WebKit::toImpl(visitedURL)->string();
    if (visitedURLString.isEmpty())
        return;

    WebKit::toImpl(contextRef)->visitedLinkStore().addVisitedLinkHash(WebCore::computeSharedStringHash(visitedURLString));
}

void WKContextClearVisitedLinks(WKContextRef contextRef)
{
    WebKit::toImpl(contextRef)->visitedLinkStore().removeAll();
}

void WKContextSetCacheModel(WKContextRef contextRef, WKCacheModel cacheModel)
{
    WebKit::toImpl(contextRef)->setCacheModel(WebKit::toCacheModel(cacheModel));
}

WKCacheModel WKContextGetCacheModel(WKContextRef contextRef)
{
    return WebKit::toAPI(WebKit::toImpl(contextRef)->cacheModel());
}

void WKContextSetMaximumNumberOfProcesses(WKContextRef, unsigned)
{
    // Deprecated.
}

unsigned WKContextGetMaximumNumberOfProcesses(WKContextRef)
{
    // Deprecated.
    return std::numeric_limits<unsigned>::max();
}

void WKContextSetAlwaysUsesComplexTextCodePath(WKContextRef contextRef, bool alwaysUseComplexTextCodePath)
{
    WebKit::toImpl(contextRef)->setAlwaysUsesComplexTextCodePath(alwaysUseComplexTextCodePath);
}

void WKContextSetShouldUseFontSmoothing(WKContextRef contextRef, bool useFontSmoothing)
{
    WebKit::toImpl(contextRef)->setShouldUseFontSmoothing(useFontSmoothing);
}

void WKContextSetAdditionalPluginsDirectory(WKContextRef contextRef, WKStringRef pluginsDirectory)
{
#if ENABLE(NETSCAPE_PLUGIN_API)
    WebKit::toImpl(contextRef)->setAdditionalPluginsDirectory(WebKit::toImpl(pluginsDirectory)->string());
#else
    UNUSED_PARAM(contextRef);
    UNUSED_PARAM(pluginsDirectory);
#endif
}

void WKContextRefreshPlugIns(WKContextRef context)
{
#if ENABLE(NETSCAPE_PLUGIN_API)
    WebKit::toImpl(context)->refreshPlugins();
#else
    UNUSED_PARAM(context);
#endif
}

void WKContextRegisterURLSchemeAsEmptyDocument(WKContextRef contextRef, WKStringRef urlScheme)
{
    WebKit::toImpl(contextRef)->registerURLSchemeAsEmptyDocument(WebKit::toImpl(urlScheme)->string());
}

void WKContextRegisterURLSchemeAsSecure(WKContextRef contextRef, WKStringRef urlScheme)
{
    WebKit::toImpl(contextRef)->registerURLSchemeAsSecure(WebKit::toImpl(urlScheme)->string());
}

void WKContextRegisterURLSchemeAsBypassingContentSecurityPolicy(WKContextRef contextRef, WKStringRef urlScheme)
{
    WebKit::toImpl(contextRef)->registerURLSchemeAsBypassingContentSecurityPolicy(WebKit::toImpl(urlScheme)->string());
}

void WKContextRegisterURLSchemeAsCachePartitioned(WKContextRef contextRef, WKStringRef urlScheme)
{
    WebKit::toImpl(contextRef)->registerURLSchemeAsCachePartitioned(WebKit::toImpl(urlScheme)->string());
}

void WKContextRegisterURLSchemeAsCanDisplayOnlyIfCanRequest(WKContextRef contextRef, WKStringRef urlScheme)
{
    WebKit::toImpl(contextRef)->registerURLSchemeAsCanDisplayOnlyIfCanRequest(WebKit::toImpl(urlScheme)->string());
}

void WKContextSetDomainRelaxationForbiddenForURLScheme(WKContextRef contextRef, WKStringRef urlScheme)
{
    WebKit::toImpl(contextRef)->setDomainRelaxationForbiddenForURLScheme(WebKit::toImpl(urlScheme)->string());
}

void WKContextSetCanHandleHTTPSServerTrustEvaluation(WKContextRef contextRef, bool value)
{
    WebKit::toImpl(contextRef)->setCanHandleHTTPSServerTrustEvaluation(value);
}

void WKContextSetPrewarmsProcessesAutomatically(WKContextRef contextRef, bool value)
{
    WebKit::toImpl(contextRef)->configuration().setIsAutomaticProcessWarmingEnabled(value);
}

void WKContextSetUsesSingleWebProcess(WKContextRef contextRef, bool value)
{
    WebKit::toImpl(contextRef)->configuration().setUsesSingleWebProcess(value);
}

bool WKContextGetUsesSingleWebProcess(WKContextRef contextRef)
{
    return WebKit::toImpl(contextRef)->configuration().usesSingleWebProcess();
}

void WKContextSetCustomWebContentServiceBundleIdentifier(WKContextRef contextRef, WKStringRef name)
{
    WebKit::toImpl(contextRef)->setCustomWebContentServiceBundleIdentifier(WebKit::toImpl(name)->string());
}

void WKContextSetDiskCacheSpeculativeValidationEnabled(WKContextRef contextRef, bool value)
{
    WebKit::toImpl(contextRef)->configuration().setDiskCacheSpeculativeValidationEnabled(value);
}

void WKContextPreconnectToServer(WKContextRef contextRef, WKURLRef serverURLRef)
{
    WebKit::toImpl(contextRef)->preconnectToServer(URL(URL(), WebKit::toWTFString(serverURLRef)));
}

WKCookieManagerRef WKContextGetCookieManager(WKContextRef contextRef)
{
    return WebKit::toAPI(WebKit::toImpl(contextRef)->supplement<WebKit::WebCookieManagerProxy>());
}

WKWebsiteDataStoreRef WKContextGetWebsiteDataStore(WKContextRef context)
{
    auto* dataStore = WebKit::toImpl(context)->websiteDataStore();
    if (!dataStore) {
        auto defaultDataStore = API::WebsiteDataStore::defaultDataStore();
        WebKit::toImpl(context)->setPrimaryDataStore(defaultDataStore.get());
        dataStore = defaultDataStore.ptr();
    }

    return WebKit::toAPI(dataStore);
}

WKApplicationCacheManagerRef WKContextGetApplicationCacheManager(WKContextRef context)
{
    return reinterpret_cast<WKApplicationCacheManagerRef>(WKContextGetWebsiteDataStore(context));
}

WKGeolocationManagerRef WKContextGetGeolocationManager(WKContextRef contextRef)
{
    return WebKit::toAPI(WebKit::toImpl(contextRef)->supplement<WebKit::WebGeolocationManagerProxy>());
}

WKIconDatabaseRef WKContextGetIconDatabase(WKContextRef)
{
    return nullptr;
}

WKKeyValueStorageManagerRef WKContextGetKeyValueStorageManager(WKContextRef context)
{
    return reinterpret_cast<WKKeyValueStorageManagerRef>(WKContextGetWebsiteDataStore(context));
}

WKMediaSessionFocusManagerRef WKContextGetMediaSessionFocusManager(WKContextRef context)
{
#if ENABLE(MEDIA_SESSION)
    return WebKit::toAPI(WebKit::toImpl(context)->supplement<WebKit::WebMediaSessionFocusManager>());
#else
    UNUSED_PARAM(context);
    return nullptr;
#endif
}

WKNotificationManagerRef WKContextGetNotificationManager(WKContextRef contextRef)
{
    return WebKit::toAPI(WebKit::toImpl(contextRef)->supplement<WebKit::WebNotificationManagerProxy>());
}

WKResourceCacheManagerRef WKContextGetResourceCacheManager(WKContextRef context)
{
    return reinterpret_cast<WKResourceCacheManagerRef>(WKContextGetWebsiteDataStore(context));
}

void WKContextStartMemorySampler(WKContextRef contextRef, WKDoubleRef interval)
{
    WebKit::toImpl(contextRef)->startMemorySampler(WebKit::toImpl(interval)->value());
}

void WKContextStopMemorySampler(WKContextRef contextRef)
{
    WebKit::toImpl(contextRef)->stopMemorySampler();
}

void WKContextSetIconDatabasePath(WKContextRef, WKStringRef)
{
}

void WKContextAllowSpecificHTTPSCertificateForHost(WKContextRef contextRef, WKCertificateInfoRef certificateRef, WKStringRef hostRef)
{
    WebKit::toImpl(contextRef)->allowSpecificHTTPSCertificateForHost(WebKit::toImpl(certificateRef), WebKit::toImpl(hostRef)->string());
}

void WKContextDisableProcessTermination(WKContextRef contextRef)
{
    WebKit::toImpl(contextRef)->disableProcessTermination();
}

void WKContextEnableProcessTermination(WKContextRef contextRef)
{
    WebKit::toImpl(contextRef)->enableProcessTermination();
}

void WKContextSetHTTPPipeliningEnabled(WKContextRef contextRef, bool enabled)
{
    WebKit::toImpl(contextRef)->setHTTPPipeliningEnabled(enabled);
}

void WKContextWarmInitialProcess(WKContextRef contextRef)
{
    WebKit::toImpl(contextRef)->prewarmProcess(nullptr, WebKit::WebProcessPool::MayCreateDefaultDataStore::Yes);
}

void WKContextGetStatistics(WKContextRef contextRef, void* context, WKContextGetStatisticsFunction callback)
{
    WebKit::toImpl(contextRef)->getStatistics(0xFFFFFFFF, WebKit::toGenericCallbackFunction(context, callback));
}

void WKContextGetStatisticsWithOptions(WKContextRef contextRef, WKStatisticsOptions optionsMask, void* context, WKContextGetStatisticsFunction callback)
{
    WebKit::toImpl(contextRef)->getStatistics(optionsMask, WebKit::toGenericCallbackFunction(context, callback));
}

bool WKContextJavaScriptConfigurationFileEnabled(WKContextRef contextRef)
{
    return WebKit::toImpl(contextRef)->javaScriptConfigurationFileEnabled();
}

void WKContextSetJavaScriptConfigurationFileEnabled(WKContextRef contextRef, bool enable)
{
    WebKit::toImpl(contextRef)->setJavaScriptConfigurationFileEnabled(enable);
}

void WKContextGarbageCollectJavaScriptObjects(WKContextRef contextRef)
{
    WebKit::toImpl(contextRef)->garbageCollectJavaScriptObjects();
}

void WKContextSetJavaScriptGarbageCollectorTimerEnabled(WKContextRef contextRef, bool enable)
{
    WebKit::toImpl(contextRef)->setJavaScriptGarbageCollectorTimerEnabled(enable);
}

void WKContextUseTestingNetworkSession(WKContextRef context)
{
    WebKit::toImpl(context)->useTestingNetworkSession();
}

void WKContextSetAllowsAnySSLCertificateForWebSocketTesting(WKContextRef context, bool allows)
{
    WebKit::toImpl(context)->setAllowsAnySSLCertificateForWebSocket(allows);
}

void WKContextSetAllowsAnySSLCertificateForServiceWorkerTesting(WKContextRef context, bool allows)
{
#if ENABLE(SERVICE_WORKER)
    WebKit::toImpl(context)->setAllowsAnySSLCertificateForServiceWorker(allows);
#endif
}

void WKContextClearCachedCredentials(WKContextRef context)
{
    WebKit::toImpl(context)->clearCachedCredentials();
}

WKDictionaryRef WKContextCopyPlugInAutoStartOriginHashes(WKContextRef contextRef)
{
    return WebKit::toAPI(&WebKit::toImpl(contextRef)->plugInAutoStartOriginHashes().leakRef());
}

void WKContextSetPlugInAutoStartOriginHashes(WKContextRef contextRef, WKDictionaryRef dictionaryRef)
{
    if (!dictionaryRef)
        return;
    WebKit::toImpl(contextRef)->setPlugInAutoStartOriginHashes(*WebKit::toImpl(dictionaryRef));
}

void WKContextSetPlugInAutoStartOriginsFilteringOutEntriesAddedAfterTime(WKContextRef contextRef, WKDictionaryRef dictionaryRef, double time)
{
    if (!dictionaryRef)
        return;
    WebKit::toImpl(contextRef)->setPlugInAutoStartOriginsFilteringOutEntriesAddedAfterTime(*WebKit::toImpl(dictionaryRef), WallTime::fromRawSeconds(time));
}

void WKContextSetPlugInAutoStartOrigins(WKContextRef contextRef, WKArrayRef arrayRef)
{
    if (!arrayRef)
        return;
    WebKit::toImpl(contextRef)->setPlugInAutoStartOrigins(*WebKit::toImpl(arrayRef));
}

void WKContextSetInvalidMessageFunction(WKContextInvalidMessageFunction invalidMessageFunction)
{
    WebKit::WebProcessPool::setInvalidMessageCallback(invalidMessageFunction);
}

void WKContextSetMemoryCacheDisabled(WKContextRef contextRef, bool disabled)
{
    WebKit::toImpl(contextRef)->setMemoryCacheDisabled(disabled);
}

void WKContextSetFontWhitelist(WKContextRef contextRef, WKArrayRef arrayRef)
{
    WebKit::toImpl(contextRef)->setFontWhitelist(WebKit::toImpl(arrayRef));
}

void WKContextTerminateNetworkProcess(WKContextRef context)
{
    WebKit::toImpl(context)->terminateNetworkProcess();
}

void WKContextTerminateServiceWorkerProcess(WKContextRef context)
{
    WebKit::toImpl(context)->terminateServiceWorkerProcesses();
}

ProcessID WKContextGetNetworkProcessIdentifier(WKContextRef contextRef)
{
    return WebKit::toImpl(contextRef)->networkProcessIdentifier();
}

void WKContextAddSupportedPlugin(WKContextRef contextRef, WKStringRef domainRef, WKStringRef nameRef, WKArrayRef mimeTypesRef, WKArrayRef extensionsRef)
{
#if ENABLE(NETSCAPE_PLUGIN_API)
    HashSet<String> mimeTypes;
    HashSet<String> extensions;

    size_t count = WKArrayGetSize(mimeTypesRef);
    for (size_t i = 0; i < count; ++i)
        mimeTypes.add(WebKit::toWTFString(static_cast<WKStringRef>(WKArrayGetItemAtIndex(mimeTypesRef, i))));
    count = WKArrayGetSize(extensionsRef);
    for (size_t i = 0; i < count; ++i)
        extensions.add(WebKit::toWTFString(static_cast<WKStringRef>(WKArrayGetItemAtIndex(extensionsRef, i))));

    WebKit::toImpl(contextRef)->addSupportedPlugin(WebKit::toWTFString(domainRef), WebKit::toWTFString(nameRef), WTFMove(mimeTypes), WTFMove(extensions));
#endif
}

void WKContextClearSupportedPlugins(WKContextRef contextRef)
{
#if ENABLE(NETSCAPE_PLUGIN_API)
    WebKit::toImpl(contextRef)->clearSupportedPlugins();
#endif
}

void WKContextSetIDBPerOriginQuota(WKContextRef contextRef, uint64_t quota)
{
    WebKit::toImpl(contextRef)->setIDBPerOriginQuota(quota);
}

void WKContextClearCurrentModifierStateForTesting(WKContextRef contextRef)
{
    WebKit::toImpl(contextRef)->clearCurrentModifierStateForTesting();
}
