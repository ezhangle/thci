/*
 *
 *    Copyright (c) 2016-2018 Nest Labs, Inc.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      This file implements SOC version of Thread Host Control Interface.
 */

#include <thci_config.h>

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP == 0

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nlalignment.h>
#include <nlassert.h>
#include <nlerlog.h>
#include <nlerevent.h>
#include <nlererror.h>
#include <nlertask.h>

#include <openthread/openthread.h>
#include <openthread/link.h>
#include <openthread/ip6.h>
#include <openthread/message.h>
#include <openthread/icmp6.h>
#include <openthread/diag.h>

/* thci includes */
#include <thci.h>
#include <thci_module.h>
#include <thci_module_soc.h>

/* nlopenthread platform includes */
#include <nlopenthread.h>

#include <lwip/init.h>
#include <lwip/ip6.h>
#include <lwip/ip6_addr.h>
#include <lwip/ip6_frag.h>
#include <lwip/err.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/tcpip.h>

/**
 * DEFINES
 */

#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
#error Legacy support has not been implemented for SOC builds.
#endif

/**
 * PROTOTYPES
 */

extern int thciSafeInitialize(void);
extern int thciSafeFinalize(void);

static int OutgoingIPPacketEventHandler(nl_event_t *aEvent, void *aClosure);
static void thciReceiveIp6DatagramCallback(otMessage *aMessage, void *aContext);

#if LWIP_VERSION_MAJOR < 2
static err_t thciLwIPOutputIP6(struct netif *netif, struct pbuf *pbuf, struct ip6_addr *ipaddr);
#else
static err_t thciLwIPOutputIP6(struct netif *netif, struct pbuf *pbuf, const struct ip6_addr *ipaddr);
#endif

/**
 * GLOBALS
 */

static otInstance *sInstance = NULL;

static const nl_event_t sOutgoingIPPacketEvent =
{
    NL_INIT_EVENT_STATIC(((nl_event_type_t)NL_EVENT_T_RUNTIME), OutgoingIPPacketEventHandler, NULL)
};

extern thci_sdk_context_t gTHCISDKContext;

/**
 * MODULE IMPLEMENTATION
 */

static int CreateOTMessageFromPbuf(struct pbuf *aPbuf, otMessage **aMessage)
{
    bool linkSecurityEnabled = THCI_ENABLE_MESSAGE_SECURITY(gTHCISDKContext.mSecurityFlags);
    otMessage *message;
    int retval = -EINVAL;

    nlREQUIRE(aPbuf != NULL && aMessage != NULL, done);

#if 0 // Enable this when full support has been implemented.
    if (linkSecurityEnabled && THCI_TEST_INSECURE_PORTS(gTHCISDKContext.mSecurityFlags))
    {
        // TODO: For router devices that have security enabled but are supporting
        //       provisional join it will be necessary to look also at the pbuf's
        //       UDP port and whether Joining is enabled, in order to set this properly.
    }
#endif

    // allocate an otMessage.
    message = otIp6NewMessage(thciGetOtInstance(), linkSecurityEnabled);
    nlREQUIRE_ACTION(message != NULL, done, retval = -ENOMEM);

    {
        struct pbuf *pbuf_chunk = aPbuf;
        uint16_t tot_len = aPbuf->tot_len;
        uint16_t len;

        // convert from pbuf to otMessage.
        while (pbuf_chunk && tot_len)
        {
            if (pbuf_chunk->len <= tot_len)
            {
                len = pbuf_chunk->len;
            }
            else
            {
                len = tot_len;
            }

            tot_len -= len;
            // populate otMessage with pbuf
            retval = otMessageAppend(message, pbuf_chunk->payload, len);
            nlREQUIRE_ACTION(retval == 0, done, NL_LOG_CRIT(lrTHCI, "otAppendMessage failed with status:%d, total len:%d, remaining len:%d\n", retval, len, aPbuf->tot_len, (tot_len + len)));

            pbuf_chunk = pbuf_chunk->next;
        }

        nlREQUIRE_ACTION(tot_len == 0, done, NL_LOG_CRIT(lrTHCI, "CreateOTMessageFromPbuf: pbuf parse error tot_len=%u\n", tot_len));

        *aMessage = message;

        retval = 0; // success
    }

 done:
    if (retval != 0)
    {
        // Print message buffer stats to see where packets may be held
        otBufferInfo bufferInfo;
        otMessageGetBufferInfo(thciGetOtInstance(), &bufferInfo);

        NL_LOG_CRIT(lrTHCI, "total: %d\r\n", bufferInfo.mTotalBuffers);
        NL_LOG_CRIT(lrTHCI, "free: %d\r\n", bufferInfo.mFreeBuffers);
        NL_LOG_CRIT(lrTHCI, "6lo send: %d %d\r\n", bufferInfo.m6loSendMessages, bufferInfo.m6loSendBuffers);
        NL_LOG_CRIT(lrTHCI, "6lo reas: %d %d\r\n", bufferInfo.m6loReassemblyMessages, bufferInfo.m6loReassemblyBuffers);
        NL_LOG_CRIT(lrTHCI, "ip6: %d %d\r\n", bufferInfo.mIp6Messages, bufferInfo.mIp6Buffers);
        NL_LOG_CRIT(lrTHCI, "mpl: %d %d\r\n", bufferInfo.mMplMessages, bufferInfo.mMplBuffers);
        NL_LOG_CRIT(lrTHCI, "mle: %d %d\r\n", bufferInfo.mMleMessages, bufferInfo.mMleBuffers);
        NL_LOG_CRIT(lrTHCI, "arp: %d %d\r\n", bufferInfo.mArpMessages, bufferInfo.mArpBuffers);
        NL_LOG_CRIT(lrTHCI, "coap: %d %d\r\n", bufferInfo.mCoapMessages, bufferInfo.mCoapBuffers);

        // Free message in case of failure
        if (message)
        {
            otMessageFree(message);
        }
    }
    return retval;
}

