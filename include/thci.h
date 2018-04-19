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
 *      Implements Thread Host Control Interface API.
 *
 */

#ifndef __THCI_H_INCLUDED__
#define __THCI_H_INCLUDED__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <nlertime.h>
#include <nlerevent.h>
#include <nlereventqueue.h>
#include <nlereventpooled.h>

#include <lwip/ip6.h>
#include <lwip/netif.h>

#include <thci_config.h>

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP
#include <openthread/types.h>
#include <openthread/instance.h>
#else
#include <openthread/openthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * thci_netif_tags_t are used to ID the lwip netif's that THCI supports/tracks.
 */
typedef enum
{
    THCI_NETIF_TAG_THREAD   = 0,
#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
    THCI_NETIF_TAG_LEGACY,
#endif
    THCI_NETIF_TAG_COUNT
} thci_netif_tags_t;

//TODO: It may be better to define this in nlopenthread
//      as it will be there that these events originate.
//      THCI is just a middle man. For now to get it to 
//      compile they are defined here.
typedef enum
{
    THCI_WAKEUP_EVENT_OVER_THE_AIR = 0,
    THCI_WAKEUP_EVENT_APPLICATION  = 1,
    THCI_WAKEUP_EVENT_COMPLETE     = 2,
} thci_legacy_wake_event_t;

/**
 * THCI initialization parameters
 */
typedef struct
{
    nl_eventqueue_t mSdkQueue;    /* queue used by the sdk to receive events. */
} thci_init_params_t;

/**
 * This function pointer is called to notify certain configuration or state changes within OpenThread.
 *
 * @param[in]  aFlags    A bit-field indicating specific state that has changed.
 * @param[in]  aContext  A pointer to application-specific context.
 *
 */
typedef void (OTCALL *thciStateChangedCallback)(uint32_t aFlags, void *aContext);

/**
 * This callback is called to notify when a Legacy ULA has been registered with OpenThread.
 *
 * @param[in]  aUlaPrefix   The new legacy ULA prefix.
 * @param[in]  aContext     A pointer to application-specific context.
 *
 */
typedef void (*thcilegacyUlaCallback)(const uint8_t *aUlaPrefix);

/**
 * This callback is called to notify that the NCP was reset.
 *
 */
typedef void (*thciResetRecoveryCallback)(void);

/**
 * This callback is called to notify certain configuration or state changes within OpenThread.
 *
 * @param[in]  aEvent            The event code that describes the reason for the wake event.
 * @param[in]  aTimeRemainingMs  The time remaining in msec before the wake operation completes.
 * @param[in]  aReason           The data byte provided in the received wake frame. Used to communicate 
 *                               the reason code for the network wake.
 *
 */
typedef void (*thciLurkerWakeCallback)(thci_legacy_wake_event_t aEvent, uint16_t aTimeRemainingMs, uint8_t aReason);

/**
 * thci_callbacks_t is used to provide callback pointers during THCI Initialization.
 */
typedef struct
{
    thciStateChangedCallback    mStateChangeCallback;
    thcilegacyUlaCallback       mLegacyUlaCallback;
    thciResetRecoveryCallback   mResetRecoveryCallback;
#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
    thciLurkerWakeCallback      mLurkerWakeCallback;
#endif
} thci_callbacks_t;

/**
 * thci_network_params_t is used to query the Thread Network Parameters.
 */
typedef struct
{
    char                mNetworkName[OT_NETWORK_NAME_MAX_SIZE];
    otPanId             mPanId;
    uint8_t             mExtPanId[OT_EXT_PAN_ID_SIZE];
    otShortAddress      mShortAddress;
    otExtAddress        mExtAddress;
    otDeviceRole        mRole;
    uint8_t             mChannel;
    uint32_t            mPartitionId;
    otLinkModeConfig    mMode;
} thci_network_params_t;

/**
 * This structure is the merger of otNeighborInfo and otChildInfo,
 * since every child will have a neighbour entry.  If mNeighborInfo.mIsChild == 0
 * then mTimeout, mChildId, and mNetworkDataVersion should be ignored.
 */
typedef struct
{
    otNeighborInfo mNeighborInfo;

    // Info from child table
    uint32_t mTimeout;
    uint16_t mChildId;
    uint8_t  mNetworkDataVersion;
    bool     mFoundChild;

} thci_neighbor_child_info_t;

#define THCI_LEGACY_ULA_SIZE_BYTES (8)

/**
 * Initialize THCI.
 *
 * This initializes the THCI core implementation and then implementation
 * specific module: NCP or SOC.
 *
 * It is up to the THCI module to initialize underlying layers, including
 * OpenThread and any platform specific code.
 *
 * @param[in] A pointer to thci_init_params_t struct.
 *
 * @retval 0  If initialization succeeded, error code otherwise.
 */
int thciSDKInit(thci_init_params_t *aInitParams);

/**
 * Check if Thci was initialized.
 *
 * @return true if Thci was initialized.
 */
int thciInitialized(void);

