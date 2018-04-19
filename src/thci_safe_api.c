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
 *      This file implements the THCI Safe API for calls to THCI from other tasks.
 *
 */

#include <thci_config.h>

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
#include <nlerlock.h>
#include <nlereventqueue.h>

#include <thci.h>
#include <thci_module.h>
#include <thci_safe_api.h>
#include <thci_update.h>


/**
 * DEFINES
 */

enum {
    kSafeCmdGetMacCounters = 1,
    kSafeCmdGetIpCounters,
    kSafeCmdAddExternalRoute,
    kSafeCmdRemoveExternalRoute,
    kSafeCmdMfgDiags,
    kSafeCmdVersionString,
    kSafeCmdGetRloc16,
    kSafeCmdGetLeaderRouterId,
    kSafeCmdGetParentAverageRssi,
    kSafeCmdGetParentLastRssi,
    kSafeCmdGetPartitionId,
    kSafeCmdHardReset,
    kSafeCmdGetLeaderWeight,
    kSafeCmdGetLocalLeaderWeight,
    kSafeCmdGetNetworkDataVersion,
    kSafeCmdGetStableNetworkDataVersion,
    kSafeCmdGetPreferredRouterId,
    kSafeCmdGetLeaderAddress,
    kSafeCmdGetNetworkData,
    kSafeCmdGetStableNetworkData,
    kSafeCmdGetCombinedNeighborTable,
    kSafeCmdGetChildTable,
    kSafeCmdGetNeighborTable,
    kSafeCmdGetExtendedAddress,
    kSafeCmdGetInstantRssi
};

struct versionStringContext
{
    char * mString;
    size_t mLength;
};

struct networkDataContext
{
    uint8_t  *mData;
    uint16_t  mInSize;
    uint16_t *mOutSize;
};

struct combinedTableContext
{
    thci_neighbor_child_info_t *mTableHead;
    uint32_t                    mInSize;
    uint32_t                   *mOutSize;
};

struct childTableContext
{
    otChildInfo *mChildTableHead;
    uint32_t     mInSize;
    uint32_t    *mOutSize;
};

struct neighborTableContext
{
    otNeighborInfo *mNeighborTableHead;
    uint32_t     mInSize;
    uint32_t    *mOutSize;
};

/**
 * PROTOTYPES
 */

static int SafeAPIEventHandler(nl_event_t *aEvent, void *aClosure);
int thciSafeInitialize(void);
int thciSafeFinalize(void);

typedef struct
{
    nl_lock_t                   mSafeLock;
    nl_eventqueue_t             mSafeQueue;
    nl_event_t                  *mSafeQueueMem[1];
    void                        *mSafeContent;
    otError                     mSafeResult;
    uint16_t                    mSafeCommand;
    bool                        mInitialized;
}thci_safe_context_t;

/**
 * GLOBALS
 */

// THCI context that is common between the NCP and SOC solutions.
extern thci_sdk_context_t gTHCISDKContext;

static const nl_event_t sSafeAPIEvent =
{
    NL_INIT_EVENT_STATIC(((nl_event_type_t)NL_EVENT_T_RUNTIME), SafeAPIEventHandler, NULL)
};

static thci_safe_context_t sThciSafeContext;

/**
 * MULTI-TASK SAFE API IMPLEMENTATION
 */