void thciSetLocalDeviceRole(void)
{
    gTHCISDKContext.mDeviceRole = thciGetDeviceRole();
}

otDeviceRole thciGetLocalDeviceRole(void)
{
    return gTHCISDKContext.mDeviceRole;
}

otInstance *thciGetOtInstance(void)
{
    nlREQUIRE_ACTION(sInstance != NULL, done, NL_LOG_CRIT(lrTHCI, "missing OpenThread instance.\n"));

done:
    return sInstance;
}

otError thciInitialize(thci_callbacks_t *aCallbacks)
{
    otError retval = OT_ERROR_NONE;

    nlREQUIRE_ACTION(aCallbacks != NULL, done, retval = OT_ERROR_INVALID_ARGS);
    /* Initialize nlopenthread platform */

    sInstance = nlOpenThreadInitialize((void *)gTHCISDKContext.mInitParams.mSdkQueue);
    nlREQUIRE_ACTION(sInstance != NULL, done, retval = OT_ERROR_FAILED);

    // Initialize the callback on every call to otInitialize as that function will
    // cause the OT state to NULL out the value.
    otIp6SetReceiveCallback(sInstance, thciReceiveIp6DatagramCallback, NULL);

    otSetStateChangedCallback(sInstance, aCallbacks->mStateChangeCallback, NULL);

    thciSafeInitialize();

    NL_LOG_DEBUG(lrTHCI, "Initialized %s\n", otGetVersionString());

done:
    return retval;
}

otError thciFinalize(void)
{
    otError retval = OT_ERROR_NONE;

    // allow thciFinalize to be called prior to thciInitialize without consequence.
    nlREQUIRE(sInstance != NULL, done);

    thciSafeFinalize();

    nlOpenThreadFinalize(thciGetOtInstance());
    sInstance = NULL;

 done:
    return retval;
}

otError thciInterfaceUp(void)
{
    otError error = otIp6SetEnabled(thciGetOtInstance(), true);

    NL_LOG_DEBUG(lrTHCI, "Interface Up\n");

    return error;
}

otError thciInterfaceDown(void)
{
    otError error = otIp6SetEnabled(thciGetOtInstance(), false);

    NL_LOG_DEBUG(lrTHCI, "Interface Down\n");

    return error;
}

otError thciIsInterfaceEnabled(bool *aEnabled)
{
    otError error = OT_ERROR_NONE;
    *aEnabled = otIp6IsEnabled(thciGetOtInstance());

    return error;
}

otError thciThreadStart(void)
{
    otError error = otThreadSetEnabled(thciGetOtInstance(), true);

    nlREQUIRE(error == OT_ERROR_NONE, done);

#if DEBUG && (nlLOG_PRIORITY >= nlLPDEBG)
    uint8_t i;
    const uint8_t *e = otThreadGetExtendedPanId(thciGetOtInstance());
    const otMasterKey *k = otThreadGetMasterKey(thciGetOtInstance());
    otLinkModeConfig linkMode = otThreadGetLinkMode(thciGetOtInstance());
    char epanid[2 * OT_EXT_PAN_ID_SIZE + 1];
    char key[2 * OT_MASTER_KEY_SIZE + 1];
    char mode[5] = {0};
    char *m = mode;

    for (i = 0; i < OT_EXT_PAN_ID_SIZE; i++)
        sprintf(epanid + 2 * i, "%02X", e[i]);

    for (i = 0; i < OT_MASTER_KEY_SIZE; i++)
        sprintf(key + 2 * i, "%02X", k->m8[i]);

    if (linkMode.mRxOnWhenIdle)
        *m++ = 'r';

    if (linkMode.mSecureDataRequests)
        *m++ = 's';

    if (linkMode.mDeviceType)
        *m++ = 'd';

    if (linkMode.mNetworkData)
        *m++ = 'n';

    NL_LOG_DEBUG(lrTHCI, "Calling Thread start:\n");
    NL_LOG_DEBUG(lrTHCI, "          panid: 0x%04X\n", otLinkGetPanId(thciGetOtInstance()));
    NL_LOG_DEBUG(lrTHCI, "       extpanid: %s\n", epanid);
    NL_LOG_DEBUG(lrTHCI, "        channel: %d\n", otLinkGetChannel(thciGetOtInstance()));
    NL_LOG_DEBUG(lrTHCI, "     master key: %s\n", key);
    NL_LOG_DEBUG(lrTHCI, "           mode: %s\n", mode);
    NL_LOG_DEBUG(lrTHCI, "  child timeout: %d\n", otThreadGetChildTimeout(thciGetOtInstance()));
#endif /* DEBUG */

    gTHCISDKContext.mSecurityFlags |= THCI_SECURITY_FLAG_THREAD_STARTED;

 done:
    return error;
}

otError thciThreadStop(void)
{
    otError error = otThreadSetEnabled(thciGetOtInstance(), false);

    gTHCISDKContext.mSecurityFlags &= ~THCI_SECURITY_FLAG_THREAD_STARTED;

    return error;
}