/**
 * Initialize / Enable the Thread Module.
 *
 * @param[in] aCallbacks   Pointer to the set of callbacks used by THCI to 
 *                         announce asynchronous events to the application.
 *
 * @retval OT_ERROR_NONE  Successfully initialized the Thread Module.
 */
otError thciInitialize(thci_callbacks_t *aCallbacks);

/**
 * Finalize / Disable the Thread Module.
 *
 * @retval OT_ERROR_NONE  Successfully disabled the Thread interface.
 */
otError thciFinalize(void);

/**
 * This function brings up the IPv6 interface.
 *
 * Call this function to bring up the IPv6 interface and enables IPv6 communication.
 *
 * @retval OT_ERROR_NONE           Successfully enabled the IPv6 interface.
 * @retval OT_ERROR_INVALID_STATE  OpenThread is not enabled or the IPv6 interface is already up.
 *
 */
otError thciInterfaceUp(void);

/**
 * This function brings down the IPv6 interface.
 *
 * Call this function to bring down the IPv6 interface and disable all IPv6 communication.
 *
 * @retval OT_ERROR_NONE           Successfully brought the interface down.
 * @retval OT_ERROR_INVALID_STATE  The interface was not up.
 *
 */
otError thciInterfaceDown(void);

/**
 * This function indicates whether the IPv6 interface is enabled or not
 *
 * @param[in]  aEnabled  Indicates if the interface is enabled or not
 *
 * @retval OT_ERROR_NONE           Successfully fetched interface state
 * @retval OT_ERROR_INVALID_STATE  Unknown interface state
 *
 */
otError thciIsInterfaceEnabled(bool *aEnabled);

/**
 * This function starts Thread protocol operation.
 *
 * The interface must be up when calling this function.
 *
 * @retval OT_ERROR_NONE           Successfully started Thread protocol operation.
 * @retval OT_ERROR_INVALID_STATE  Thread protocol operation is already started or the interface is not up.
 *
 */
otError thciThreadStart(void);

/**
 * This function stops Thread protocol operation.
 *
 * @retval OT_ERROR_NONE           Successfully stopped Thread protocol operation.
 * @retval OT_ERROR_INVALID_STATE  The Thread protocol operation was not started.
 *
 */
otError thciThreadStop(void);

/**
 * This function indicates if the node has been commissioned or not.
 *
 * @param[out]  aCommissioned    Return value indicating if node is commissioned or not.
 *
 * @retval OT_ERROR_NONE           Successfully found if not is commissioned or not.
 * @retval OT_ERROR_INVALID_STATE  Failed to get node commissioned state.
 *
 */
otError thciIsNodeCommissioned(bool *aCommissioned);

/**
 * This function searches for credentials in the legacy region of NCP non-volatile.
 * If valid credentials are found, they are used to configure Openthread. These
 * credentials include; Network-ID, pan-ID, extended PAN-ID, channel, and master 
 * security key. This API is intended to be used by host images which update the NCP 
 * firmware from a non-OpenThread image to an OpenThread image.
 *
 * @param[out]  aResult     Return value indicating the status of the recovery operation.
 *                          OT_ERROR_NONE - The credentials were found and used to configure OpenThread.
 *                          OT_ERROR_FAILED - The credentials were found but configuring OpenThread failed.
 *                          OT_ERROR_NOT_FOUND - The credentials could not be found.
 *
 * @retval OT_ERROR_NONE                 Successfully Initiated legacy credential recovery,
                                         see aResult for the result of the recovery.
 * @retval OT_ERROR_INVALID_ARGS         aResult is not valid.
 * @retval OT_ERROR_INVALID_STATE        The THCI module is not initialized.
 * @retval OT_ERROR_PARSE                Failed to parse the result from OpenThread.
 *
 */
otError thciRecoverLegacyCredentials(otError *aResult);

/**
 * This function erases credentials in the legacy region of NCP non-volatile.
 * After credentials have been recovered using thciRecoverLegacyCredentials they 
 * should be erased to prevent them from ever being used again. 
 * This API is intended to be used by host images which update the NCP 
 * firmware from a non-OpenThread image to an OpenThread image.
 *
 * @param[out]  aResult     Return value indicating the status of the erase operation.
 *                          OT_ERROR_NONE - The credentials were erased
 *                          OT_ERROR_FAILED - The credentials were not erased.
 *
 * @retval OT_ERROR_NONE                 Successfully Initiated legacy credential erase,
                                         see aResult for the result of the erase.
 * @retval OT_ERROR_INVALID_ARGS         aResult is not valid.
 * @retval OT_ERROR_INVALID_STATE        The THCI module is not initialized.
 * @retval OT_ERROR_PARSE                Failed to parse the result from OpenThread.
 *
 */
otError thciEraseLegacyCredentials(otError *aResult);

/**
 * This method deletes all the settings stored on non-volatile memory, and then triggers platform reset.
 *
 * @retval OT_ERROR_NONE  Succeeded in reseting State back to Factory settings, error code otherwise.
 */
otError thciFactoryReset(void);

