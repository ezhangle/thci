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
 *      Implements multi-task safe Thread Host Control Interface API.
 *
 */

#ifndef __THCI_SAFE_API_H_INCLUDED__
#define __THCI_SAFE_API_H_INCLUDED__

#include <openthread/types.h>
#include <thci.h>

#ifdef __cplusplus
extern "C" {
#endif

otError thciSafeGetMacCounters(otMacCounters *aMacCounters);

otError thciSafeGetIpCounters(otIpCounters *aIpCounters);

otDeviceRole thciSafeGetDeviceRole(void);

otError thciSafeAddExternalRoute(otExternalRouteConfig *aConfig);

otError thciSafeRemoveExternalRoute(otIp6Prefix *aPrefix);

otError thciSafeMfgDiagsCmd(const char *aString);

otError thciSafeGetVersionString(char *aString, size_t aLength);

otError thciSafeGetRloc16(uint16_t *aRloc);

otError thciSafeGetLeaderRouterId(uint8_t *aLeaderId);

otError thciSafeGetParentAverageRssi(int8_t *aParentRssi);

otError thciSafeGetParentLastRssi(int8_t *aLastRssi);

otError thciSafeGetPartitionId(uint32_t *aPartitionId);

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP
otError thciSafeHardResetNCP(void);
#endif

otError thciSafeGetLeaderWeight(uint8_t *aWeight);

otError thciSafeGetLocalLeaderWeight(uint8_t *aWeight);

otError thciSafeGetNetworkDataVersion(uint8_t *aVersion);

otError thciSafeGetStableNetworkDataVersion(uint8_t *aVersion);

otError thciSafeGetPreferredRouterId(uint8_t *aRouterId);

otError thciSafeGetLeaderAddress(otIp6Address *aLeaderAddr);

otError thciSafeGetNetworkData(uint8_t *aNetworkData, uint16_t aInSize, uint16_t *aOutSize);

otError thciSafeGetStableNetworkData(uint8_t *aNetworkData, uint16_t aInSize, uint16_t *aOutSize);

otError thciSafeGetCombinedNeighborTable(thci_neighbor_child_info_t *aTableHead, uint32_t aInSize, uint32_t *aOutSize);

otError thciSafeGetChildTable(otChildInfo *aChildTableHead, uint32_t aInSize, uint32_t *aOutSize);

otError thciSafeGetNeighborTable(otNeighborInfo *aNeighborTableHead, uint32_t aInSize, uint32_t *aOutSize);

otError thciSafeGetExtendedAddress(uint8_t *aAddress);

otError thciSafeGetInstantRssi(int8_t *aRssi);

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP
bool thciSafeIsNcpPosting(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // __THCI_SAFE_API_H_INCLUDED__