static int SafeAPIEventHandler(nl_event_t *aEvent, void *aClosure)
{
    otError result = OT_ERROR_INVALID_STATE;

    nlREQUIRE(sThciSafeContext.mInitialized, done);

    switch (sThciSafeContext.mSafeCommand)
    {
    case kSafeCmdGetMacCounters:
        result = thciGetMacCounters((otMacCounters*)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdGetIpCounters:
        result = thciGetIpCounters((otIpCounters*)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdAddExternalRoute:
        result = thciAddExternalRoute((otExternalRouteConfig*)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdRemoveExternalRoute:
        result = thciRemoveExternalRoute((const otIp6Prefix *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdMfgDiags:
        result = thciDiagnosticsCommand((const char *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdVersionString:
        result = thciGetVersionString(((struct versionStringContext *)sThciSafeContext.mSafeContent)->mString,
                                      ((struct versionStringContext *)sThciSafeContext.mSafeContent)->mLength);
        break;

    case kSafeCmdGetRloc16:
        result = thciGetRloc16((uint16_t *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdGetLeaderRouterId:
        result = thciGetLeaderRouterId((uint8_t *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdGetParentAverageRssi:
        result = thciGetParentAverageRssi((int8_t *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdGetParentLastRssi:
        result = thciGetParentLastRssi((int8_t *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdGetPartitionId:
        result = thciGetPartitionId((uint32_t *)sThciSafeContext.mSafeContent);
        break;

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP
    case kSafeCmdHardReset:
        {
            const bool aStartBootloader = true;

            thciHardResetNcp(!aStartBootloader);
            result = OT_ERROR_NONE;
        }
        break;
#endif

    case kSafeCmdGetLeaderWeight:
        result = thciGetLeaderWeight((uint8_t *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdGetLocalLeaderWeight:
        result = thciGetLocalLeaderWeight((uint8_t *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdGetNetworkDataVersion:
        result = thciGetNetworkDataVersion((uint8_t *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdGetStableNetworkDataVersion:
        result = thciGetStableNetworkDataVersion((uint8_t *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdGetPreferredRouterId:
        result = thciGetPreferredRouterId((uint8_t *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdGetLeaderAddress:
        result = thciGetLeaderAddress((otIp6Address *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdGetNetworkData:
        result = thciGetNetworkData(
            ((struct networkDataContext *)sThciSafeContext.mSafeContent)->mData,
            ((struct networkDataContext *)sThciSafeContext.mSafeContent)->mInSize,
            ((struct networkDataContext *)sThciSafeContext.mSafeContent)->mOutSize);
        break;

    case kSafeCmdGetStableNetworkData:
        result = thciGetStableNetworkData(
            ((struct networkDataContext *)sThciSafeContext.mSafeContent)->mData,
            ((struct networkDataContext *)sThciSafeContext.mSafeContent)->mInSize,
            ((struct networkDataContext *)sThciSafeContext.mSafeContent)->mOutSize);
        break;

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP

    case kSafeCmdGetCombinedNeighborTable:
        result = thciGetCombinedNeighborTable(
            ((struct combinedTableContext *)sThciSafeContext.mSafeContent)->mTableHead,
            ((struct combinedTableContext *)sThciSafeContext.mSafeContent)->mInSize,
            ((struct combinedTableContext *)sThciSafeContext.mSafeContent)->mOutSize);
        break;

    case kSafeCmdGetChildTable:
        result = thciGetChildTable(
            ((struct childTableContext *)sThciSafeContext.mSafeContent)->mChildTableHead,
            ((struct childTableContext *)sThciSafeContext.mSafeContent)->mInSize,
            ((struct childTableContext *)sThciSafeContext.mSafeContent)->mOutSize);
        break;

    case kSafeCmdGetNeighborTable:
        result = thciGetNeighborTable(
            ((struct neighborTableContext *)sThciSafeContext.mSafeContent)->mNeighborTableHead,
            ((struct neighborTableContext *)sThciSafeContext.mSafeContent)->mInSize,
            ((struct neighborTableContext *)sThciSafeContext.mSafeContent)->mOutSize);
        break;

#endif // THCI_CONFIG_USE_OPENTHREAD_ON_NCP

    case kSafeCmdGetExtendedAddress:
        result = thciGetExtendedAddress((uint8_t *)sThciSafeContext.mSafeContent);
        break;

    case kSafeCmdGetInstantRssi:
        result = thciGetInstantRssi((int8_t *)sThciSafeContext.mSafeContent);
        break;

    default:
        result = OT_ERROR_INVALID_ARGS;
        break;
    }

 done:

    sThciSafeContext.mSafeResult = result;

    // It doesn't matter what gets posted to this queue as it behaves like a semaphore.
    nl_eventqueue_post_event(sThciSafeContext.mSafeQueue, &sSafeAPIEvent);

    return NLER_SUCCESS;
}

static otError IssueSafeCommand(uint16_t aCmd, void *aContext)
{
    otError retval = OT_ERROR_INVALID_STATE;
    int status;
    nl_event_t *ev;

    nlREQUIRE(sThciSafeContext.mInitialized, done);

    status = nl_er_lock_enter(sThciSafeContext.mSafeLock);
    nlREQUIRE(!status, done);

    sThciSafeContext.mSafeCommand = aCmd;
    sThciSafeContext.mSafeContent = aContext;

    status = nl_eventqueue_post_event(gTHCISDKContext.mInitParams.mSdkQueue, &sSafeAPIEvent);
    nlREQUIRE(!status, unlock);

    ev = nl_eventqueue_get_event(sThciSafeContext.mSafeQueue);
    nlREQUIRE(ev != NULL, unlock);

    retval = sThciSafeContext.mSafeResult;

 unlock:
    nl_er_lock_exit(sThciSafeContext.mSafeLock);

 done:
    return retval;
}

int thciSafeInitialize(void)
{
    int retval = 0;

    if (sThciSafeContext.mSafeLock == NULL)
    {
        sThciSafeContext.mSafeLock = nl_er_lock_create();
        nlREQUIRE_ACTION(sThciSafeContext.mSafeLock != NULL, done, retval = -EFAULT);
    }

    if (sThciSafeContext.mSafeQueue == NULL)
    {
        sThciSafeContext.mSafeQueue = nl_eventqueue_create(sThciSafeContext.mSafeQueueMem, sizeof(sThciSafeContext.mSafeQueueMem));
        nlREQUIRE_ACTION(sThciSafeContext.mSafeQueue != NULL, done, retval = -EFAULT);
    }

    sThciSafeContext.mInitialized = true;

 done:
    return retval;
}

int thciSafeFinalize(void)
{
    int retval = 0;

    sThciSafeContext.mInitialized = false;

    return retval;
}

otError thciSafeGetMacCounters(otMacCounters *aMacCounters)
{
    return IssueSafeCommand(kSafeCmdGetMacCounters, (void*)aMacCounters);
}

otError thciSafeGetIpCounters(otIpCounters *aIpCounters)
{
    return IssueSafeCommand(kSafeCmdGetIpCounters, (void*)aIpCounters);
}

otDeviceRole thciSafeGetDeviceRole(void)
{
    return gTHCISDKContext.mDeviceRole;
}

otError thciSafeAddExternalRoute(otExternalRouteConfig *aConfig)
{
    return IssueSafeCommand(kSafeCmdAddExternalRoute, (void*)aConfig);
}

otError thciSafeRemoveExternalRoute(otIp6Prefix *aPrefix)
{
    return IssueSafeCommand(kSafeCmdRemoveExternalRoute, (void*)aPrefix);
}

otError thciSafeMfgDiagsCmd(const char *aString)
{
    return IssueSafeCommand(kSafeCmdMfgDiags, (void*)aString);
}

otError thciSafeGetVersionString(char *aString, size_t aLength)
{
    struct versionStringContext context;

    context.mString = aString;
    context.mLength = aLength;

    return IssueSafeCommand(kSafeCmdVersionString, (void*)&context);
}

otError thciSafeGetRloc16(uint16_t *aRloc)
{
    return IssueSafeCommand(kSafeCmdGetRloc16, (void*)aRloc);
}

otError thciSafeGetLeaderRouterId(uint8_t *aLeaderId)
{
    return IssueSafeCommand(kSafeCmdGetLeaderRouterId, (void*)aLeaderId);
}

otError thciSafeGetParentAverageRssi(int8_t *aParentRssi)
{
    return IssueSafeCommand(kSafeCmdGetParentAverageRssi, (void*)aParentRssi);
}

otError thciSafeGetParentLastRssi(int8_t *aLastRssi)
{
    return IssueSafeCommand(kSafeCmdGetParentLastRssi, (void*)aLastRssi);
}

otError thciSafeGetPartitionId(uint32_t *aPartitionId)
{
    return IssueSafeCommand(kSafeCmdGetPartitionId, (void*)aPartitionId);
}

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP
otError thciSafeHardResetNCP(void)
{
    return IssueSafeCommand(kSafeCmdHardReset, NULL);
}
#endif

otError thciSafeGetLeaderWeight(uint8_t *aWeight)
{
    return IssueSafeCommand(kSafeCmdGetLeaderWeight, (void*)aWeight);
}

otError thciSafeGetLocalLeaderWeight(uint8_t *aWeight)
{
    return IssueSafeCommand(kSafeCmdGetLocalLeaderWeight, (void*)aWeight);
}

otError thciSafeGetNetworkDataVersion(uint8_t *aVersion)
{
    return IssueSafeCommand(kSafeCmdGetNetworkDataVersion, (void*)aVersion);
}

otError thciSafeGetStableNetworkDataVersion(uint8_t *aVersion)
{
    return IssueSafeCommand(kSafeCmdGetStableNetworkDataVersion, (void*)aVersion);
}

otError thciSafeGetPreferredRouterId(uint8_t *aRouterId)
{
    return IssueSafeCommand(kSafeCmdGetPreferredRouterId, (void*)aRouterId);
}

otError thciSafeGetLeaderAddress(otIp6Address *aLeaderAddr)
{
    return IssueSafeCommand(kSafeCmdGetLeaderAddress, (void*)aLeaderAddr);
}

otError thciSafeGetNetworkData(uint8_t *aNetworkData, uint16_t aInSize, uint16_t *aOutSize)
{
    struct networkDataContext context;
    context.mData        = aNetworkData;
    context.mInSize      = aInSize;
    context.mOutSize     = aOutSize;

    return IssueSafeCommand(kSafeCmdGetNetworkData, (void*)&context);
}

otError thciSafeGetStableNetworkData(uint8_t *aNetworkData, uint16_t aInSize, uint16_t *aOutSize)
{
    struct networkDataContext context;
    context.mData        = aNetworkData;
    context.mInSize      = aInSize;
    context.mOutSize     = aOutSize;

    return IssueSafeCommand(kSafeCmdGetStableNetworkData, (void*)&context);
}

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP

otError thciSafeGetCombinedNeighborTable(thci_neighbor_child_info_t *aTableHead, uint32_t aInSize, uint32_t *aOutSize)
{
    struct combinedTableContext context;
    context.mTableHead = aTableHead;
    context.mInSize    = aInSize;
    context.mOutSize   = aOutSize;

    return IssueSafeCommand(kSafeCmdGetCombinedNeighborTable, (void*)&context);
}

otError thciSafeGetChildTable(otChildInfo *aChildTableHead, uint32_t aInSize, uint32_t *aOutSize)
{
    struct childTableContext context;
    context.mChildTableHead = aChildTableHead;
    context.mInSize         = aInSize;
    context.mOutSize        = aOutSize;

    return IssueSafeCommand(kSafeCmdGetChildTable, (void*)&context);
}

otError thciSafeGetNeighborTable(otNeighborInfo *aNeighborTableHead, uint32_t aInSize, uint32_t *aOutSize)
{
    struct neighborTableContext context;
    context.mNeighborTableHead = aNeighborTableHead;
    context.mInSize            = aInSize;
    context.mOutSize           = aOutSize;

    return IssueSafeCommand(kSafeCmdGetNeighborTable, (void*)&context);
}

bool thciSafeIsNcpPosting(void)
{
    //TODO: Implement this function to test the Interrupt line from the NCP.
    return false;
}

#endif // THCI_CONFIG_USE_OPENTHREAD_ON_NCP

otError thciSafeGetExtendedAddress(uint8_t *aAddress)
{
    return IssueSafeCommand(kSafeCmdGetExtendedAddress, (void*)aAddress);
}

otError thciSafeGetInstantRssi(int8_t *aRssi)
{
    return IssueSafeCommand(kSafeCmdGetInstantRssi, (void*)aRssi);
}