/**
 * This method erases all the settings stored on non-volatile memory.
 *
 * @retval OT_ERROR_NONE           All data was wiped successfully.
 * @retval OT_ERROR_INVALID_STATE  Device is not in `disabled` state/role.
 *
 */
otError thciPersistentInfoErase(void);

/**
 * This function indicates whether or not Thread control traffic is filtered out when delivering IPv6 datagrams
 *
 * @param[in]  aEnabled  TRUE to enable filter, FALSE to disable.
 *
 * @retval OT_ERROR_NONE  Succeeded in setting Ip6 datagram filter.
 */
otError thciSetReceiveIp6DatagramFilterEnabled(bool aEnabled);

/**
 * This function pointer is called during an IEEE 802.15.4 Active Scan when an IEEE 802.15.4 Beacon is received or
 * the scan completes.
 *
 * @param[in]  aResult   A valid pointer to the beacon information or NULL when the active scan completes.
 * @param[in]  aContext  A pointer to application-specific context.
 *
 */
typedef void (*thciHandleActiveScanResult)(otActiveScanResult *aResult, void *aContext);


/**
 * This function starts an IEEE 802.15.4 Active Scan
 *
 * @param[in]  aScanChannels  A bit vector indicating which channels to scan.
 * @param[in]  aScanDuration  The time in milliseconds to spend scanning each channel.
 * @param[in]  aCallback      A pointer to a function that is called when a beacon is received or the scan completes.
 * @param[in]  aContext       A pointer to application-specific context.
 *
 * @retval OT_ERROR_NONE      Accepted the Active Scan request, error code otherwise.
 */
otError thciActiveScan(uint32_t aScanChannels, uint16_t aScanDuration, thciHandleActiveScanResult aCallback, void *aContext);

/**
 * This function starts an MLE Scan
 *
 * @param[in]  aScanChannels  A bit vector indicating which channels to scan.
 * @param[in]  aJoiner        Value of the Joiner Flag in the Discovery Request TLV.
 * @param[in]  aEnableEUI64Filtering  Enable filtering out of MLE discovery responses that don't match factory assigned EUI64.
 * @param[in]  aCallback      A pointer to a function that is called when a beacon is received or the scan completes.
 * @param[in]  aContext  A pointer to application-specific context.
 *
 * @retval kThreadError_None  Accepted the MLE Scan request, error code otherwise.
 */
otError thciDiscover(uint32_t aScanChannels, bool aJoiner, bool aEnableEUI64Filtering, thciHandleActiveScanResult aCallback, void *aContext);

/**
 * Acquire the set of Network Parameters used by OpenThread.
 *
 * @param[out]  aNetworkParams  a Set of Network parameters to be filled.
 *
 * @retval OT_ERROR_NONE  filled aNetworkParams, error code otherwise.
 */
otError thciGetNetworkParams(thci_network_params_t *aNetworkParams);


/**
 * Get the IEEE 802.15.4 Extended Address.
 *
 * @param[in] A pointer to a byte array capable of holding the requested address.
 *
 * @retval OT_ERROR_NONE filled aAddress, error code otherwise.
 */
otError thciGetExtendedAddress(uint8_t *aAddress);


/**
 * Get the IEEE 802.15.4 channel.
 *
 * @param[out] A pointer to a uint8 to deposit the channel value in.
 *
 * @retval OT_ERROR_NONE  Accepted the Get Channel request, error code otherwise.
 *
 * @sa thciSetChannel
 */
otError thciGetChannel(uint8_t *aChannel);


/**
 * Set the IEEE 802.15.4 channel
 *
 * @param[in]  aChannel  The IEEE 802.15.4 channel.
 *
 * @retval  OT_ERROR_NONE  Accepted the Set Channel request.
 *
 * @sa thciGetChannel
 */
otError thciSetChannel(uint16_t aChannel);


/**
 * Get the Thread Child Timeout used when operating in the Child role.
 *
 * @retval    The Thread Child Timeout value.
 *
 * @sa thciSetChildTimeout
 */
uint32_t thciGetChildTimeout(void);


/**
 * Set the Thread Child Timeout used when operating in the Child role.
 *
 * @param[in]  aTimeout    A child timeout.
 *
 * @sa thciSetChildTimeout
 */
void thciSetChildTimeout(uint32_t aTimeout);


/**
 * Get the IEEE 802.15.4 Extended PAD ID.
 *
 * @retval A pointer to the IEEE 802.15.4 Extended PAN ID.
 *
 * @sa thciSetExtendedPanId
 */
const uint8_t *thciGetExtendedPanId(void);


/**
 * Set the IEEE 802.15.4 Extended PAN ID.
 *
 * @param[in]  aExtendedPanId  A pointer to the IEEE 802.15.4 Extended PAN ID.
 *
 * @retval  OT_ERROR_NONE   Accepted the Set Extended Pan Id request.
 *
 * @sa thciGetExtendedPanId
 */
otError thciSetExtendedPanId(const uint8_t *aExtendedPanId);


