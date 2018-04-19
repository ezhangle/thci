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
 *      Implements Thread Host Control Interface NCP.
 *
 */

#ifndef __THCI_MODULE_NCP_H_INCLUDED__
#define __THCI_MODULE_NCP_H_INCLUDED__

#include <openthread/types.h>
#include <nlerlock.h>

#ifdef __cplusplus
extern "C" {
#endif


#define THCI_MESSAGE_FLAG_FREE      0x01
#define THCI_MESSAGE_FLAG_SECURE    0x02
#define THCI_MESSAGE_FLAG_LEGACY    0x04

// OpenThread maintains a table of unicast addresses that is build-time configurable
// As there is currently no way to learn that value, THCI_CACHED_UNICAST_ADDRESS_SIZE definition
// must match the value set on the NCP.  4 is the current default value.
#define THCI_CACHED_UNICAST_ADDRESS_SIZE 4

// This define must match value set on NCP corresponding to
// OPENTHREAD_CONFIG_MAX_EXT_MULTICAST_IP_ADDRS
#define THCI_CACHED_MULTICAST_ADDRESS_SIZE 2

// The number of memory buffers used to store content until an event is handled
// to deliver that content to the client via the client callbacks.
#define THCI_NUM_CALLBACK_BUFFERS   (4)

typedef struct
{
    uint8_t         *mBuffer;
    uint16_t        mOffset;
    uint16_t        mLength;
    uint16_t        mTotalLength;
    uint8_t         mFlags;
    uint8_t         mReserved; // ensure that the structure is 4 byte aligned.
} thci_message_t;

typedef enum {
    kCallbackBufferStateFree = 0,
    kCallbackBufferStateScanResult,
    kCallbackBufferStateLegacyUla
} callback_buffer_state_t;

typedef struct callback_buffer_s {
    callback_buffer_state_t mState;

    union {
        uint8_t            mLegacyUla[THCI_LEGACY_ULA_SIZE_BYTES];
        otActiveScanResult mScanResult;
    } mContent;
} callback_buffer_t;

typedef enum {
    kModuleStateUninitialized = 0,
    kModuleStateInitialized,
    kModuleStateResetRecovery,
    kModuleStateHostSleep
} module_state_t;

// THCI context that is unique to the NCP solution.
typedef struct
{
    uint8_t                     mMessageRingBuffer[THCI_CONFIG_NCP_TX_MESSAGE_RING_BUFFER_SIZE];
    uint8_t                     *mMessageRingHead;
    uint8_t                     *mMessageRingTail;
    uint16_t                    mMessageRingEndGap;
    nl_lock_t                   mMessageLock;
    nl_eventqueue_t             mWaitFreeQueue;
    nl_event_t                  *mWaitFreeQueueMem[1];
    bool                        mWaitFreeQueueEmpty;

    otNetifAddress              mCachedUnicastAddresses[THCI_CACHED_UNICAST_ADDRESS_SIZE];
    otNetifMulticastAddress     mCachedMulticastAddresses[THCI_CACHED_MULTICAST_ADDRESS_SIZE];
    callback_buffer_t           mCallbackBuffers[THCI_NUM_CALLBACK_BUFFERS];

    thciHandleActiveScanResult  mScanResultCallback;
    void                        *mScanResultCallbackContext;
    thciStateChangedCallback    mStateChangeCallback;
    thcilegacyUlaCallback       mLegacyUlaCallback;
    thciResetRecoveryCallback   mResetRecoveryCallback;
#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
    thciLurkerWakeCallback      mLurkerWakeCallback;
#endif

    uint8_t                     mTransactionId;
    module_state_t              mModuleState;
    spinel_status_t             mLastStatus;
    uint32_t                    mStateChangeFlags;
} thci_ncp_context_t;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif /* __THCI_MODULE_NCP_H_INCLUDED__ */
