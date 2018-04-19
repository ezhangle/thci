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
 *      This file implements Thread Host Control Interface API.
 *
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <nlassert.h>
#include <nlerlog.h>
#include <nlalignment.h>

#include <thci.h>
#include <thci_config.h>
#include <thci_module.h>

#ifdef __cplusplus
extern "C" {
#endif

thci_sdk_context_t gTHCISDKContext;

/**
* Function to initialize Thci context.
*
* @param[in]  aInitParams    thci_init_params_t structure that provides pointers to
*                            queues that Thci can use to send and receive events.
*
* @return 0 when success.
*/
int thciSDKInit(thci_init_params_t *aInitParams)
{
    int retval = 0;

    nlEXPECT_ACTION(gTHCISDKContext.mState == THCI_UNINITIALIZED, done, retval = -EALREADY);

    /* Initialize the sdk context with the passed parameters */
    memset(&gTHCISDKContext, 0, sizeof(thci_sdk_context_t));

    memcpy(&gTHCISDKContext.mInitParams, aInitParams, sizeof(thci_init_params_t));

    gTHCISDKContext.mState = THCI_INITIALIZED;

 done:
    if (retval)
    {
        NL_LOG_CRIT(lrTHCI, "thciSDKInit failed with error = %d\n", retval);
    }

    return retval;
}

/**
 * Check if Thci was initialized.
 *
 * @return true if Thci was initialized.
 */
int thciInitialized(void)
{
    return (gTHCISDKContext.mState == THCI_INITIALIZED);
}

otMessage* DequeueMessage(void)
{
    otMessage* retval = NULL;
    thci_message_queue_t *queue = &gTHCISDKContext.mMessageQueue;

    nlREQUIRE(queue->mQueue[queue->mTail] != NULL, done);

    retval = queue->mQueue[queue->mTail];
    queue->mQueue[queue->mTail] = NULL;
    queue->mTail = (queue->mTail < THCI_CONFIG_MESSAGE_QUEUE_SIZE - 1) ? queue->mTail + 1 : 0;

 done:
    return retval;
}

int EnqueueMessage(otMessage *aMessage)
{
    int retval = -ENOSPC;
    thci_message_queue_t *queue = &gTHCISDKContext.mMessageQueue;

    nlREQUIRE(queue->mQueue[queue->mHead] == NULL, done);

    queue->mQueue[queue->mHead] = aMessage;
    queue->mHead = (queue->mHead < THCI_CONFIG_MESSAGE_QUEUE_SIZE - 1) ? queue->mHead + 1 : 0;

    retval = 0;

 done:
    return retval;
}

bool IsMessageQueueEmpty(void)
{
    return (gTHCISDKContext.mMessageQueue.mQueue[gTHCISDKContext.mMessageQueue.mTail] == NULL);
}

uint16_t thciGetChecksum(const struct pbuf *q)
{
    size_t offset;
    uint16_t checksum;

    nlREQUIRE_ACTION(q != NULL, done, checksum = -1);

    uint8_t next_header = *IP6H_NEXTH_P(q->payload);

    switch (next_header)
    {
    case IP6_NEXTH_TCP:
        {
            const size_t tcp_checksum_offset = 16;
            offset = IP6_HLEN + tcp_checksum_offset;
        }
        break;

    case IP6_NEXTH_UDP:
        {
            const size_t udp_checksum_offset = 6;
            offset = IP6_HLEN + udp_checksum_offset;
        }
        break;

    default:
        offset = 0;
        break;
    }

    if (offset && (q->tot_len >= offset + sizeof(uint16_t)))
    {
        const uint8_t *p = (const uint8_t *)q->payload;
        checksum = p[offset] << 8 | p[offset + 1];
    }
    else
    {
        checksum = 0;
    }

done:
    return checksum;
}

#ifdef __cplusplus
}  // extern "C"
#endif