/**
 * Get the MLE Link Mode configuration.
 *
 * @returns The MLE Link Mode configuration.
 *
 * @sa thciSetLinkMode
 */
otLinkModeConfig thciGetLinkMode(void);


/**
 * Set the MLE Link Mode configuration.
 *
 * @param[in]  aConfig  A pointer to the Link Mode configuration.
 *
 * @retval  OT_ERROR_NONE         Accepted the Set MLE Link Mode request.
 *
 * @sa thciGetLinkMode
 */
otError thciSetLinkMode(otLinkModeConfig aMode);


/**
 * Get the thciGetMasterKey.
 *
 * @param[out]  aKeyLength  A pointer to an unsigned 8-bit value that the function will set to the number of bytes that
 *                          represent the Thread Master key. Caller may set to NULL.
 *
 * @retval A pointer to a buffer containing the Thread Master key.
 *
 * @sa thciSetMasterKey
 */
const uint8_t *thciGetMasterKey(uint8_t *aKeyLength);


/**
 * Set the Thread Master key.
 *
 * @param[out]  aKeyLength  A pointer to an unsigned 8-bit value that the function will set to the number of bytes that
 *                          represent the Thread Master key. Caller may set to NULL.
 *
 * @retval  OT_ERROR_NONE         Accepted the Set Master Key request.
 *
 * @sa thciSetMasterKey
 */
otError thciSetMasterKey(const void *aKey, uint8_t aKeyLength);


/**
 * Get the Thread Network Name.
 *
 * @retval A pointer to the Thread Network Name.
 *
 * @sa thciSetNetworkName
 */
const char *thciGetNetworkName(void);


/**
 * Set the Thread Network Name.
 *
 * @param[in]  aNetworkName  A pointer to the Thread Network Name.
 *
 * @retval  OT_ERROR_NONE   Accepted the Set Thread Network Name request.
 *
 * @sa thciGetNetworkName
 */
otError thciSetNetworkName(const char *aNetworkName);


/**
 * Get the IEEE 802.15.4 PAN ID.
 *
 * @retval The IEEE 802.15.4 PAN ID.
 *
 * @sa thciSetPanId
 */
otPanId thciGetPanId(void);


/**
 * Set the IEEE 802.15.4 PAN ID.
 *
 * @param[in]  aPanId  The IEEE 802.15.4 PAN ID.
 *
 * @retval  OT_ERROR_NONE   Accepted the Set PAN ID request.
 *
 * @sa thciGetPanId
 */
otError thciSetPanId(otPanId aPanId);

/**
 * Set the maximum radio transmit power
 *
 * @param[in]  aPower  TX power
 *
 * @sa thciSetMaxTxPower
 */
void thciSetMaxTxPower(int8_t aPower);

/**
 * Issue a OpenThread Diagnostics command string
 *
 * @param[in]  aCommandString  The command string which gets passed to diagProcessCmdLine
 *
 * @retval OT_ERROR_NONE on Success, error code otherwise.
 */
otError thciDiagnosticsCommand(const char *aCommandString);

/**
 * Get the list of unicast IPv6 addresses assigned to the Thread interface.
 *
 * @retval A pointer to the first Network Interface Address.
 */
const otNetifAddress *thciGetUnicastAddresses(void);

/**
 * Get the list of multicast IPv6 addresses assigned to the Thread interface.
 *
 * @retval A pointer to the first multicast address.
 */
const otNetifMulticastAddress *thciGetMulticastAddresses(void);

/**
 * Add a Network Interface Address to the Thread interface.
 *
 * @param[in]  aAddress  A pointer to a Network Interface Address.
 *
 * @retval  OT_ERROR_NONE   Accepted the Add Unicast Address request.
 */
otError thciAddUnicastAddress(otNetifAddress *aAddress);


/**
 * Remove a Network Interface Address from the Thread interface.
 *
 * @param[in]  aAddress  A pointer to a Network Interface Address.
 *
 * @retval  OT_ERROR_NONE   Accepted the Remove Unicast Address request.
 */
otError thciRemoveUnicastAddress(otIp6Address *aAddress);

/**
 * Set the legacy prefix using the provided parameters
 *
 * @param[in]  aLegacyPrefix  A byte array containing the legacy prefix.
 * @param[in]  aPrefixLength  The length in bits of the prefix (64).
 *
 * @retval  OT_ERROR_NONE  The prefix was set successfully, error code otherwise.
 */
otError thciSetLegacyPrefix(const uint8_t *aLegacyPrefix, uint8_t aPrefixLength);

/**
 * Get the Thread Local Leader Weight used when operating in the Leader role.
 *
 * @param[out]  aWeight  The Thread Local Leader Weight value.
 *
 * @retval OT_ERROR_NONE  Successfully got the thead local leader weight.
 *
 * @sa thciSetLocalLeaderWeight
 */
otError thciGetLocalLeaderWeight(uint8_t *aWeight);

/**
 * Set the Thread Leader Weight used when operating in the Leader role.
 *
 * @param[in]  aWeight   The Thread Leader Weight value.
 *
 *
 * @retval OT_ERROR_NONE  Successfully set the thead local leader weight.
 *
 * @sa thciGetLeaderWeight
 */