otError thciGetVersionString(char *aString, size_t aLength)
{
    otError error = OT_ERROR_NONE;
    const char *version = otGetVersionString();

    nlREQUIRE_ACTION(aString != NULL && aLength != 0, done, error = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(version != NULL, done, error = OT_ERROR_FAILED);

    memcpy(aString, version, aLength);

    if (strlen(version) >= aLength)
    {
        // Supply a NULL term in the event that the caller passed a too small buffer.
        aString[aLength - 1] = '\0';
    }

  done:
    return error;
}

otError thciIsNodeCommissioned(bool *aCommissioned)
{
    otError error = OT_ERROR_NONE;
    *aCommissioned = otDatasetIsCommissioned(thciGetOtInstance());

    return error;
}

otError thciFactoryReset(void)
{
    otError error = OT_ERROR_NONE;

    otInstanceFactoryReset(thciGetOtInstance());

    return error;
}

otError thciPersistentInfoErase(void)
{
    otError error;

    error = otInstanceErasePersistentInfo(thciGetOtInstance());

    return error;
}

otError thciSetReceiveIp6DatagramFilterEnabled(bool aEnabled)
{
    otIp6SetReceiveFilterEnabled(thciGetOtInstance(), aEnabled);

    return OT_ERROR_NONE;
}

otError thciActiveScan(uint32_t aScanChannels, uint16_t aScanDuration, thciHandleActiveScanResult aCallback, void *aContext)
{
    otError error = otLinkActiveScan(thciGetOtInstance(), aScanChannels, aScanDuration, aCallback, aContext);

    return error;
}

otError thciDiscover(uint32_t aScanChannels, bool aJoiner, bool aEnableEUI64Filtering, thciHandleActiveScanResult aCallback, void *aContext)
{
    otError error = otThreadDiscover(thciGetOtInstance(), aScanChannels, OT_PANID_BROADCAST, aJoiner, aEnableEUI64Filtering, aCallback, aContext);

    return error;
}

otError thciGetNetworkParams(thci_network_params_t *aNetworkParams)
{
    otError error = OT_ERROR_INVALID_ARGS;

    if (aNetworkParams)
    {
        memcpy(aNetworkParams->mNetworkName, otThreadGetNetworkName(thciGetOtInstance()), sizeof(aNetworkParams->mNetworkName));
        memcpy(aNetworkParams->mExtAddress.m8, otLinkGetExtendedAddress(thciGetOtInstance()), sizeof(aNetworkParams->mExtAddress));
        memcpy(aNetworkParams->mExtPanId, otThreadGetExtendedPanId(thciGetOtInstance()), sizeof(aNetworkParams->mExtPanId));

        aNetworkParams->mPanId = otLinkGetPanId(thciGetOtInstance());
        aNetworkParams->mShortAddress = otLinkGetShortAddress(thciGetOtInstance());
        aNetworkParams->mRole =  thciGetLocalDeviceRole();
        aNetworkParams->mChannel = otLinkGetChannel(thciGetOtInstance());
        aNetworkParams->mPartitionId = otThreadGetPartitionId(thciGetOtInstance());

        error = OT_ERROR_NONE;
    }

    return error;
}

otError thciGetChannel(uint8_t *aChannel)
{
    otError error = OT_ERROR_NONE;
    nlREQUIRE_ACTION(aChannel != NULL, done, error = OT_ERROR_INVALID_ARGS);

    *aChannel = otLinkGetChannel(thciGetOtInstance());

done:
    return error;
}

otError thciSetChannel(uint16_t aChannel)
{
    otError error;

    error = otLinkSetChannel(thciGetOtInstance(), aChannel);

    return error;
}

uint32_t thciGetChildTimeout(void)
{
    uint32_t timeout = otThreadGetChildTimeout(thciGetOtInstance());

    return timeout;
}

void thciSetChildTimeout(uint32_t aTimeout)
{
    otThreadSetChildTimeout(thciGetOtInstance(), aTimeout);
}


otError thciGetExtendedAddress(uint8_t *aAddress)
{
    otError error = OT_ERROR_NONE;
    const uint8_t *theAddress;

    nlREQUIRE_ACTION(aAddress != NULL, done, error = OT_ERROR_INVALID_ARGS);

    theAddress = otLinkGetExtendedAddress(thciGetOtInstance());

    nlREQUIRE_ACTION(theAddress != NULL, done, error = OT_ERROR_FAILED);

    memcpy(aAddress, theAddress, OT_EXT_ADDRESS_SIZE);

 done:
    return error;
}

const uint8_t *thciGetExtendedPanId(void)
{
    const uint8_t *extended_panid = otThreadGetExtendedPanId(thciGetOtInstance());

    return extended_panid;
}


otError thciSetExtendedPanId(const uint8_t *aExtendedPanId)
{
    otError error = OT_ERROR_NONE;

    otThreadSetExtendedPanId(thciGetOtInstance(), aExtendedPanId);

    return error;
}


otLinkModeConfig thciGetLinkMode(void)
{
    return otThreadGetLinkMode(thciGetOtInstance());
}


otError thciSetLinkMode(otLinkModeConfig aMode)
{
    return otThreadSetLinkMode(thciGetOtInstance(), aMode);
}

const uint8_t *thciGetMasterKey(uint8_t *aKeyLength)
{
    if (aKeyLength != NULL)
    {
        *aKeyLength = OT_MASTER_KEY_SIZE;
    }

    return otThreadGetMasterKey(thciGetOtInstance())->m8;
}

void thciSetMaxTxPower(int8_t aPower)
{
    otPlatRadioSetTransmitPower(thciGetOtInstance(), aPower);
}

otError thciSetMasterKey(const void *aKey, uint8_t aKeyLength)
{
    otError error = OT_ERROR_INVALID_ARGS;

    nlREQUIRE(aKeyLength == OT_MASTER_KEY_SIZE, done);

    error = otThreadSetMasterKey(thciGetOtInstance(), (const otMasterKey *)aKey);
    nlREQUIRE(error == OT_ERROR_NONE, done);

 done:
    return error;
}

const char *thciGetNetworkName(void)
{
    const char *name = otThreadGetNetworkName(thciGetOtInstance());

    return name;
}

otError thciSetNetworkName(const char *aNetworkName)
{
    return otThreadSetNetworkName(thciGetOtInstance(), aNetworkName);
}

otPanId thciGetPanId(void)
{
    otPanId pan = otLinkGetPanId(thciGetOtInstance());

    return pan;
}


otError thciSetPanId(otPanId aPanId)
{
    return otLinkSetPanId(thciGetOtInstance(), aPanId);
}


const otNetifAddress *thciGetUnicastAddresses(void)
{
    const otNetifAddress *addr = otIp6GetUnicastAddresses(thciGetOtInstance());

    return addr;
}

otError thciAddUnicastAddress(otNetifAddress *aAddress)
{
    otError error = OT_ERROR_INVALID_ARGS;

    if (aAddress)
    {
        NL_LOG_DEBUG(lrTHCI, "Adding IPv6 Address %s\n", ip6addr_ntoa((const ip6_addr_t*)aAddress));

        error = otIp6AddUnicastAddress(thciGetOtInstance(), aAddress);
    }

    return error;
}


otError thciRemoveUnicastAddress(otIp6Address *aAddress)
{
    otError error = OT_ERROR_INVALID_ARGS;

    if (aAddress)
    {
        NL_LOG_DEBUG(lrTHCI, "Removing IPv6 Address %s\n", ip6addr_ntoa((const ip6_addr_t*)aAddress));

        error = otIp6RemoveUnicastAddress(thciGetOtInstance(), aAddress);
    }

    return error;
}

const otNetifMulticastAddress *thciGetMulticastAddresses(void)
{
    const otNetifMulticastAddress *addr = otIp6GetMulticastAddresses(thciGetOtInstance());

    return addr;
}

#if THCI_ENABLE_FTD

otError thciSetLocalLeaderWeight(uint8_t aWeight)
{
    otThreadSetLocalLeaderWeight(thciGetOtInstance(), aWeight);

    return OT_ERROR_NONE;
}

otError thciSetContextIdReuseDelay(uint32_t aDelay)
{
    otThreadSetContextIdReuseDelay(thciGetOtInstance(), aDelay);

    return OT_ERROR_NONE;
}

otError thciGetContextIdReuseDelay(uint32_t *aDelay)
{
    otError retval = OT_ERROR_INVALID_ARGS;

    if (aDelay)
    {
        *aDelay = otThreadGetContextIdReuseDelay(thciGetOtInstance());

        retval = OT_ERROR_NONE;
    }

    return retval;
}

otError thciGetNetworkIdTimeout(uint8_t *aTimeout)
{
    otError retval = OT_ERROR_INVALID_ARGS;

    if (aTimeout)
    {
        *aTimeout = otThreadGetNetworkIdTimeout(thciGetOtInstance());

        retval = OT_ERROR_NONE;
    }

    return timeout;
}

otError thciSetNetworkIdTimeout(uint32_t aTimeout)
{
    otThreadSetNetworkIdTimeout(thciGetOtInstance(), aTimeout);

    return OT_ERROR_NONE;
}

otError thciGetRouterUpgradeThreshold(uint8_t *aThreshold)
{
    otError retval = OT_ERROR_INVALID_ARGS;

    if (aThreshold)
    {
        *aThreshold = otThreadGetRouterUpgradeThreshold(thciGetOtInstance());
        
        retval = OT_ERROR_NONE;
    }

    return retval;
}

otError thciSetRouterUpgradeThreshold(uint8_t aThreshold)
{
    otThreadSetRouterUpgradeThreshold(thciGetOtInstance(), aThreshold);

    return OT_ERROR_NONE;
}

otError thciReleaseRouterId(uint8_t aRouterId)
{
    otError error = otThreadReleaseRouterId(thciGetOtInstance(), aRouterId);

    return error;
}

otError thciGetRouterIdSequence(uint8_t *aSequence)
{
    otError retval = OT_ERROR_INVALID_ARGS;

    if (aThreshold)
    {
        *aThreshold = otThreadGetRouterIdSequence(thciGetOtInstance());

        retval = OT_ERROR_NONE;
    }

    return seq;
}

#else // THCI_ENABLE_FTD

otError thciSetLocalLeaderWeight(uint8_t aWeight)
{
    (void)aWeight;

    return OT_ERROR_DISABLED_FEATURE;
}

otError thciSetContextIdReuseDelay(uint32_t aDelay)
{
    (void)aDelay;

    return OT_ERROR_DISABLED_FEATURE;
}

otError thciGetContextIdReuseDelay(uint32_t *aDelay)
{
    (void)aDelay;

    return OT_ERROR_DISABLED_FEATURE;
}

otError thciGetNetworkIdTimeout(uint8_t *aTimeout)
{
    (void)aTimeout;

    return OT_ERROR_DISABLED_FEATURE;
}

otError thciSetNetworkIdTimeout(uint32_t aTimeout)
{
    (void)aTimeout;

    return OT_ERROR_DISABLED_FEATURE;
}

otError thciGetRouterUpgradeThreshold(uint8_t *aThreshold)
{
    (void)aThreshold;

    return OT_ERROR_DISABLED_FEATURE;
}

otError thciSetRouterUpgradeThreshold(uint8_t aThreshold)
{
    (void)aThreshold;

    return OT_ERROR_DISABLED_FEATURE;
}

otError thciReleaseRouterId(uint8_t aRouterId)
{
    (void)aRouterId;

    return OT_ERROR_DISABLED_FEATURE;
}

otError thciGetRouterIdSequence(uint8_t *aSequence)
{
    (void)aSequence;

    return OT_ERROR_DISABLED_FEATURE;
}

#endif // THCI_ENABLE_FTD

#if THCI_CONFIG_ENABLE_BORDER_ROUTER

otError thciAddBorderRouter(const otBorderRouterConfig *aConfig)
{
    otError error = OT_ERROR_INVALID_ARGS;

    if (aConfig)
    {
        error = otBorderRouterAddOnMeshPrefix(thciGetOtInstance(), aConfig);
    }

    return error;
}

otError thciRemoveBorderRouter(const otIp6Prefix *aPrefix)
{
    otError error = OT_ERROR_INVALID_ARGS;

    if (aPrefix)
    {
        error = otBorderRouterRemoveOnMeshPrefix(thciGetOtInstance(), aPrefix);
    }

    return error;
}

otError thciAddExternalRoute(const otExternalRouteConfig *aConfig)
{
    otError error = OT_ERROR_INVALID_ARGS;

    if (aConfig)
    {
        error = otBorderRouterAddRoute(thciGetOtInstance(), aConfig);
    }

    return error;
}

otError thciRemoveExternalRoute(const otIp6Prefix *aPrefix)
{
    otError error = OT_ERROR_INVALID_ARGS;

    if (aPrefix)
    {
        error = otBorderRouterRemoveRoute(thciGetOtInstance(), aPrefix);
    }

    return error;
}

otError thciSendServerData(void)
{
    otError error = otBorderRouterRegister(thciGetOtInstance());

    return error;
}

otError thciBecomeRouter(void)
{
    otError error = otThreadBecomeRouter(thciGetOtInstance());

    return error;
}

otError thciBecomeLeader(void)
{
    otError error = otThreadBecomeLeader(thciGetOtInstance());

    return error;
}

#else // THCI_CONFIG_ENABLE_BORDER_ROUTER

otError thciAddBorderRouter(const otBorderRouterConfig *aConfig)
{
    (void)aConfig;
    return OT_ERROR_DISABLED_FEATURE;
}

otError thciRemoveBorderRouter(const otIp6Prefix *aPrefix)
{
    (void)aPrefix;
    return OT_ERROR_DISABLED_FEATURE;
}

otError thciAddExternalRoute(const otExternalRouteConfig *aConfig)
{
    (void)aConfig;
    return OT_ERROR_DISABLED_FEATURE;
}

otError thciRemoveExternalRoute(const otIp6Prefix *aPrefix)
{
    (void)aPrefix;
    return OT_ERROR_DISABLED_FEATURE;
}

otError thciSendServerData(void)
{
    return OT_ERROR_DISABLED_FEATURE;
}

otError thciBecomeRouter(void)
{
    return OT_ERROR_DISABLED_FEATURE;
}

otError thciBecomeLeader(void)
{
    return OT_ERROR_DISABLED_FEATURE;
}

#endif // THCI_CONFIG_ENABLE_BORDER_ROUTER

otError thciAddUnsecurePort(uint16_t aPort)
{
    otError error;

    // Currently, THCI only allows one insecure port at a time, despite Openthread allowing multiple.
    nlREQUIRE_ACTION(THCI_TEST_INSECURE_PORTS(gTHCISDKContext.mSecurityFlags) == false, done, error = OT_ERROR_INVALID_STATE);

    error = otIp6AddUnsecurePort(thciGetOtInstance(), aPort);
    nlREQUIRE(error == OT_ERROR_NONE, done);

    gTHCISDKContext.mSecurityFlags |= THCI_SECURITY_FLAG_INSECURE_PORTS_ENABLED;

 done:
    return error;
}


otError thciRemoveUnsecurePort(uint16_t aPort)
{
    otError error;

    error = otIp6RemoveUnsecurePort(thciGetOtInstance(), aPort);
    nlREQUIRE(error == OT_ERROR_NONE, done);

    gTHCISDKContext.mSecurityFlags &= ~THCI_SECURITY_FLAG_INSECURE_PORTS_ENABLED;

    if (THCI_TEST_INSECURE_SOURCE_PORT(gTHCISDKContext.mSecurityFlags))
    {
        otIp6RemoveUnsecurePort(thciGetOtInstance(), gTHCISDKContext.mInsecureSourcePort);
        gTHCISDKContext.mSecurityFlags &= ~THCI_SECURITY_FLAG_INSECURE_SOURCE_PORT;
    }

 done:
    return error;
}

uint32_t thciGetKeySequenceCounter(void)
{
    uint32_t seq = otThreadGetKeySequenceCounter(thciGetOtInstance());

    return seq;
}


void thciSetKeySequenceCounter(uint32_t aKeySequenceCounter)
{
    otThreadSetKeySequenceCounter(thciGetOtInstance(), aKeySequenceCounter);
}

otError thciBecomeDetached(void)
{
    otError error = otThreadBecomeDetached(thciGetOtInstance());

    return error;
}

otError thciBecomeChild(void)
{
    otError error = otThreadBecomeChild(thciGetOtInstance());

    return error;
}

otDeviceRole thciGetDeviceRole(void)
{
    otDeviceRole role = otThreadGetDeviceRole(thciGetOtInstance());

    return role;
}


otError thciGetLeaderRouterId(uint8_t *aLeaderId)
{
    *aLeaderId = otThreadGetLeaderRouterId(thciGetOtInstance());

    return OT_ERROR_NONE;
}

uint32_t thciGetPollPeriod(void)
{
    uint32_t poll = otLinkGetPollPeriod(thciGetOtInstance());

    return poll;
}

void thciSetPollPeriod(uint32_t aPollPeriod)
{
    otLinkSetPollPeriod(thciGetOtInstance(), aPollPeriod);
}

otError thciLinkAddWhitelist(const uint8_t *aExtAddr)
{
    return otLinkFilterAddAddress(thciGetOtInstance(), (const otExtAddress *)aExtAddr);
}

void thciLinkClearWhitelist(void)
{
    otLinkFilterClearAddresses(thciGetOtInstance());
}

void thciLinkSetWhitelistEnabled(bool aEnabled)
{
    otLinkFilterSetAddressMode(thciGetOtInstance(), aEnabled ? OT_MAC_FILTER_ADDRESS_MODE_WHITELIST : OT_MAC_FILTER_ADDRESS_MODE_DISABLED);
}

otError thciGetLeaderWeight(uint8_t *aWeight)
{
    *aWeight = otThreadGetLeaderWeight(thciGetOtInstance());

    return OT_ERROR_NONE;
}

otError thciGetNetworkDataVersion(uint8_t *aVersion)
{
    *aVersion = otNetDataGetVersion(thciGetOtInstance());

    return OT_ERROR_NONE;
}

otError thciGetPartitionId(uint32_t *aPartitionId)
{
    otError retval = OT_ERROR_NONE;
    nlREQUIRE_ACTION(aPartitionId != NULL, done, retval = OT_ERROR_INVALID_ARGS);

    *aPartitionId = otThreadGetPartitionId(thciGetOtInstance());

done:
    return retval;
}

otError thciGetRloc16(uint16_t *aRloc)
{
    otError retval = OT_ERROR_NONE;
    nlREQUIRE_ACTION(aRloc != NULL, done, retval = OT_ERROR_INVALID_ARGS);

    *aRloc = otThreadGetRloc16(thciGetOtInstance());

done:
    return retval;
}

otError thciGetInstantRssi(int8_t *aRssi)
{
    otError retval = OT_ERROR_NONE;
    nlREQUIRE_ACTION(aRssi != NULL, done, retval = OT_ERROR_INVALID_ARGS);

    *aRssi = otPlatRadioGetRssi(thciGetOtInstance());

done:
    return retval;
}

otError thciGetStableNetworkDataVersion(uint8_t *aVersion)
{
    *aVersion = otNetDataGetStableVersion(thciGetOtInstance());

    return OT_ERROR_NONE;
}

otError thciSetIcmpEchoEnabled(bool aEnable)
{
    otError error = OT_ERROR_NONE;

    otIcmp6SetEchoEnabled(thciGetOtInstance(), aEnable);

    return error;
}

otError thciSendMacDataRequest(void)
{
    otError error;

#ifdef BUILD_FEATURE_DYNAMIC_POLL_RATE_DEBUG
    /* Adding timestamp since some shell apps don't show timestamp automatically. */
    NL_LOG_DEBUG(lrTHCI, "Polling\n");
#endif

    error = otLinkSendDataRequest(thciGetOtInstance());

    return error;
}

otError thciGetMacCounters(otMacCounters *aCounters)
{
    otError error = OT_ERROR_NONE;
    nlREQUIRE_ACTION(aCounters != NULL, done, error = OT_ERROR_INVALID_ARGS);

    const otMacCounters *ot_counters = otLinkGetCounters(thciGetOtInstance());
    memcpy(aCounters, ot_counters, sizeof(otMacCounters));

done:
    return error;
}

otError thciGetIpCounters(otIpCounters *aCounters)
{
    otError error = OT_ERROR_NONE;
    nlREQUIRE_ACTION(aCounters != NULL, done, error = OT_ERROR_INVALID_ARGS);

    const otIpCounters *ot_counters = otThreadGetIp6Counters(thciGetOtInstance());
    memcpy(aCounters, ot_counters, sizeof(otIpCounters));

done:
    return error;
}

bool thciIsSingleton(void)
{
    bool isSingleton = otThreadIsSingleton(thciGetOtInstance());

    return isSingleton;
}

bool thciIsConnected(void)
{
    bool connected = false;

    switch (thciGetLocalDeviceRole())
    {
        case OT_DEVICE_ROLE_CHILD:
        case OT_DEVICE_ROLE_ROUTER:
        case OT_DEVICE_ROLE_LEADER:
            connected = true;
            break;
        default:
            connected = false;
            break;
    }

    return connected;
}

/* Forward received IPv6 packet to LwIP for processing.
 */
static void thciReceiveIp6DatagramCallback(otMessage *aMessage, void *aContext)
{
    struct pbuf *pbuf;
    err_t err;
    uint16_t len;

    (void)aContext;

    len = otMessageGetLength(aMessage);

    // Pass all packets up to LwIP.
    pbuf = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    nlREQUIRE_ACTION(pbuf != NULL, done, NL_LOG_CRIT(lrTHCI, "pbufs exhausted...dropping incoming packet.\n"));

    if (otMessageRead(aMessage, 0, pbuf->payload, len) != len)
    {
        NL_LOG_CRIT(lrTHCI, "%s: Failed to read message.\n", __FUNCTION__);
    }

#ifdef BUILD_FEATURE_THCI_CERT
    thci_cert_rx_corrupt(pbuf);
#endif //BUILD_FEATURE_THCI_CERT

    NL_LOG_DEBUG(lrTHCI, "IP RX len: %u, cksum: 0x%04x\n", len, thciGetChecksum(pbuf));
    NL_LOG_DEBUG(lrTHCI, "from: %s\n", ip6addr_ntoa((const ip6_addr_t*)&((struct ip6_hdr*)(pbuf->payload))->src));  // IPv6 Header Source
    NL_LOG_DEBUG(lrTHCI, "  to: %s\n", ip6addr_ntoa((const ip6_addr_t*)&((struct ip6_hdr*)(pbuf->payload))->dest)); // IPv6 Header Destination

    err = tcpip_input(pbuf, gTHCISDKContext.mNetif[THCI_NETIF_TAG_THREAD]);

    if (err != ERR_OK)
    {
        pbuf_free(pbuf);
    }

 done:
    otMessageFree(aMessage);

    return;
}

/* Called by LwIP for transmission of IP packets.
 *
 * Note that the pbuf q passed in is not owned by this method. Thus, it must
 * be returned with the same ref count with which it came.
 */
#if LWIP_VERSION_MAJOR < 2
static err_t thciLwIPOutputIP6(struct netif *netif, struct pbuf *pbuf, struct ip6_addr *ipaddr)
#else
static err_t thciLwIPOutputIP6(struct netif *netif, struct pbuf *pbuf, const struct ip6_addr *ipaddr)
#endif
{
    err_t retval = ERR_OK;
    otMessage *message = NULL;

    nlREQUIRE_ACTION(pbuf->len <= (NL_THCI_PAYLOAD_MTU), done, retval = ERR_VAL);
    nlREQUIRE_ACTION(netif == gTHCISDKContext.mNetif[THCI_NETIF_TAG_THREAD], done, retval = ERR_IF);
    // If security is enabled, check if the radio is connected before sending packet to OT.
    nlREQUIRE_ACTION(!(THCI_ENABLE_MESSAGE_SECURITY(gTHCISDKContext.mSecurityFlags) && !thciIsConnected()), done, retval = ERR_CONN);

#ifdef BUILD_FEATURE_THCI_CERT
    thci_cert_tx_corrupt(pbuf);
#endif //BUILD_FEATURE_THCI_CERT

    nlREQUIRE_ACTION(0 == CreateOTMessageFromPbuf(pbuf, &message), done, retval = ERR_MEM);
    nlREQUIRE_ACTION(0 == EnqueueMessage(message), done, retval = ERR_INPROGRESS);

    NL_LOG_DEBUG(lrTHCI, "IP TX pbuf_len: %d, ot_len: %u, cksum: 0x%04x\n", pbuf->tot_len, otMessageGetLength(message), thciGetChecksum(pbuf));
    NL_LOG_DEBUG(lrTHCI, "from: %s\n", ip6addr_ntoa((const ip6_addr_t*)&((struct ip6_hdr*)(pbuf->payload))->src));  // IPv6 Header Source
    NL_LOG_DEBUG(lrTHCI, "  to: %s\n", ip6addr_ntoa((const ip6_addr_t*)&((struct ip6_hdr*)(pbuf->payload))->dest)); // IPv6 Header Destination

    nl_eventqueue_post_event(gTHCISDKContext.mInitParams.mSdkQueue, &sOutgoingIPPacketEvent);

 done:
    if (retval != ERR_OK)
    {
        NL_LOG_CRIT(lrTHCI, "Message queue error (%d)...dropping outgoing packet.\n", retval);

        if (message)
        {
            otMessageFree(message);
        }

        // TODO(COM-4102)
        // FIXME: should not silently drop in the case that security is enabled while radio detached.
        if (retval == ERR_CONN)
        {
            retval = ERR_OK;
        }
    }

    return retval;
}

// If the message is TCP then the source port is made insecure so that
// response messages won't be filtered out.
static void OpenSourcePort(otMessage *aMessage)
{
    struct ip6_hdr ip6_hdr;
    uint16_t offset = 0;
    uint16_t len;
    uint16_t src_port;
    otError error = OT_ERROR_NONE;

    len = otMessageRead(aMessage, offset, &ip6_hdr, sizeof(ip6_hdr));
    nlREQUIRE(len == sizeof(ip6_hdr), done);

    offset += len;

    nlREQUIRE_ACTION(IP6H_NEXTH((&ip6_hdr)) == IP6_NEXTH_TCP, done, error = OT_ERROR_INVALID_ARGS);

    len = otMessageRead(aMessage, offset, &src_port, sizeof(src_port));
    nlREQUIRE_ACTION(len == sizeof(src_port), done, error = OT_ERROR_PARSE);

    src_port = lwip_ntohs(src_port);

    NL_LOG_DEBUG(lrTHCI, "Open Port %d\n", src_port);

    error = otIp6AddUnsecurePort(thciGetOtInstance(), src_port);
    nlREQUIRE(error == OT_ERROR_NONE, done);

    gTHCISDKContext.mInsecureSourcePort = src_port;
    gTHCISDKContext.mSecurityFlags |= THCI_SECURITY_FLAG_INSECURE_SOURCE_PORT;

 done:
    if (error != OT_ERROR_NONE)
    {
        NL_LOG_CRIT(lrTHCI, "OpenSourcePort failed with err = %d\n", error);
    }

    return;
}

// Process pbufs on the outgoing queue.
static int OutgoingIPPacketEventHandler(nl_event_t *aEvent, void *aClosure)
{
    otMessage *message;

    // When the Stall is on don't post an event even if message queue is not empty.
    nlREQUIRE(!gTHCISDKContext.mStallOutgoingDataPackets, nopost_exit);

    while(!IsMessageQueueEmpty())
    {
        message = DequeueMessage();

        if (!THCI_ENABLE_MESSAGE_SECURITY(gTHCISDKContext.mSecurityFlags) &&
             THCI_TEST_INSECURE_PORTS(gTHCISDKContext.mSecurityFlags) &&
            !THCI_TEST_INSECURE_SOURCE_PORT(gTHCISDKContext.mSecurityFlags))
        {
            // If True, then this is a device that is joining provisionally.  As such,
            // it is necessary that the source port also be made insecure. We pass the
            // outgoing IP packet to OpenSourcePort so that it can extract the source
            // port from the TCP header and add it to OT's insecure port list.
            OpenSourcePort(message);
        }

#if BUILD_FEATURE_THREAD_IP_TX_CALLOUT
        extern void thread_tx_packet_indicator(uint16_t packetLength);

        // Indicate that the radio is about to transmit a data packet.
        thread_tx_packet_indicator(otMessageGetLength(message));
#endif

        otIp6Send(thciGetOtInstance(), message);
    }

 nopost_exit:
    return NLER_SUCCESS;
}

otError thciNetifInit(struct netif *aNetif, thci_netif_tags_t aTag, const char * aInterfaceName)
{
    otError retval = OT_ERROR_INVALID_ARGS;

    nlREQUIRE(aTag < THCI_NETIF_TAG_COUNT, done);
    nlREQUIRE(sizeof(aNetif->name) == strlen(aInterfaceName), done);

    memcpy(&aNetif->name[0], aInterfaceName, strlen(aInterfaceName)); // intentionally omits the null term in the copy.
#if LWIP_IPV4 || LWIP_VERSION_MAJOR < 2
    aNetif->output = NULL;
#endif /* LWIP_IPV4 || LWIP_VERSION_MAJOR < 2 */
#if LWIP_IPV6
    aNetif->output_ip6 = thciLwIPOutputIP6;
#endif /* LWIP_IPV6 */

    aNetif->linkoutput = NULL;
    aNetif->flags = NETIF_FLAG_BROADCAST;
    aNetif->mtu = NL_THCI_PAYLOAD_MTU;

    // mNetif is used by THCI task which may lead to race conditions
    gTHCISDKContext.mNetif[aTag] = aNetif;

    retval = OT_ERROR_NONE;

 done:
    return retval;
}

otError thciGetParentAverageRssi(int8_t *aParentRssi)
{
    return otThreadGetParentAverageRssi(thciGetOtInstance(), aParentRssi);
}

otError thciGetParentLastRssi(int8_t *aLastRssi)
{
    return otThreadGetParentLastRssi(thciGetOtInstance(), aLastRssi);
}

otError thciDiagnosticsCommand(const char *aCommandString)
{
#if BUILD_FEATURE_OPENTHREAD_DIAGS
    char *output = NULL;

    output = otDiagProcessCmdLine(aCommandString);

    NL_LOG_CRIT(lrTHCI, "%s\n", output);

    return OT_ERROR_NONE;
#else
    (void)aCommandString;

    NL_LOG_DEBUG(lrTHCI, "WARNING: %s, OpenThread diag feature is not enabled.\n", __FUNCTION__);

    return OT_ERROR_DISABLED_FEATURE;
#endif
}

/**
 * UNIMPLEMENTED
 */

otError thciGetLocalLeaderWeight(uint8_t *aWeight)
{
    (void)aWeight;

    return OT_ERROR_NOT_IMPLEMENTED;
}

otError thciGetPreferredRouterId(uint8_t *aRouterId)
{
    (void)aRouterId;

    return OT_ERROR_NOT_IMPLEMENTED;
}

otError thciGetLeaderAddress(otIp6Address *aLeaderAddr)
{
    (void)aLeaderAddr;

    return OT_ERROR_NOT_IMPLEMENTED;
}

otError thciGetNetworkData(uint8_t *aNetworkData, uint16_t aInSize, uint16_t *aOutSize)
{
    otError retval = OT_ERROR_NONE;
    const bool aStable = false;

    nlREQUIRE_ACTION(aOutSize != NULL, done, retval = OT_ERROR_INVALID_ARGS);
    *aOutSize = aInSize;

    retval = otNetDataGet(thciGetOtInstance(), aStable, aNetworkData, aOutSize);

done:
    return retval;
}

otError thciGetStableNetworkData(uint8_t *aNetworkData, uint16_t aInSize, uint16_t *aOutSize)
{
    (void)aNetworkData;
    (void)aInSize;

    if (aOutSize)
    {
        *aOutSize = 0;
    }

    return OT_ERROR_NOT_IMPLEMENTED;
}

otError thciSetSteeringData(const uint8_t *aSteeringDataAddr)
{
    (void)aSteeringDataAddr;

    return OT_ERROR_NOT_IMPLEMENTED;
}

void thciInitiateNCPRecovery(void)
{
    // not implemented on SOC builds.
}

void thciStallOutgoingDataPackets(bool aEnable)
{
    if (gTHCISDKContext.mStallOutgoingDataPackets != aEnable)
    {
        gTHCISDKContext.mStallOutgoingDataPackets = aEnable;

        if (!gTHCISDKContext.mStallOutgoingDataPackets && !IsMessageQueueEmpty())
        {
            // post an event to restart the flow of outgoing packets.
            nl_eventqueue_post_event(gTHCISDKContext.mInitParams.mSdkQueue, &sOutgoingIPPacketEvent);
        }
    }
}

otError thciSetLegacyPrefix(const uint8_t *aLegacyPrefix, uint8_t aPrefixLength)
{
    // Legacy ULA is not currently supported in the SOC configuration.
    (void)aLegacyPrefix;
    (void)aPrefixLength;

    NL_LOG_CRIT(lrTHCI, "WARNING: Call to unimplemented API %s\n", __FUNCTION__);

    return OT_ERROR_NOT_IMPLEMENTED;
}

#endif /* THCI_CONFIG_USE_OPENTHREAD_ON_NCP == 0 */
