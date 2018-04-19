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

#ifndef __THCI_MODULE_H_INCLUDED__
#define __THCI_MODULE_H_INCLUDED__

#include <nlertime.h>
#include <nlerevent.h>
#include <nlereventqueue.h>
#include <nlereventpooled.h>

#include <lwip/ip6.h>
#include <lwip/netif.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NL_THCI_PAYLOAD_MTU 1280

/**
 * enumertion of THCI states
 */
typedef enum
{
    THCI_UNINITIALIZED    = 0,
    THCI_INITIALIZED      = 1,
} thci_state_t;

/**
 * THCI OT message queue storage.
 */
typedef struct 
{
    uint16_t mHead;
    uint16_t mTail;
    otMessage *mQueue[THCI_CONFIG_MESSAGE_QUEUE_SIZE];
} thci_message_queue_t;

/**
 * thci_security_state_flags_t are used to track the state of OT encryption on outgoing packets.
 */
typedef enum
{
    THCI_SECURITY_FLAG_THREAD_STARTED                   = 0x01, // the thread protocol is started.
    THCI_SECURITY_FLAG_INSECURE_PORTS_ENABLED           = 0x02, // one or more insecure ports are enabled.
    THCI_SECURITY_FLAG_INSECURE_SOURCE_PORT             = 0x04, // A insecure source port opened by THCI.
    THCI_SECURITY_FLAG_SECURE_MSG_RXD_ON_INSECURE_PORT  = 0x08, // A secure message was received on the insecure port.
} thci_security_state_flags_t;

/**
 * THCI_ENABLE_MESSAGE_SECURITY - Macro to aid in determining whether or not to secure an outgoing packet.
 */
#define THCI_ENABLE_MESSAGE_SECURITY(_flags) ((_flags) & THCI_SECURITY_FLAG_THREAD_STARTED)

/**
 * THCI_TEST_INSECURE_PORTS - Macro to aid in determining whether or not to secure an outgoing packet.
 */
#define THCI_TEST_INSECURE_PORTS(_flags) (((_flags) & THCI_SECURITY_FLAG_INSECURE_PORTS_ENABLED) ? true : false)

/**
 * THCI_TEST_INSECURE_SOURCE_PORT - Macro to learn whether THCI has made a source port insecure.
 */
#define THCI_TEST_INSECURE_SOURCE_PORT(_flags) (((_flags) & THCI_SECURITY_FLAG_INSECURE_SOURCE_PORT) ? true : false)

/**
 * THCI_RECEIVED_SECURE_MESSAGE_ON_INSECURE_PORT - Macro to learn whether a secure message has been received on an insecure port.
 */
#define THCI_RECEIVED_SECURE_MESSAGE_ON_INSECURE_PORT(_flags) (((_flags) & THCI_SECURITY_FLAG_SECURE_MSG_RXD_ON_INSECURE_PORT) ? true : false)

/**
 * THCI context storage
 */
typedef struct
{
    thci_init_params_t      mInitParams;                    // send and receive event queues.
    struct netif            *mNetif[THCI_NETIF_TAG_COUNT];  // The associated LwIP network Interface.
    thci_message_queue_t    mMessageQueue;                  // The outgoing message queue.
    thci_state_t            mState;                         // THCI state.
    uint16_t                mInsecureSourcePort;            // A second insecure port opened by THCI or the port opened by the client.
    uint8_t                 mSecurityFlags;                 // OpenThread Security State flags.
    otDeviceRole            mDeviceRole;                    // OpenThread device role
    bool                    mStallOutgoingDataPackets;      // Allows the flow of outgoing data packets to be stalled.
} thci_sdk_context_t;

otMessage* DequeueMessage(void);
int EnqueueMessage(otMessage *aMessage);
bool IsMessageQueueEmpty(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif /* __THCI_MODULE_H_INCLUDED__ */