otError thciSetLocalLeaderWeight(uint8_t aWeight);

/**
 * Add a border router configuration to the local network data.
 *
 * @param[in]  aConfig  A pointer to the border router configuration.
 *
 * @retval  OT_ERROR_NONE   Accepted the Add Border Router request.
 *
 * @sa thciRemoveBorderRouter
 * @sa thciSendServerData
 */
otError thciAddBorderRouter(const otBorderRouterConfig *aConfig);


/**
 * Remove a border router configuration from the local network data.
 *
 * @param [in]  aPrefix  A pointer to an IPv6 prefix.
 *
 * @retval  OT_ERROR_NONE   Accepted the Remove Border Router request.
 *
 * @sa thciAddBorderRouter
 * @sa thciSendServerData
 */
otError thciRemoveBorderRouter(const otIp6Prefix *aPrefix);


/**
 * Add an external route configuration to the local network data.
 *
 * @param[in]  aConfig  A pointer to the external route configuration.
 *
 * @retval  OT_ERROR_NONE   Accepted the Add External Route request.
 *
 * @sa thciRemoveExternalRoute
 * @sa thciSendServerData
 */
otError thciAddExternalRoute(const otExternalRouteConfig *aConfig);


/**
 * Remove an external route configuration from the local network data.
 *
 * @param[in]  aPrefix  A pointer to an IPv6 prefix.
 *
 * @retval  OT_ERROR_NONE   Accepted the Remove External Route request.
 *
 * @sa thciAddExternalRoute
 * @sa thciSendServerData
 */
otError thciRemoveExternalRoute(const otIp6Prefix *aPrefix);


/**
 * Immediately register the local network data with the Leader.
 *
 * @retval  OT_ERROR_NONE   Accepted the Send Server Data request.
 *
 * @sa thciAddBorderRouter
 * @sa thciRemoveBorderRouter
 * @sa thciAddExternalRoute
 * @sa thciRemoveExternalRoute
 */
otError thciSendServerData(void);


/**
 * This function adds a port to the allowed unsecured port list.
 *
 * @param[in]  aPort  The port value.
 *
 * @retval OT_ERROR_NONE     The port was successfully added to the allowed unsecure port list.
 * @retval OT_ERROR_NO_BUFS  The unsecure port list is full.
 *
 */
otError thciAddUnsecurePort(uint16_t aPort);


/**
 * This function removes a port from the allowed unsecure port list.
 *
 * @param[in]  aPort  The port value.
 *
 * @retval OT_ERROR_NONE       The port was successfully removed from the allowed unsecure port list.
 * @retval OT_ERROR_NOT_FOUND  The port was not found in the unsecure port list.
 *
 */
otError thciRemoveUnsecurePort(uint16_t aPort);

/**
 * This function sets steering data
 *
 * @param[in]  aSteeringDataAddr All zeros clears steering data
 *                               All 0xFFs sets steering data to 0xFF
 *                               Anything else is used to compute the bloom filter
 *
 * @retval OT_ERROR_NONE         The steering data was successfully set
 * @retval OT_ERROR_NOT_FOUND    The steering data was not successfully set
 *
 */
otError thciSetSteeringData(const uint8_t *aSteeringDataAddr);

/**
 * Get the CONTEXT_ID_REUSE_DELAY parameter used in the Leader role.
 *
 * @param[out]  aDelay   The CONTEXT_ID_REUSE_DELAY value storage memory.
 *
 * @retval OT_ERROR_NONE         Successfully acquired the context reuse delay.
 *
 * @sa thciSetContextIdReuseDelay
 */
otError thciGetContextIdReuseDelay(uint32_t *aDelay);

/**
 * Set the CONTEXT_ID_REUSE_DELAY parameter used in the Leader role.
 *
 * @param[in]  aDelay  The CONTEXT_ID_REUSE_DELAY value.
 *
 * @retval OT_ERROR_NONE         Successfully set the context reuse delay.
 *
 * @sa thciGetContextIdReuseDelay
 */
otError thciSetContextIdReuseDelay(uint32_t aDelay);

/**
 * Get the thrKeySequenceCounter.
 *
 * @retval The thrKeySequenceCounter value.
 *
 * @sa thciSetKeySequenceCounter
 */
uint32_t thciGetKeySequenceCounter(void);

/**
 * Set the thrKeySequenceCounter.
 *
 * @param[in]  aKeySequenceCounter  The thrKeySequenceCounter value.
 *
 * @sa thciGetKeySequenceCounter
 */
void thciSetKeySequenceCounter(uint32_t aKeySequenceCounter);

/**
 * Get the NETWORK_ID_TIMEOUT parameter used in the Router role.
 *
 * @param[out]  aTimeout   The NETWORK_ID_TIMEOUT value storage memory.
 * 
 * @retval OT_ERROR_NONE on success, error code otherwise.
 *
 * @sa thciSetNetworkIdTimeout
 */
otError thciGetNetworkIdTimeout(uint8_t *aTimeout);

/**
 * Set the NETWORK_ID_TIMEOUT parameter used in the Leader role.
 *
 * @param[in]  aTimeout  The NETWORK_ID_TIMEOUT value.
 *
 * @retval OT_ERROR_NONE on success, error code otherwise.
 *
 * @sa thciGetNetworkIdTimeout
 */
otError thciSetNetworkIdTimeout(uint32_t aTimeout);

/**
 * Get the ROUTER_UPGRADE_THRESHOLD parameter used in the REED role.
 *
 * @param[out]  aTimeout   The ROUTER_UPGRADE_THRESHOLD value storage memory.
 * 
 * @retval OT_ERROR_NONE on success, error code otherwise.
 *
 * @sa thciSetRouterUpgradeThreshold
 */
otError thciGetRouterUpgradeThreshold(uint8_t *aThreshold);

/**
 * Set the ROUTER_UPGRADE_THRESHOLD parameter used in the Leader role.
 *
 * @param[in]  aThreshold  The ROUTER_UPGRADE_THRESHOLD value.
 *
 * @retval OT_ERROR_NONE on success, error code otherwise.
 *
 * @sa thciGetRouterUpgradeThreshold
 */
otError thciSetRouterUpgradeThreshold(uint8_t aThreshold);

/**
 * Release a Router ID that has been allocated by the device in the Leader role.
 *
 * @param[in]  aRouterId  The Router ID to release. Valid range is [0, 62].
 *
 * @retval  OT_ERROR_NONE   Accepted the Release Router Id request.
 */
otError thciReleaseRouterId(uint8_t aRouterId);

/**
 * Detach from the Thread network.
 *
 * @retval  OT_ERROR_NONE   Accepted the Become Detached request.
 */
otError thciBecomeDetached(void);


/**
 * Attempt to reattach as a child.
 *
 *
 * @retval  OT_ERROR_NONE   Accepted the Become Child request.
 */
otError thciBecomeChild(void);


/**
 * Attempt to become a router.
 *
 * @retval  OT_ERROR_NONE   Accepted the Become Router request.
 */
otError thciBecomeRouter(void);


/**
 * Become a leader and start a new partition.
 *
 * @retval  OT_ERROR_NONE   Accepted the Become Leader request.
 */
otError thciBecomeLeader(void);


/**
 * Get the device role.
 *
 * @retval ::OT_DEVICE_ROLE_DISABLED  The Thread stack is disabled.
 * @retval ::OT_DEVICE_ROLE_DETACHED  The device is not currently participating in a Thread network/partition.
 * @retval ::OT_DEVICE_ROLE_CHILD     The device is currently operating as a Thread Child.
 * @retval ::OT_DEVICE_ROLE_ROUTER    The device is currently operating as a Thread Router.
 * @retval ::OT_DEVICE_ROLE_LEADER    The device is currently operating as a Thread Leader.
 */
otDeviceRole thciGetDeviceRole(void);

/**
 * Set local device role
 */
void thciSetLocalDeviceRole(void);

/**
 * Get local device role
 *
 * @retval :: Local device role
 */
otDeviceRole thciGetLocalDeviceRole(void);

/**
 * Get the Leader's Router ID.
 *
 * @param[out] The Leader's Router ID.
 *
 * @retval OT_ERROR_NONE
 */
otError thciGetLeaderRouterId(uint8_t *aLeaderId);


/**
 * Get the data poll period of sleepy end device.
 *
 * @retval  The data poll period of sleepy end device.
 *
 * @sa thciSetPollPeriod
 */
uint32_t thciGetPollPeriod(void);


/**
 * Set the data poll period for sleepy end device.
 *
 * @param[in]  aPollPeriod  data poll period.
 *
 * @sa thciGetPollPeriod
 */
void thciSetPollPeriod(uint32_t aPollPeriod);

/**
 * Add an IEEE 802.15.4 Extended Address to the MAC whitelist.
 *
 * @param[in]  aExtAddr  A pointer to the IEEE 802.15.4 Extended Address.
 *
 * @retval OT_ERROR_NONE    Successfully added to the MAC whitelist.
 * @retval kThreadErrorNoBufs  No buffers available for a new MAC whitelist entry.
 *
 */
otError thciLinkAddWhitelist(const uint8_t *aExtAddr);

/**
 * Remove all entries from the MAC whitelist.
 *
 */
void thciLinkClearWhitelist(void);

/**
 * Enable MAC whitelist filtering.
 *
 * @param[in]  aEnabled   TRUE to enable the whitelist, FALSE otherwise.
 *
 */
void thciLinkSetWhitelistEnabled(bool aEnabled);

/**
 * Get the Thread Leader Weight used when operating in the Leader role.
 *
 * @param[out]  aWeight  The Thread Leader Weight value.
 *
 * @retval OT_ERROR_NONE  Successfully got the thead leader weight.
 *
 * @sa thciSetLeaderWeight
 */
otError thciGetLeaderWeight(uint8_t *aWeight);

/**
 * Get the Network Data Version.
 *
 * @param[out]  aVersion  The network data version.
 *
 * @retval OT_ERROR_NONE  Successfully got the network data version.
 */
otError thciGetNetworkDataVersion(uint8_t *aVersion);

/**
 * Get the Partition ID.
 *
 * @retval The Partition ID.
 */
otError thciGetPartitionId(uint32_t *aPartitionId);


/**
 * Get the RLOC16.
 *
 * @param[out] The devices RLOC16.
 *
 * @retval OT_ERROR_NONE
 */
otError thciGetRloc16(uint16_t *aRloc);

/**
 * Get the current Router ID Sequence.
 *
 * @param[out] aSequence  The Router ID Sequence.
 *
 * @retval OT_ERROR_NONE  Successfully got the router ID Sequence.
 */
otError thciGetRouterIdSequence(uint8_t *aSequence);

/**
 * Get the Stable Network Data Version.
 *
 * @param[out]  aVersion  The stable network data version.
 *
 * @retval OT_ERROR_NONE  Successfully got the stable network data version.
 */
otError thciGetStableNetworkDataVersion(uint8_t *aVersion);


/**
 * This function sets whether or not ICMPv6 Echo processing is enabled.
 *
 * @param[in]  aEnabled  TRUE to enable ICMPv6 Echo processing, FALSE otherwise.
 *
 */
otError thciSetIcmpEchoEnabled(bool aEnable);


/**
 * This function enqueues an IEEE 802.15.4 Data Request message for transmission.
 *
 * @retval OT_ERROR_NONE      Successfully enqueued an IEEE 802.15.4 Data Request message.
 * @retval OT_ERROR_ALREADY   An IEEE 802.15.4 Data Request message is already enqueued.
 * @retval OT_ERROR_NO_BUFS   Insufficient message buffers available.
 */
otError thciSendMacDataRequest(void);


/**
 * Get the current state of otMacCounters.
 *
 * @param[out]  aCounters         A pointer to a counter struct to copy data to.
 *
 * @retval OT_ERROR_NONE          Successfully got counters.
 * @retval OT_ERROR_INVALID_ARGS  Passed in a null pointer.
 *
 */
otError thciGetMacCounters(otMacCounters *aCounters);


/**
 * Get the current state of otIpCounters.
 *
 * @param[out]  aCounters         A pointer to a counter struct to copy data to.
 *
 * @retval OT_ERROR_NONE          Successfully got counters.
 * @retval OT_ERROR_INVALID_ARGS  Passed in a null pointer.
 *
 */
otError thciGetIpCounters(otIpCounters *aCounters);


/**
 * This function indicates whether a node is the only router on the network.
 *
 * @retval TRUE   It is the only router in the network.
 * @retval FALSE  It is a child or is not a single router in the network.
 *
 */
bool thciIsSingleton(void);

/**
 * This function indicates whether a node is connected to the network or not
 *
 * @retval TRUE   If device role is OT_DEVICE_ROLE_CHILD, OT_DEVICE_ROLE_ROUTER or OT_DEVICE_ROLE_LEADER
 * @retval FALSE  For all other device roles
 *
 */
bool thciIsConnected(void);

/**
 * Get the OpenThread version string.
 *
 * @param[in]  aString        A buffer to hold the string.
 * @param[in]  aLength        The length in bytes of the aString buffer.
 *
 * @retval OT_ERROR_NONE      Successfully acquired and returned the string.
 *
 */
otError thciGetVersionString(char *aString, size_t aLength);

/**
 * Initialize the LwIP network interface for thread.
 *
 * @param[in]  aNetif           Pointer to a LwIP network Interface to be associated with Thread.
 * @param[in]  aTag             Tag value to assign to aNetif.
 * @param[in]  aInterfaceName   The name to assign to aNetif.
 *
 * @retval  OT_ERROR_NONE       The Netif was successfully initialized.
 */
otError thciNetifInit(struct netif *aNetif, thci_netif_tags_t aTag, const char * aInterfaceName);

/**
 * Get the parent average RSSI
 *
 * @param[in]  aParentRssi  Pointer to location to store RSSI.
 *
 * @retval  OT_ERROR_NONE   Successfully acquired and returned the RSSI.
 */
otError thciGetParentAverageRssi(int8_t *aParentRssi);

/**
 * Get the last packet RSSI for the parent
 *
 * @param[in]  aLastRssi   Pointer to location to store RSSI.
 *
 * @retval  OT_ERROR_NONE  Successfully acquired and returned the RSSI.
 */
otError thciGetParentLastRssi(int8_t *aLastRssi);

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP
/**
 * Communicate to the NCP that the host is going to sleep. The NCP
 * should therefore employ rules for waking the host and should filter
 * communication to the host.
 *
 * @retval  OT_ERROR_NONE  Successfully communicated the sleep state to the NCP. Error code otherwise.
 */
otError thciHostSleep(void);

/**
 * Communicate to the NCP that the host is waking from sleep.
 *
 * @retval  OT_ERROR_NONE  Successfully communicated the wake state to the NCP. Error code otherwise.
 */
otError thciHostWake(void);

#endif

/**
 * Get the preferred router id
 *
 * @param[out]  aRouterId   Pointer to location to store router id.
 *
 * @retval  OT_ERROR_NONE  Successfully acquired and returned the router id.
 */
otError thciGetPreferredRouterId(uint8_t *aRouterId);

/**
 * Get the thread leader address
 *
 * @param[out]  aLeaderAddr  Pointer to location to store the IPv6 address.
 *
 * @retval  OT_ERROR_NONE  Sucessfully acquired and returned the address.
 */
otError thciGetLeaderAddress(otIp6Address *aLeaderAddr);

/**
 * Get the thread network data
 *
 * @param[out]  aNetworkData  Pointer to location to store the data.
 * @param[in]   aInSize       The size of aNetworkData.
 * @param[out]  aOutSize      The amount of data written to aNetworkData.
 *
 * @retval  OT_ERROR_NONE    Successfully acquired and returned the data.
 * @retval  OT_ERROR_FAILED  Failed to read the data.
 */
otError thciGetNetworkData(uint8_t *aNetworkData, uint16_t aInSize, uint16_t *aOutSize);

/**
 * Get the thread stable network data
 *
 * @param[out]  aNetworkData  Pointer to location to store the data.
 * @param[in]   aInSize       The size of aNetworkData.
 * @param[out]  aOutSize      The amount of data written to aNetworkData.
 *
 * @retval  OT_ERROR_NONE    Successfully acquired and returned the data.
 * @retval  OT_ERROR_FAILED  Failed to read the data.
 */
otError thciGetStableNetworkData(uint8_t *aNetworkData, uint16_t aInSize, uint16_t *aOutSize);

/**
 * Get the child and neighbour tables, condensed into one table
 *
 * @param[out]  aTableHead       Pointer to an array of info objects.
 * @param[in]   aInSize          The size (number of entries) of the input array.
 * @param[out]  aOutSize         The number of records put into the child table.
 *
 * @retval  OT_ERROR_NONE         Successfully got the table.
 * @retval  OT_ERROR_FAILED       Failed to read the table.
 */
otError thciGetCombinedNeighborTable(thci_neighbor_child_info_t *aTableHead, uint32_t aInSize, uint32_t *aOutSize);

/**
 * Get the thread child table
 *
 * @param[out]  aChildTableHead  Pointer to an array of child info objects.
 * @param[in]   aInSize          The size (number of entries) of the input array.
 * @param[out]  aOutSize         The number of records put into the child table.
 *
 * @retval  OT_ERROR_NONE         Successfully got the child table.
 * @retval  OT_ERROR_FAILED       Failed to read the table.
 */
otError thciGetChildTable(otChildInfo *aChildTableHead, uint32_t aInSize, uint32_t *aOutSize);

/**
 * Get the thread neighbour table
 *
 * @param[out]  aNeighborTableHead  Pointer to an array of neighbour info objects.
 * @param[in]   aInSize             The size (number of entries) of the input array.
 * @param[out]  aOutSize            The number of records put into the neighbour table.
 *
 * @retval  OT_ERROR_NONE            Successfully got the neighbour table.
 * @retval  OT_ERROR_FAILED          Failed to read the table.
 */
otError thciGetNeighborTable(otNeighborInfo *aNeighborTableHead, uint32_t aInSize, uint32_t *aOutSize);

/**
 * Get the instantaneous RSSI
 *
 * @param[out] aRssi       Pointer to a location to store the RSSI
 *
 * @retval  OT_ERROR_NONE  Successfully got the RSSI
 */
otError thciGetInstantRssi(int8_t *aRssi);

/**
 * Triggers NCP Crash recovery. When a communication failure or NCP crash is
 * detected this API should be called.
 */
void thciInitiateNCPRecovery(void);

/**
 * This function provides control over the outgoing IP packet flow. When
 * enabled, outgoing IP packets will be stalled and held in the packet queue
 * until the stall is disabled.
 *
 * @param[in]   aEnable  Set true to stall outgoing IP packets, false otherwise.
 */
void thciStallOutgoingDataPackets(bool aEnable);

/**
 * This function extracts the checksum from the IP packet
 *
 * @param[in]   pbuf  Buffer containing the IP packet
 */
uint16_t thciGetChecksum(const struct pbuf *q);

#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
/**
 * Call this function to start or stop an alarm wake procedure.
 *
 * @param[in]    aEnable    Set to true to start Network wake, false otherwise.
 * @param[in]    aReason    The reason code for the wake.
 */
otError thciSetLegacyNetworkWake(bool aEnable, uint8_t aReason);

/**
 * Call this function to set Lurker behavior on the NCP.
 *
 * @param[in]   aEnable  Set to true to enable lurker support, false otherwise.
 */
otError thciSetLegacyNetworkLurk(bool aEnable);

#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif /* __THCI_H_INCLUDED__ */
