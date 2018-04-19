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
 *      This file implements NCP version of Thread Host Control Interface.
 */

#include <thci_config.h>

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP

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
#if THCI_CONFIG_INITIALIZE_WITHOUT_NCP_RESET
#include <nlboard.h>
#endif

/* OpenThread library includes */
#include <openthread/types.h>
#include <openthread/spinel.h>
#include <openthread/instance.h>

#if THCI_CONFIG_SPINEL_VENDOR_SUPPORT
#include <openthread/spinel-vendor.h>
#endif

/* thci includes */
#include <thci.h>
#include <thci_module.h>
#include <thci_module_ncp.h>
#include <thci_module_ncp_uart.h>
#include <thci_update.h>
#include <thci_cert.h>

/* LWIP Includes */
#include <lwip/ip6.h>
#include <lwip/ip6_addr.h>
#include <lwip/ip6_frag.h>
#include <lwip/udp.h>
#include <lwip/err.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/tcpip.h>

/**
 * DEFINES
 */

const uint8_t kMaxTransactionId         = 0x0f;
const uint8_t kDontCareTransactionId    = 0x01;
const uint8_t kEmptyTransactionId       = 0x00;

#define kDefaultSpinelPropertyKey   SPINEL_PROP_LAST_STATUS

/**
 * PROTOTYPES
 */

static int OutgoingIPPacketEventHandler(nl_event_t *aEvent, void *aClosure);
static int StateChangeEventHandler(nl_event_t *aEvent, void *aClosure);
static int LegacyUlaChangeEventHandler(nl_event_t *aEvent, void *aClosure);
static int ScanCompleteEventHandler(nl_event_t *aEvent, void *aClosure);
static int ScanResultEventHandler(nl_event_t *aEvent, void *aClosure);
static int NCPRecoveryEventHandler(nl_event_t *aEvent, void *aClosure);

extern int thciSafeInitialize(void);
extern int thciSafeFinalize(void);
otError InitializeInternal(bool aMandatoryNcpReset, bool aAPIInitialize, thci_callbacks_t *aCallbacks,
                           thciUartDataFrameCallback_t aDataCB, thciUartControlFrameCallback_t aControlCB);
otError FinalizeInternal(bool inAPIFinalize);
void HandleLastStatusUpdate(const uint8_t *aArgPtr, unsigned int aArgLen);

/**
 * GLOBALS
 */

// THCI context that is common between the NCP and SOC solutions.
extern thci_sdk_context_t gTHCISDKContext;

thci_ncp_context_t gTHCINCPContext;

static uint8_t sOutgoingIPPacketEventPosted;

static const nl_event_t sOutgoingIPPacketEvent =
{
    NL_INIT_EVENT_STATIC(((nl_event_type_t)NL_EVENT_T_RUNTIME), OutgoingIPPacketEventHandler, NULL)
};

static const nl_event_t sStateChangeEvent =
{
    NL_INIT_EVENT_STATIC(((nl_event_type_t)NL_EVENT_T_RUNTIME), StateChangeEventHandler, NULL)
};

static const nl_event_t sLegacyUlaChangeEvent =
{
    NL_INIT_EVENT_STATIC(((nl_event_type_t)NL_EVENT_T_RUNTIME), LegacyUlaChangeEventHandler, NULL)
};

static const nl_event_t sScanResultEvent =
{
    NL_INIT_EVENT_STATIC(((nl_event_type_t)NL_EVENT_T_RUNTIME), ScanResultEventHandler, NULL)
};

static const nl_event_t sScanCompleteEvent =
{
    NL_INIT_EVENT_STATIC(((nl_event_type_t)NL_EVENT_T_RUNTIME), ScanCompleteEventHandler, NULL)
};

static const nl_event_t sNCPRecoveryEvent =
{
    NL_INIT_EVENT_STATIC(((nl_event_type_t)NL_EVENT_T_RUNTIME), NCPRecoveryEventHandler, NULL)
};

static const nl_event_t sFreeMessageEvent =
{
    NL_INIT_EVENT_STATIC(((nl_event_type_t)NL_EVENT_T_RUNTIME), NULL, NULL)
};

static const spinel_prop_key_t kMacCounterPropertyList[] =
{
    SPINEL_PROP_CNTR_TX_PKT_TOTAL,
    SPINEL_PROP_CNTR_TX_PKT_UNICAST,
    SPINEL_PROP_CNTR_TX_PKT_BROADCAST,
    SPINEL_PROP_CNTR_TX_PKT_ACK_REQ,
    SPINEL_PROP_CNTR_TX_PKT_ACKED,
    SPINEL_PROP_CNTR_TX_PKT_NO_ACK_REQ,
    SPINEL_PROP_CNTR_TX_PKT_DATA,
    SPINEL_PROP_CNTR_TX_PKT_DATA_POLL,
    SPINEL_PROP_CNTR_TX_PKT_BEACON,
    SPINEL_PROP_CNTR_TX_PKT_BEACON_REQ,
    SPINEL_PROP_CNTR_TX_PKT_OTHER,
    SPINEL_PROP_CNTR_TX_PKT_RETRY,
    SPINEL_PROP_CNTR_TX_ERR_CCA,
    SPINEL_PROP_CNTR_TX_ERR_ABORT,
    SPINEL_PROP_CNTR_RX_PKT_TOTAL,
    SPINEL_PROP_CNTR_RX_PKT_UNICAST,
    SPINEL_PROP_CNTR_RX_PKT_BROADCAST,
    SPINEL_PROP_CNTR_RX_PKT_DATA,
    SPINEL_PROP_CNTR_RX_PKT_DATA_POLL,
    SPINEL_PROP_CNTR_RX_PKT_BEACON,
    SPINEL_PROP_CNTR_RX_PKT_BEACON_REQ,
    SPINEL_PROP_CNTR_RX_PKT_OTHER,
    SPINEL_PROP_CNTR_RX_PKT_FILT_WL,
    SPINEL_PROP_CNTR_RX_PKT_FILT_DA,
    SPINEL_PROP_CNTR_RX_PKT_DUP,
    SPINEL_PROP_CNTR_RX_ERR_EMPTY,
    SPINEL_PROP_CNTR_RX_ERR_UKWN_NBR,
    SPINEL_PROP_CNTR_RX_ERR_NVLD_SADDR,
    SPINEL_PROP_CNTR_RX_ERR_SECURITY,
    SPINEL_PROP_CNTR_RX_ERR_BAD_FCS,
    SPINEL_PROP_CNTR_RX_ERR_OTHER
};

static const spinel_prop_key_t kIpCounterPropertyList[] =
{
    SPINEL_PROP_CNTR_IP_TX_SUCCESS,
    SPINEL_PROP_CNTR_IP_RX_SUCCESS,
    SPINEL_PROP_CNTR_IP_TX_FAILURE,
    SPINEL_PROP_CNTR_IP_RX_FAILURE
};

/**
 * MODULE IMPLEMENTATION
 */

/**
 * Returns true if the device is trying to provisionally join a network
 * and an insecure source port has not yet been opened.  Under this condition
 * THCI must open the source port selected by the TCP/IP stack by calling
 * OpenSourcePort().
 */
static bool NeedToOpenInsecureSourcePort(void)
{
    return (!THCI_ENABLE_MESSAGE_SECURITY(gTHCISDKContext.mSecurityFlags) &&
             THCI_TEST_INSECURE_PORTS(gTHCISDKContext.mSecurityFlags) &&
            !THCI_TEST_INSECURE_SOURCE_PORT(gTHCISDKContext.mSecurityFlags));
}

/**
 * Returns true if the device is assisting another device that is
 * trying to provisionally join.  Under this condition outgoing frames
 * with the assigned insecure source port will be sent insecurely.
 * As soon as a secure frame is received on the insecure port this
 * function will return false to indicate that outgoing frames
 * on the insecure port should be sent securely.
 */
static bool SendProvisionalJoinResponseInsecurely(void)
{
    return (THCI_ENABLE_MESSAGE_SECURITY(gTHCISDKContext.mSecurityFlags) &&
            THCI_TEST_INSECURE_PORTS(gTHCISDKContext.mSecurityFlags) &&
            !THCI_RECEIVED_SECURE_MESSAGE_ON_INSECURE_PORT(gTHCISDKContext.mSecurityFlags));
}

static otDeviceRole TranslateSpinelRole(spinel_net_role_t aRole)
{
    otDeviceRole retval = OT_DEVICE_ROLE_DETACHED;

    switch(aRole)
    {
    case SPINEL_NET_ROLE_CHILD:
        retval = OT_DEVICE_ROLE_CHILD;
        break;

    case SPINEL_NET_ROLE_ROUTER:
        retval = OT_DEVICE_ROLE_ROUTER;
        break;

    case SPINEL_NET_ROLE_LEADER:
        retval = OT_DEVICE_ROLE_LEADER;
        break;

    default:
        break;
    }

    return retval;
}

static void HandleChildTableUpdate(const uint8_t *aArgPtr, unsigned int aArgLen)
{
    spinel_ssize_t parsedLength;
    otChildInfo childInfo;
    uint8_t modeFlags;
    uint16_t index = 0;
    // Log the contents of the child table update from the NCP.
    // Currently, the entire Child table is sent as a single frame.
    NL_LOG_CRIT(lrTHCI, "OT Child Table Contents:\n");

    while (aArgLen > 0)
    {
        // The EUI-64 is unpacked as a pointer instead of a value.
        uint64_t *eui64;

        parsedLength = spinel_datatype_unpack(aArgPtr, aArgLen,
                        SPINEL_DATATYPE_STRUCT_S(
                            SPINEL_DATATYPE_EUI64_S         // EUI64 Address
                            SPINEL_DATATYPE_UINT16_S        // Rloc16
                            SPINEL_DATATYPE_UINT32_S        // Timeout
                            SPINEL_DATATYPE_UINT32_S        // Age
                            SPINEL_DATATYPE_UINT8_S         // Network Data Version
                            SPINEL_DATATYPE_UINT8_S         // Link Quality In
                            SPINEL_DATATYPE_INT8_S          // Average RSS
                            SPINEL_DATATYPE_UINT8_S         // Mode (flags)
                            SPINEL_DATATYPE_INT8_S          // Most recent RSS
                        ),
                        &eui64,
                        &childInfo.mRloc16,
                        &childInfo.mTimeout,
                        &childInfo.mAge,
                        &childInfo.mNetworkDataVersion,
                        &childInfo.mLinkQualityIn,
                        &childInfo.mAverageRssi,
                        &modeFlags,
                        &childInfo.mLastRssi);

        if (parsedLength <= 0)
        {
            break;
        }

        index++;
        childInfo.mRxOnWhenIdle      = (modeFlags & SPINEL_THREAD_MODE_RX_ON_WHEN_IDLE    ) ?    true : false;
        childInfo.mSecureDataRequest = (modeFlags & SPINEL_THREAD_MODE_SECURE_DATA_REQUEST) ?    true : false;
        childInfo.mFullFunction      = (modeFlags & SPINEL_THREAD_MODE_FULL_FUNCTION_DEV  ) ?    true : false;
        childInfo.mFullNetworkData   = (modeFlags & SPINEL_THREAD_MODE_FULL_NETWORK_DATA  ) ?    true : false;

        memcpy(childInfo.mExtAddress.m8, eui64, sizeof(uint64_t));

        NL_LOG_CRIT(lrTHCI, "%02d) RLOC=%04x, Age=%3d, AvgRSSI=%3d, LastRSSI=%3d, RxOnWhenIdle=%s\n",
            index,
            childInfo.mRloc16,
            childInfo.mAge,
            childInfo.mAverageRssi,
            childInfo.mLastRssi,
            (childInfo.mRxOnWhenIdle) ? "yes" : "no");

        aArgPtr += parsedLength;
        aArgLen -= parsedLength;
    }

    NL_LOG_CRIT(lrTHCI, "Child Table contains %d entries\n", index);

    return;
}

#if THCI_CONFIG_LOG_NCP_LOGS
static void HandleDebugStream(const uint8_t *aArgPtr, unsigned int aArgLen)
{
    char linebuffer[96 + 1];
    int linepos = 0;

    while (aArgLen--)
    {
        char nextchar = *aArgPtr++;

        if ((nextchar == '\t') || (nextchar >= 32)) {
            linebuffer[linepos++] = nextchar;
        }

        if ((linepos != 0) &&
            ((nextchar == '\n') ||
             (nextchar == '\r') ||
             (linepos >= (sizeof(linebuffer) - 1)) ||
             !aArgLen))
        {
            // flush.
            linebuffer[linepos] = 0;
            NL_LOG_CRIT(lrTHCI, "NCP => %s\n", linebuffer);
            linepos = 0;
        }
    }
}
#endif


#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
static void HandleNetworkWake(const uint8_t *aArgPtr, unsigned int aArgLen)
{
    spinel_ssize_t parsedLength;
    uint8_t event, reason;
    uint16_t timeRemaining;

    parsedLength = spinel_datatype_unpack(aArgPtr, aArgLen,
                                           SPINEL_DATATYPE_STRUCT_S(
                                               SPINEL_DATATYPE_UINT16_S // timeRemaining
                                               SPINEL_DATATYPE_UINT8_S  // event
                                               SPINEL_DATATYPE_UINT8_S  // reason
                                               ),
                                           &timeRemaining,
                                           &event,
                                           &reason);

    if (parsedLength > 0)
    {
        if (gTHCINCPContext.mLurkerWakeCallback)
        {
            gTHCINCPContext.mLurkerWakeCallback(event, timeRemaining, reason);
        }
    }
}
#endif

void HandleLastStatusUpdate(const uint8_t *aArgPtr, unsigned int aArgLen)      
{
    spinel_ssize_t parsedLength;
    spinel_status_t status;

    parsedLength = spinel_datatype_unpack(aArgPtr, aArgLen, SPINEL_DATATYPE_UINT_PACKED_S, &status);
    
    if (parsedLength) 
    {
        gTHCINCPContext.mLastStatus = status;
        NL_LOG_CRIT(lrTHCI, "Last status Error report: %d\n", status);
    }

    if (status >= SPINEL_STATUS_RESET__BEGIN && status <= SPINEL_STATUS_RESET__END)
    {
        // Receiving a last status frame with a value that falls between ...RESET__BEGIN and ...RESET__END
        // indicates that the NCP reset unexpectedly. The host needs to invoke reset recovery when that
        // occurs.
        thciInitiateNCPRecovery();
    }
}

// The Spinel transaction ID is packed in a bit field 4-bits wide.
// value = 1, the kDontCareTransactionId, is reserved by this module to be
// used for Transactions which don't require a response. value = 0 is
// reserved by Spinel.  All other values are returned by this function.
static uint8_t GetNewTransactionId(void)
{
    uint8_t newId = gTHCINCPContext.mTransactionId + 1;
    const uint8_t kMinTransactionId = kDontCareTransactionId + 1;

    if (newId >= kMaxTransactionId)
    {
        newId = kMinTransactionId;
    }

    if (newId < kMinTransactionId)
    {
        newId = kMinTransactionId;
    }

    gTHCINCPContext.mTransactionId = newId;

    return gTHCINCPContext.mTransactionId;
}

static callback_buffer_t* AllocateCallbackBuffer(callback_buffer_state_t aState)
{
    callback_buffer_t *retval = NULL;

    for (size_t i = 0 ; i < THCI_NUM_CALLBACK_BUFFERS ; i++)
    {
        if (gTHCINCPContext.mCallbackBuffers[i].mState == kCallbackBufferStateFree)
        {
            gTHCINCPContext.mCallbackBuffers[i].mState = aState;
            retval = &gTHCINCPContext.mCallbackBuffers[i];
            break;
        }
    }

    if (!retval)
    {
        char *state_string;

        switch (aState)
        {
            case kCallbackBufferStateScanResult:
                state_string = "scan result.";
                break;

            case kCallbackBufferStateLegacyUla:
                state_string = "legacy ULA.";
                break;

            default:
                state_string = "unknown.";
                break;
        }

        NL_LOG_CRIT(lrTHCI, "ERROR: Failed to allocate callback buffer for %s\n", state_string);
    }

    return retval;
}

static thci_message_t *NewMessage(bool aSecurity, uint16_t aLength)
{
    thci_message_t *retval = NULL;
    uint8_t *ringStart = &gTHCINCPContext.mMessageRingBuffer[0];
    uint8_t *ringEnd = &gTHCINCPContext.mMessageRingBuffer[THCI_CONFIG_NCP_TX_MESSAGE_RING_BUFFER_SIZE];
    int status;

    status = nl_er_lock_enter(gTHCINCPContext.mMessageLock);
    nlREQUIRE(!status, done);

    if (gTHCINCPContext.mMessageRingHead == gTHCINCPContext.mMessageRingTail)
    {
        // The logic below is simplified if the head and tail are reset to ringStart
        // whenever they are found to be equal.
        gTHCINCPContext.mMessageRingHead = ringStart;
        gTHCINCPContext.mMessageRingTail = ringStart;
    }

    {
        // terminating point at the end of the ring buffer.
        const uint8_t *termEnd = (gTHCINCPContext.mMessageRingHead < gTHCINCPContext.mMessageRingTail) ? gTHCINCPContext.mMessageRingTail : ringEnd;
        // termination point at the start of the ring buffer.
        const uint8_t *termStart = (gTHCINCPContext.mMessageRingHead > gTHCINCPContext.mMessageRingTail) ? gTHCINCPContext.mMessageRingTail : NULL;

        // include sizeof message header.
        aLength += sizeof(thci_message_t);

        // All allocations must be 4-byte aligned.
        aLength += (sizeof(uint32_t) - (aLength & (sizeof(uint32_t) - 1))) & (sizeof(uint32_t) - 1);

        if (aLength + gTHCINCPContext.mMessageRingHead < termEnd)
        {
            // The message fits in the space available at the end of the ring buffer?
            retval = (thci_message_t *)gTHCINCPContext.mMessageRingHead;
            gTHCINCPContext.mMessageRingHead += aLength;
        }
        else if (termStart && aLength + ringStart < termStart)
        {
            // The message fits in the space available at the beginning of the ring buffer?
            retval = (thci_message_t *)ringStart;
            gTHCINCPContext.mMessageRingEndGap = ringEnd - gTHCINCPContext.mMessageRingHead;
            gTHCINCPContext.mMessageRingHead = ringStart + aLength;
        }
        else
        {
            // not enough space available
            goto unlock;
        }

        // Initialize the header
        retval->mTotalLength = aLength;
        retval->mBuffer = ((uint8_t*)retval) + sizeof(thci_message_t);
        retval->mOffset = 0;
        retval->mLength = 0;
        retval->mFlags = 0;

        if (aSecurity)
        {
            retval->mFlags |= THCI_MESSAGE_FLAG_SECURE;
        }
    }

 unlock:
    nl_er_lock_exit(gTHCINCPContext.mMessageLock);

 done:
    return retval;
}

static void FreeMessage(thci_message_t *aMessage)
{
    uint8_t *messageHead = (uint8_t*)aMessage;
    uint8_t *ringStart = &gTHCINCPContext.mMessageRingBuffer[0];
    uint8_t *ringEnd = &gTHCINCPContext.mMessageRingBuffer[THCI_CONFIG_NCP_TX_MESSAGE_RING_BUFFER_SIZE];
    int status;

    nlREQUIRE(aMessage, done);

    status = nl_er_lock_enter(gTHCINCPContext.mMessageLock);
    nlREQUIRE(!status, done);

    /** 
     * aMessage must either be the oldest message as identified by the ring-tail or the newest
     * message as identified by the ring-head
     */
    nlREQUIRE_ACTION(messageHead == gTHCINCPContext.mMessageRingTail || messageHead + aMessage->mTotalLength == gTHCINCPContext.mMessageRingHead, unlock,
                     NL_LOG_CRIT(lrTHCI, "ERROR: freed message does not align with head or tail %x, %x, %x\n", messageHead, gTHCINCPContext.mMessageRingTail, gTHCINCPContext.mMessageRingHead));

    if (messageHead == gTHCINCPContext.mMessageRingTail)
    {
        // move the tail forward.
        gTHCINCPContext.mMessageRingTail += aMessage->mTotalLength;

        if (gTHCINCPContext.mMessageRingTail + gTHCINCPContext.mMessageRingEndGap >= ringEnd)
        {
            // advance the tail beyond the end-gap that was created when the last message was allocated.
            gTHCINCPContext.mMessageRingTail = ringStart;
            gTHCINCPContext.mMessageRingEndGap = 0;
        }
    }
    else
    {
        // Move the head backward.
        gTHCINCPContext.mMessageRingHead = messageHead;

        if (gTHCINCPContext.mMessageRingHead == ringStart && gTHCINCPContext.mMessageRingEndGap)
        {
            gTHCINCPContext.mMessageRingHead = ringEnd - gTHCINCPContext.mMessageRingEndGap;
            gTHCINCPContext.mMessageRingEndGap = 0;
        }
    }

    if (gTHCINCPContext.mWaitFreeQueueEmpty)
    {
        gTHCINCPContext.mWaitFreeQueueEmpty = false;
        nl_eventqueue_post_event(gTHCINCPContext.mWaitFreeQueue, &sFreeMessageEvent);
    }

 unlock:
    nl_er_lock_exit(gTHCINCPContext.mMessageLock);

 done:
    return;
}

static bool IsMessageSecure(thci_message_t *aMessage)
{
    return (aMessage->mFlags & THCI_MESSAGE_FLAG_SECURE);
}

static void SetMessageSecurity(thci_message_t *aMessage, bool aSecurity)
{
    if (aSecurity)
    {
        aMessage->mFlags |= THCI_MESSAGE_FLAG_SECURE;
    }
    else
    {
        aMessage->mFlags &= ~THCI_MESSAGE_FLAG_SECURE;
    }
}

static bool IsMessageLegacy(thci_message_t *aMessage)
{ 
   return (aMessage->mFlags & THCI_MESSAGE_FLAG_LEGACY);
}

static void SetMessageLegacy(thci_message_t *aMessage, bool aLegacy)
{
    if (aLegacy)
    {
        aMessage->mFlags |= THCI_MESSAGE_FLAG_LEGACY;
    }
    else
    {
        aMessage->mFlags &= ~THCI_MESSAGE_FLAG_LEGACY;
    }
}

static int AppendMessage(thci_message_t *aMessage, const uint8_t *aBuf, uint16_t aLen)
{
    int retval = 0;

    nlREQUIRE_ACTION(aLen + aMessage->mLength <= aMessage->mTotalLength - sizeof(thci_message_t), done, retval = -ENOMEM);

    memcpy(&aMessage->mBuffer[aMessage->mLength], aBuf, aLen);
    aMessage->mLength += aLen;

 done:
    return retval;
}

static void ResetOffset(thci_message_t *aMessage)
{
    aMessage->mOffset = 0;
}

static int ReadMessage(thci_message_t *aMessage, uint8_t *aBuf, uint16_t aLen)
{
    int retval = 0;

    retval = (aLen > aMessage->mLength - aMessage->mOffset) ? aMessage->mLength - aMessage->mOffset : aLen;

    if (retval)
    {
        memcpy(aBuf, &aMessage->mBuffer[aMessage->mOffset], retval);
        aMessage->mOffset += retval;
    }

    return retval;
}

static int CreateTHCIMessageFromPbuf(struct pbuf *aPbuf, thci_message_t **aMessage)
{
    bool linkSecurityEnabled = THCI_ENABLE_MESSAGE_SECURITY(gTHCISDKContext.mSecurityFlags);
    thci_message_t *message = NULL;
    int retval = -EINVAL;

    nlREQUIRE(aPbuf != NULL && aMessage != NULL, done);

    do
    {
        nl_event_t *ev;
        const nl_time_ms_t timeout = 2000;

        // allocate a thci_message_t. Block if one is not immediately available.
        message = NewMessage(linkSecurityEnabled, aPbuf->tot_len);

        if (message)
        {
            break;
        }

        ev = nl_eventqueue_get_event_with_timeout(gTHCINCPContext.mWaitFreeQueue, timeout);
        nlREQUIRE_ACTION(ev != NULL, done, retval = -ENOMEM; NL_LOG_CRIT(lrTHCI, "ERROR: Wait for free message timed out.\n"));
        // reset the variable after pulling an event.
        gTHCINCPContext.mWaitFreeQueueEmpty = true;

    } while (message == NULL);

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
            retval = AppendMessage(message, pbuf_chunk->payload, len);
            nlREQUIRE_ACTION(!retval, done, NL_LOG_CRIT(lrTHCI, "%s: AppendMessage failed len = %u\n", __FUNCTION__, len));

            pbuf_chunk = pbuf_chunk->next;
        }

        nlREQUIRE_ACTION(tot_len == 0, done, NL_LOG_CRIT(lrTHCI, "%s: pbuf parse error tot_len=%u\n", __FUNCTION__, tot_len));

        if (SendProvisionalJoinResponseInsecurely())
        {
            struct ip6_hdr ip6Hdr;
            uint16_t srcPort;
            // For router devices that have security enabled but are allowing
            // provisional join it will be necessary to look also at the pbuf's
            // UDP port and whether Joining is enabled, in order to set this properly.
            ResetOffset(message);

            len = ReadMessage(message, (uint8_t *)&ip6Hdr, sizeof(ip6Hdr));

            if(len == sizeof(ip6Hdr) && IP6H_NEXTH((&ip6Hdr)) == IP6_NEXTH_TCP)
            {
                len = ReadMessage(message, (uint8_t *)&srcPort, sizeof(srcPort));
                nlREQUIRE_ACTION(len == sizeof(srcPort), done, retval = -EBADMSG);

                srcPort = lwip_ntohs(srcPort);

                if (srcPort == gTHCISDKContext.mInsecureSourcePort)
                {
                    // set the message as insecure.
                    SetMessageSecurity(message, false);
                }
            }
        }

        // reset the offset to the beginning of the message for reading later.
        ResetOffset(message);

        *aMessage = message;
        retval = 0; // success
    }

 done:
    if (message && retval)
    {
        FreeMessage(message);
    }

    return retval;
}

static void ReceiveIp6Datagram(unsigned int aCommand, spinel_prop_key_t aKey, const uint8_t *aBuf, unsigned int aBufLength)
{
    struct pbuf *pbuf = NULL;
    err_t err;
    spinel_ssize_t parsedLength;
    const uint8_t *argPtr = NULL;
    unsigned int argLen = 0;
    struct ip6_hdr ip6Hdr;
    thci_netif_tags_t tag = THCI_NETIF_TAG_THREAD;
    const bool isSecure = (aKey != SPINEL_PROP_STREAM_NET_INSECURE);

    parsedLength = spinel_datatype_unpack(aBuf, aBufLength, "D.", &argPtr, &argLen);
    nlREQUIRE_ACTION(parsedLength == aBufLength, done, NL_LOG_CRIT(lrTHCI, "Failed to parse length from Ip6Datagram\n"));

    // Pass all packets up to LwIP.
    pbuf = pbuf_alloc(PBUF_RAW, argLen, PBUF_POOL);
    nlREQUIRE_ACTION(pbuf != NULL, done, NL_LOG_CRIT(lrTHCI, "pbufs exhausted...dropping incoming packet.\n"));

    memcpy(pbuf->payload, argPtr, argLen);
    memcpy(&ip6Hdr, pbuf->payload, sizeof(ip6Hdr));

#ifdef BUILD_FEATURE_THCI_CERT
    thci_cert_rx_corrupt(pbuf);
#endif //BUILD_FEATURE_THCI_CERT

    if (isSecure && SendProvisionalJoinResponseInsecurely())
    {
        if (IP6H_NEXTH((&ip6Hdr)) == IP6_NEXTH_TCP)
        {
            uint16_t dstPort;

            // read the 2-byte destination port from the TCP header
            memcpy(&dstPort, &((uint8_t *)pbuf->payload)[sizeof(ip6Hdr) + sizeof(dstPort)], sizeof(dstPort));
            dstPort = lwip_ntohs(dstPort);

            // If this frame has the insecure port assigned as the dst port, then
            // future frames must be secure.
            if (dstPort == gTHCISDKContext.mInsecureSourcePort)
            {
                gTHCISDKContext.mSecurityFlags |= THCI_SECURITY_FLAG_SECURE_MSG_RXD_ON_INSECURE_PORT;
                NL_LOG_CRIT(lrTHCI, "Received secure message on insecure port\n");
            }
        }
    }

    NL_LOG_DEBUG(lrTHCI, "IP RX len: %u secure: %s cksum: 0x%04x\n", argLen, ((isSecure) ? "yes" : "no"), thciGetChecksum(pbuf));
    NL_LOG_DEBUG(lrTHCI, "from: %s\n", ip6addr_ntoa((const ip6_addr_t *)&ip6Hdr.src));  // IPv6 Header Source
    NL_LOG_DEBUG(lrTHCI, "  to: %s\n", ip6addr_ntoa((const ip6_addr_t *)&ip6Hdr.dest)); // IPv6 Header Destination

#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
    if (aCommand == SPINEL_CMD_VENDOR_NEST_PROP_VALUE_IS)
    {
        tag = THCI_NETIF_TAG_LEGACY;
    }
#endif

    err = tcpip_input(pbuf, gTHCISDKContext.mNetif[tag]);

    if (err == ERR_OK)
    {
        pbuf = NULL;
    }

 done:
    if (pbuf)
    {
        pbuf_free(pbuf);
    }

    return;
}

static int StateChangeEventHandler(nl_event_t *aEvent, void *aClosure)
{
    if (gTHCINCPContext.mStateChangeCallback)
    {
        uint32_t flags = gTHCINCPContext.mStateChangeFlags;
        gTHCINCPContext.mStateChangeFlags = 0;
        gTHCINCPContext.mStateChangeCallback(flags, NULL);
    }

    return NLER_SUCCESS;
}

static int LegacyUlaChangeEventHandler(nl_event_t *aEvent, void *aClosure)
{
    for (size_t i = 0 ; i < THCI_NUM_CALLBACK_BUFFERS ; i++)
    {
        if (gTHCINCPContext.mCallbackBuffers[i].mState == kCallbackBufferStateLegacyUla)
        {
            if (gTHCINCPContext.mLegacyUlaCallback)
            {
                gTHCINCPContext.mLegacyUlaCallback(gTHCINCPContext.mCallbackBuffers[i].mContent.mLegacyUla);
            }

            gTHCINCPContext.mCallbackBuffers[i].mState = kCallbackBufferStateFree;
        }
    }

    return NLER_SUCCESS;
}

static int ScanResultEventHandler(nl_event_t *aEvent, void *aClosure)
{
    for (size_t i = 0 ; i < THCI_NUM_CALLBACK_BUFFERS ; i++)
    {
        if (gTHCINCPContext.mCallbackBuffers[i].mState == kCallbackBufferStateScanResult)
        {
            if (gTHCINCPContext.mScanResultCallback)
            {
                gTHCINCPContext.mScanResultCallback(&gTHCINCPContext.mCallbackBuffers[i].mContent.mScanResult, gTHCINCPContext.mScanResultCallbackContext);
            }

            gTHCINCPContext.mCallbackBuffers[i].mState = kCallbackBufferStateFree;
        }
    }

    return NLER_SUCCESS;
}

static int ScanCompleteEventHandler(nl_event_t *aEvent, void *aClosure)
{
    if (gTHCINCPContext.mScanResultCallback)
    {
        // pass NULL as the scan result to indicate scan complete.
        gTHCINCPContext.mScanResultCallback(NULL, gTHCINCPContext.mScanResultCallbackContext);
    }

    return NLER_SUCCESS;
}

static int NCPRecoveryEventHandler(nl_event_t *aEvent, void *aClosure)
{
    // announce recovery to Upper layer so that it can re-establish state.
    if (gTHCINCPContext.mResetRecoveryCallback)
    {
        gTHCINCPContext.mResetRecoveryCallback();
    }

    return NLER_SUCCESS;
}

// This function handles unsolicited control frames from the NCP.
// Handling consists of extracting pertinent information from aArgPtr and posting an appropriate event for
// post processing.  Posting an event is necessary to avoid recursive execution.
// Do not call client callbacks from this function as those callbacks may make calls to
// thci API's which result in a new effort to extract bytes from the UART fifo. This call was made from
// the UART fifo extraction routine, hence executing callbacks from here could result in recursive execution.
static void ReceiveControlFrame(uint8_t aHeader, unsigned int aCommand, spinel_prop_key_t aKey, const uint8_t *aArgPtr, unsigned int aArgLen)
{
    spinel_ssize_t parsedLength;
    uint32_t prevStateFlags = gTHCINCPContext.mStateChangeFlags;

    if (aCommand == SPINEL_CMD_PROP_VALUE_IS)
    {
        switch ((uint16_t)aKey)
        {
        case SPINEL_PROP_LAST_STATUS:
            HandleLastStatusUpdate(aArgPtr, aArgLen);
            break;

        case SPINEL_PROP_NET_ROLE:
            {
                // OpenThread Role Change
                spinel_net_role_t spinelRole;

                parsedLength = spinel_datatype_unpack(aArgPtr, aArgLen, SPINEL_DATATYPE_UINT8_S, &spinelRole);
                nlREQUIRE_ACTION(parsedLength > 0, done, NL_LOG_CRIT(lrTHCI, "Failed to parse role from frame.\n"));

                gTHCISDKContext.mDeviceRole = TranslateSpinelRole(spinelRole);

                gTHCINCPContext.mStateChangeFlags |= OT_CHANGED_THREAD_ROLE;
            }
            break;

        case SPINEL_PROP_NEST_LEGACY_ULA_PREFIX:
            {
                const uint8_t *legacyUlaPrefix = NULL;
                spinel_size_t len;
                callback_buffer_t *callbackBuffer = AllocateCallbackBuffer(kCallbackBufferStateLegacyUla);

                nlREQUIRE(callbackBuffer != NULL, done);

                parsedLength = spinel_datatype_unpack(aArgPtr, aArgLen, SPINEL_DATATYPE_DATA_S, &legacyUlaPrefix, &len);
                nlREQUIRE_ACTION(parsedLength > 0, done, NL_LOG_CRIT(lrTHCI, "Failed to parse legacy ula.\n"));

                memcpy(&callbackBuffer->mContent.mLegacyUla[0], legacyUlaPrefix, THCI_LEGACY_ULA_SIZE_BYTES);

                nl_eventqueue_post_event(gTHCISDKContext.mInitParams.mSdkQueue, &sLegacyUlaChangeEvent);
            }
            break;

        case SPINEL_PROP_MAC_SCAN_STATE:
            // scan complete
            nl_eventqueue_post_event(gTHCISDKContext.mInitParams.mSdkQueue, &sScanCompleteEvent);
            break;

        case SPINEL_PROP_THREAD_CHILD_TABLE:
            HandleChildTableUpdate(aArgPtr, aArgLen);
            break;

        case SPINEL_PROP_IPV6_ADDRESS_TABLE:
            // This prop could also be received for OT_CHANGED_IP6_ADDRESS_REMOVED but 
            // there is no way to know. NetworkManager currently doesn't care which flag provoked
            // this property event.
            gTHCINCPContext.mStateChangeFlags |= OT_CHANGED_IP6_ADDRESS_ADDED;
            break;

        case SPINEL_PROP_IPV6_MULTICAST_ADDRESS_TABLE:
            // This prop could also be received for OT_CHANGED_IP6_MULTICAST_UNSUBSRCRIBED but 
            // there is no way to know. NetworkManager currently doesn't care which flag provoked
            // this property event.
            gTHCINCPContext.mStateChangeFlags |= OT_CHANGED_IP6_MULTICAST_SUBSRCRIBED;
            break;

#if THCI_CONFIG_LOG_NCP_LOGS
        case SPINEL_PROP_STREAM_DEBUG:
            HandleDebugStream(aArgPtr, aArgLen);
            break;
#endif

#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
        case SPINEL_PROP_VENDOR_NEST_NETWORK_WAKE_STATE:
            HandleNetworkWake(aArgPtr, aArgLen);
            break;
#endif
        default:
            break; // Ignore this control frame.
        }

        // If mStateChangeFlags transitioned from 0 to non-zero an StateChangeEvent needs to be posted.
        if (!prevStateFlags && gTHCINCPContext.mStateChangeFlags)
        {
            nl_eventqueue_post_event(gTHCISDKContext.mInitParams.mSdkQueue, &sStateChangeEvent);
        }
    }
    else if (aCommand == SPINEL_CMD_PROP_VALUE_INSERTED)
    {
        switch (aKey)
        {
        case SPINEL_PROP_MAC_SCAN_BEACON:
            // new scan result
            if (gTHCINCPContext.mScanResultCallback)
            {
                const char* networkid = "";
                const uint8_t* xpanid = NULL;
                unsigned int xpanidLen = 0;
                const spinel_eui64_t* extAddress;
                uint8_t flags;
                callback_buffer_t *callbackBuffer = AllocateCallbackBuffer(kCallbackBufferStateScanResult);
                otActiveScanResult *result;

                nlREQUIRE(callbackBuffer != NULL, done);

                result = &callbackBuffer->mContent.mScanResult;

                parsedLength = spinel_datatype_unpack(aArgPtr, aArgLen, "CcT(ESSC.)T(iCUD.).",
                                                        &result->mChannel,
                                                        &result->mRssi,
                                                        &extAddress,
                                                        NULL, // saddr
                                                        &result->mPanId,
                                                        &result->mLqi,
                                                        NULL, // protocol
                                                        &flags,
                                                        &networkid,
                                                        &xpanid,
                                                        &xpanidLen);

                memcpy(result->mExtAddress.m8, extAddress, sizeof(result->mExtAddress.m8));
                memcpy(result->mNetworkName.m8, networkid, sizeof(result->mNetworkName.m8));
                memcpy(result->mExtendedPanId.m8, xpanid, sizeof(result->mExtendedPanId.m8));
                result->mIsJoinable = (flags & SPINEL_BEACON_THREAD_FLAG_JOINABLE) ? true : false;

                nl_eventqueue_post_event(gTHCISDKContext.mInitParams.mSdkQueue, &sScanResultEvent);
            }
            break;

        default:
            break; // Ignore this control frame.
        }
    }

 done:
    return;
}

/* Called by LwIP for transmission of IP packets.
 *
 * Note that the pbuf q passed in is not owned by this method. Thus, it must
 * be returned with the same ref count with which it came.
 */
static err_t LwIPOutputIP6(struct netif *netif, struct pbuf *pbuf,
                           struct ip6_addr *ipaddr)
{
    err_t retval = ERR_OK;
    thci_message_t *message = NULL;

#ifdef BUILD_FEATURE_THCI_CERT
    thci_cert_tx_corrupt(pbuf);
#endif //BUILD_FEATURE_THCI_CERT

    nlREQUIRE_ACTION(pbuf->len <= (NL_THCI_PAYLOAD_MTU), done, retval = ERR_VAL);
#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
    nlREQUIRE_ACTION(netif == gTHCISDKContext.mNetif[THCI_NETIF_TAG_THREAD] || netif == gTHCISDKContext.mNetif[THCI_NETIF_TAG_LEGACY], done, retval = ERR_IF);
#else
    nlREQUIRE_ACTION(netif == gTHCISDKContext.mNetif[THCI_NETIF_TAG_THREAD], done, retval = ERR_IF);
#endif
    nlREQUIRE_ACTION(0 == CreateTHCIMessageFromPbuf(pbuf, &message), done, retval = ERR_MEM);

#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
    if (netif == gTHCISDKContext.mNetif[THCI_NETIF_TAG_LEGACY])
    {
        SetMessageLegacy(message, true);
    }
#endif
    nlREQUIRE_ACTION(0 == EnqueueMessage((otMessage*)message), done, retval = ERR_INPROGRESS);

    {
        struct ip6_hdr *pHeader = pbuf->payload;

        NL_LOG_DEBUG(lrTHCI, "IP TX len: %u secure: %s cksum: 0x%04x\n", message->mLength, ((IsMessageSecure(message)) ? "yes" : "no"), thciGetChecksum(pbuf));
        NL_LOG_DEBUG(lrTHCI, "from: %s\n", ip6addr_ntoa((const ip6_addr_t*)&(pHeader->src)));  // IPv6 Header Source
        NL_LOG_DEBUG(lrTHCI, "  to: %s\n", ip6addr_ntoa((const ip6_addr_t*)&(pHeader->dest))); // IPv6 Header Destination
    }

    // Race conditions can exist between the LWIP task here and the THCI task 
    // in OutgoingIPPacketEventHandler which can result in multiple sOutgoingIPPacketEvents posted to the event queue.
    // This __sync_fetch_and_or ensures that only one sOutgoingIPPacketEvent is ever posted to the queue.
    if (!__sync_fetch_and_or(&sOutgoingIPPacketEventPosted, 1))
    {
        nl_eventqueue_post_event(gTHCISDKContext.mInitParams.mSdkQueue, &sOutgoingIPPacketEvent);
    }

 done:
    if (retval != ERR_OK)
    {
        NL_LOG_CRIT(lrTHCI, "Message queue error (%d)...dropping outgoing packet.\n", retval);

        if (message)
        {
            FreeMessage(message);
        }
    }

    return retval;
}

// If the message is TCP then the source port is made insecure so that
// response messages won't be filtered out.
static void OpenSourcePort(thci_message_t *aMessage)
{
    struct ip6_hdr ip6Hdr;
    uint16_t len;
    uint16_t srcPort;
    otError error = OT_ERROR_NONE;

    len = ReadMessage(aMessage, (uint8_t *)&ip6Hdr, sizeof(ip6Hdr));
    nlREQUIRE(len == sizeof(ip6Hdr), done);

    nlREQUIRE_ACTION(IP6H_NEXTH((&ip6Hdr)) == IP6_NEXTH_TCP, done, error = OT_ERROR_INVALID_ARGS);

    len = ReadMessage(aMessage, (uint8_t *)&srcPort, sizeof(srcPort));
    nlREQUIRE_ACTION(len == sizeof(srcPort), done, error = OT_ERROR_PARSE);

    srcPort = lwip_ntohs(srcPort);

    NL_LOG_DEBUG(lrTHCI, "Open Port %d\n", srcPort);

    error = thciAddUnsecurePort(srcPort);
    nlREQUIRE(error == OT_ERROR_NONE, done);

    gTHCISDKContext.mInsecureSourcePort = srcPort;
    gTHCISDKContext.mSecurityFlags |= THCI_SECURITY_FLAG_INSECURE_SOURCE_PORT;

 done:
    if (error != OT_ERROR_NONE)
    {
        NL_LOG_CRIT(lrTHCI, "OpenSourcePort failed with err = %d\n", error);
    }

    ResetOffset(aMessage);

    return;
}

// Process pbufs on the outgoing queue.
static int OutgoingIPPacketEventHandler(nl_event_t *aEvent, void *aClosure)
{
    thci_message_t *message;
    otError status = OT_ERROR_NONE;
    spinel_prop_key_t key;
    uint32_t command;

    sOutgoingIPPacketEventPosted = 0;

    nlREQUIRE(gTHCINCPContext.mModuleState == kModuleStateInitialized, done);
    // When the Stall is on don't post an event even if message queue is not empty.
    nlREQUIRE(!gTHCISDKContext.mStallOutgoingDataPackets, nopost_exit);

    while(!IsMessageQueueEmpty())
    {
        message = (thci_message_t *)DequeueMessage();

        if (NeedToOpenInsecureSourcePort())
        {
            // If this condition is true, then this is a device that is joining provisionally.
            // As such, it is necessary that the source port also be made insecure. We pass the
            // outgoing IP packet to OpenSourcePort so that it can extract the source
            // port from the TCP header and add it to OT's insecure port list.
            OpenSourcePort(message);
        }

#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
        if (IsMessageLegacy(message))
        {
            command = SPINEL_CMD_VENDOR_NEST_PROP_VALUE_SET;
            key = SPINEL_PROP_STREAM_NET;
        }
        else
#endif
        {
            command = SPINEL_CMD_PROP_VALUE_SET;
            key = (IsMessageSecure(message)) ? SPINEL_PROP_STREAM_NET : SPINEL_PROP_STREAM_NET_INSECURE;
        }

        {
            uint8_t tid = GetNewTransactionId();
            uint32_t last;
            spinel_ssize_t parsedLength;
            const uint8_t *argPtr = NULL;
            size_t argLen;

            status = thciUartFrameSend(tid, command, key, SPINEL_DATATYPE_DATA_WLEN_S, message->mBuffer, message->mLength);

            FreeMessage(message);
            nlREQUIRE(status == OT_ERROR_NONE, done);


            status = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_LAST_STATUS, &argPtr, &argLen);
            nlREQUIRE(status == OT_ERROR_NONE, done);

            parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_UINT_PACKED_S, &last);
            nlREQUIRE_ACTION(parsedLength > 0, done, status = OT_ERROR_PARSE);

            if (last != SPINEL_STATUS_OK)
            {
                NL_LOG_CRIT(lrTHCI, "IP packet NCP rejected! %x %x\n", last, key);
            }
        }
    }

 done:
    if (status != OT_ERROR_NONE)
    {
        NL_LOG_CRIT(lrTHCI, "ERROR: OutgoingIPPacketEventHandler %d\n", status);
    }

    if (!IsMessageQueueEmpty())
    {
        // If this function exits while the message queue is not empty an event must be posted so that the
        // producer-consumer flow does not stall. This can happen for instance if this function
        // exits prematurely with an error.

        // Race conditions can exist between the LWIP task in LwIPOutputIP6 and the THCI task 
        // here which can result in multiple sOutgoingIPPacketEvents posted to the event queue.
        // This __sync_fetch_and_or ensures that only one sOutgoingIPPacketEvent is ever posted to the queue.
        if (!__sync_fetch_and_or(&sOutgoingIPPacketEventPosted, 1))
        {
            nl_eventqueue_post_event(gTHCISDKContext.mInitParams.mSdkQueue, &sOutgoingIPPacketEvent);
        }
    }

 nopost_exit:
    return NLER_SUCCESS;
}

static otError AllowLocalNetworkDataChange(bool aUnlock)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    bool status;
    spinel_ssize_t parsedLength;

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_THREAD_ALLOW_LOCAL_NET_DATA_CHANGE, SPINEL_DATATYPE_BOOL_S, aUnlock);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_THREAD_ALLOW_LOCAL_NET_DATA_CHANGE, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_BOOL_S, &status);
    nlREQUIRE_ACTION(parsedLength > 0 && status == aUnlock, done, retval = OT_ERROR_FAILED);

 done:
    return retval;
}

static otError SetScanMaskAll(uint32_t aScanChannels)
{
    otError retval = OT_ERROR_INVALID_ARGS;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    uint8_t channelArray[32];
    size_t index = 0;

    #define MIN_SUPPORTED_CHANNEL 11
    #define MAX_SUPPORTED_CHANNEL 26

    for (int i = MIN_SUPPORTED_CHANNEL; i <= MAX_SUPPORTED_CHANNEL; i++)
        {
            if (0 != (aScanChannels & (1 << i)))
            {
                channelArray[index++] = i;
            }
        }

        retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_MAC_SCAN_MASK, SPINEL_DATATYPE_DATA_S, channelArray, index);
        nlREQUIRE(retval == OT_ERROR_NONE, done);

        retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_MAC_SCAN_MASK, &argPtr, &argLen);
        nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    return retval;
}

static otError UnimplementedAPI(const char *aName)
{
    otError retval = OT_ERROR_NOT_IMPLEMENTED;

    NL_LOG_CRIT(lrTHCI, "Warning: Call to unimplemented API; %s\n", aName);

    return retval;
}

static otError thciGetSpinelProperty(spinel_prop_key_t aKey, spinel_datatype_t *aType,
                                     void *aVal, spinel_ssize_t *aOutLen)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;

    nlREQUIRE_ACTION(aVal    != NULL, done, retval = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(aOutLen != NULL, done, retval = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_GET, aKey, NULL);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, aKey, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    *aOutLen = spinel_datatype_unpack(argPtr, argLen, aType, aVal);
    nlREQUIRE_ACTION(*aOutLen > 0, done, retval = OT_ERROR_FAILED);

done:
    return retval;
}

static otError thciGetSpinelDataProperty(spinel_prop_key_t aKey, spinel_datatype_t *aType, 
                                         uint8_t *aOutData, uint16_t aInSize, uint16_t *aOutSize)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    spinel_ssize_t parseLength;
    uint8_t *dataPtr = NULL;
    unsigned int dataLen;

    nlREQUIRE_ACTION(aOutData != NULL, done, retval = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(aOutSize != NULL, done, retval = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(aInSize  >  0,    done, retval = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_GET, aKey, NULL);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, aKey, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parseLength = spinel_datatype_unpack(argPtr, argLen, aType, &dataPtr, &dataLen);
    nlREQUIRE_ACTION(parseLength >  0, done, retval = OT_ERROR_FAILED);
    nlREQUIRE_ACTION(dataPtr     != 0, done, retval = OT_ERROR_FAILED);

    nlREQUIRE_ACTION(dataLen <= aInSize, done, retval = OT_ERROR_FAILED);

    memcpy(aOutData, dataPtr, dataLen);
    *aOutSize = dataLen;

done:
    return retval;
}

static otError ThreadStartStop(bool aStart)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    bool status;
    spinel_ssize_t parsedLength;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_NET_STACK_UP, SPINEL_DATATYPE_BOOL_S, aStart);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_NET_STACK_UP, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_BOOL_S, &status);
    nlREQUIRE_ACTION(parsedLength > 0 && status == aStart, done, retval = OT_ERROR_FAILED);

    // When Thead Starts on the NCP all data packets must be secured.
    if (aStart)
    {
        gTHCISDKContext.mSecurityFlags |= THCI_SECURITY_FLAG_THREAD_STARTED;
    }
    else
    {
        gTHCISDKContext.mSecurityFlags &= ~THCI_SECURITY_FLAG_THREAD_STARTED;
    }

 done:
    return retval;
}

static otError ThreadUpDown(bool aUp)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    bool status;
    spinel_ssize_t parsedLength;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_NET_IF_UP, SPINEL_DATATYPE_BOOL_S, aUp);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_NET_IF_UP, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_BOOL_S, &status);
    nlREQUIRE_ACTION(parsedLength > 0 && status == aUp, done, retval = OT_ERROR_FAILED);

 done:
    return retval;
}

static otError ResetNcpWithVerify(thciUartDataFrameCallback_t aDataCB, thciUartControlFrameCallback_t aControlCB)
{
    const uint8_t *argPtr = NULL;
    size_t argLen;
    otError retval = OT_ERROR_NONE;
    uint32_t status;
    spinel_ssize_t parsedLength;
    int loop = 0;
    const int maxTries = 3;
    const bool startBootloader = true;

    // This loop will try to reset the NCP maxTries times before giving up.  Normally, one attempt should suffice,
    // however on T2, after a reboot, 2 attempts has been necessary.
    // TODO: Determine why T2 needs 2 resets after reboot.
    while(loop++ < maxTries)
    {
        thciUartDisable();

        thciHardResetNcp(!startBootloader);

        retval = thciUartEnable(aDataCB, aControlCB);
        nlREQUIRE(retval == OT_ERROR_NONE, done);

        retval = thciUartWaitForResponseIgnoreTimeout(kDontCareTransactionId, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_LAST_STATUS, &argPtr, &argLen);

        if (retval == OT_ERROR_NONE)
        {
            break;
        } 
    }

    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_UINT_PACKED_S, &status);
    nlREQUIRE_ACTION(parsedLength > 0, done, retval = OT_ERROR_PARSE);

    nlREQUIRE_ACTION(SPINEL_STATUS_RESET__BEGIN <= status && SPINEL_STATUS_RESET__END >= status, done, retval = OT_ERROR_NO_ACK);

 done:
    return retval;
}

static otError ReEstablishNcpComm(thciUartDataFrameCallback_t aDataCB, thciUartControlFrameCallback_t aControlCB)
{
    otError retval;
    spinel_net_role_t spinelRole;
    spinel_ssize_t parsedLength;

    thciUartDisable();

    retval = thciUartEnable(aDataCB, aControlCB);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    // Confirm that communication exists with the NCP with the following command-response sequence. This 
    // may receive other frames from the UART before the expected response frame so the module must 
    // be setup to receive them despite this function being called from InitializeInternal.
    retval = thciGetSpinelProperty(SPINEL_PROP_NET_ROLE, SPINEL_DATATYPE_UINT8_S, &spinelRole, &parsedLength);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    if (retval != OT_ERROR_NONE)
    {
        NL_LOG_CRIT(lrTHCI, "WARNING: %s failed with error (%d), resetting NCP!\n", __FUNCTION__, retval);
    }
    else
    {
        NL_LOG_CRIT(lrTHCI, "Successfully re-established NCP comm without reset.\n");
    }

    return retval;
}

/**
 * API IMPLEMENTATION
 */

otError thciNetifInit(struct netif *aNetif, thci_netif_tags_t aTag, const char * aInterfaceName)
{
    otError retval = OT_ERROR_INVALID_ARGS;

    nlREQUIRE(aTag < THCI_NETIF_TAG_COUNT, done);
    nlREQUIRE(sizeof(aNetif->name) == strlen(aInterfaceName), done);

    memcpy(&aNetif->name[0], aInterfaceName, strlen(aInterfaceName)); // intentionally omits the null term in the copy.
    aNetif->output = NULL;

#if LWIP_IPV6
    aNetif->output_ip6 = LwIPOutputIP6;
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

otError InitializeInternal(bool aMandatoryNcpReset, bool aAPIInitialize, thci_callbacks_t *aCallbacks, 
                           thciUartDataFrameCallback_t aDataCB, thciUartControlFrameCallback_t aControlCB)
{
    otError retval = OT_ERROR_NONE;
    size_t i;

    if (aCallbacks)
    {
        gTHCINCPContext.mStateChangeCallback = aCallbacks->mStateChangeCallback;
        gTHCINCPContext.mResetRecoveryCallback = aCallbacks->mResetRecoveryCallback;
        gTHCINCPContext.mLegacyUlaCallback = aCallbacks->mLegacyUlaCallback;
#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
        gTHCINCPContext.mLurkerWakeCallback = aCallbacks->mLurkerWakeCallback;
#endif
    }
    else
    {
        gTHCINCPContext.mStateChangeCallback = NULL;
        gTHCINCPContext.mResetRecoveryCallback = NULL;
        gTHCINCPContext.mLegacyUlaCallback = NULL;
#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
        gTHCINCPContext.mLurkerWakeCallback = NULL;
#endif
    }

    gTHCINCPContext.mScanResultCallback = NULL;
    
    if (aAPIInitialize)
    {
        thciSafeInitialize();

        if (gTHCINCPContext.mMessageLock == NULL)
        {
            gTHCINCPContext.mMessageLock = nl_er_lock_create();
            nlREQUIRE_ACTION(gTHCINCPContext.mMessageLock != NULL, done, retval = OT_ERROR_FAILED);

            // equating the RingHead and RingTail effectively free's the entire ring buffer.
            gTHCINCPContext.mMessageRingHead = gTHCINCPContext.mMessageRingTail = &gTHCINCPContext.mMessageRingBuffer[0];
            gTHCINCPContext.mMessageRingEndGap = 0;
        }

        if (gTHCINCPContext.mWaitFreeQueue == NULL)
        {
            gTHCINCPContext.mWaitFreeQueue = nl_eventqueue_create(&gTHCINCPContext.mWaitFreeQueueMem, sizeof(gTHCINCPContext.mWaitFreeQueueMem));
            nlREQUIRE_ACTION(gTHCINCPContext.mWaitFreeQueue != NULL, done, retval = OT_ERROR_FAILED);

            nl_eventqueue_disable_event_counting(gTHCINCPContext.mWaitFreeQueue);
            gTHCINCPContext.mWaitFreeQueueEmpty = true;
        }

        for (i = 0 ; i < THCI_NUM_CALLBACK_BUFFERS ; i++)
        {
            gTHCINCPContext.mCallbackBuffers[i].mState = kCallbackBufferStateFree;
        }
    }

    gTHCINCPContext.mStateChangeFlags = 0;
    gTHCINCPContext.mModuleState = kModuleStateInitialized;

    if (!aMandatoryNcpReset)
    {
        retval = ReEstablishNcpComm(aDataCB, aControlCB);
    }

    if (aMandatoryNcpReset || retval != OT_ERROR_NONE)
    {
        retval = ResetNcpWithVerify(aDataCB, aControlCB);
        nlREQUIRE(retval == OT_ERROR_NONE, done);
    }

    

 done:
    return retval;
}

otError FinalizeInternal(bool inAPIFinalize)
{
    otError retval = OT_ERROR_NONE;
    const uint8_t *argPtr = NULL;
    size_t argLen;
    uint8_t tid = GetNewTransactionId();

    if (inAPIFinalize)
    {
        thciSafeFinalize();
    }

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_POWER_STATE, SPINEL_DATATYPE_UINT8_S, SPINEL_POWER_STATE_OFFLINE);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_LAST_STATUS, &argPtr, &argLen);

    //TODO: The NCP currently returns SPINEL_STATUS_UNIMPLEMENTED as the power state feature has not yet been implemented.  When this changes
    // we will see this log, announcing that the feature is now available.
    if (retval == OT_ERROR_FAILED)
    {
        NL_LOG_DEBUG(lrTHCI, "ALERT: NCP now supports SPINEL_PROP_POWER_STATE!\n");
    }

 done:
    // Despite failing the commands above the UART should still be disabled so as to make the 
    // the interface recoverable when later calling thciInitialize.
    thciUartDisable();

    gTHCINCPContext.mModuleState = kModuleStateUninitialized;

    return retval;
}

otError thciInitialize(thci_callbacks_t *aCallbacks)
{
    const bool apiInitialize = true;
#if THCI_CONFIG_INITIALIZE_WITHOUT_NCP_RESET
    const bool mandatoryReset = (nl_board_get_reset_reason() != NL_RESET_REASON_WAKEUP);
#else
    const bool mandatoryReset = true;
#endif
    otError retval;

    nlREQUIRE_ACTION(aCallbacks != NULL, done, retval = OT_ERROR_INVALID_ARGS);

    retval = InitializeInternal(mandatoryReset, apiInitialize, aCallbacks, ReceiveIp6Datagram, ReceiveControlFrame);

 done:
    return retval;
}

otError thciFinalize(void)
{
    const bool apiFinalize = true;

    return FinalizeInternal(apiFinalize);
}

otError thciThreadStop(void)
{
    return ThreadStartStop(false);
}

otError thciThreadStart(void)
{
    return ThreadStartStop(true);
}

otError thciInterfaceUp(void)
{
    return ThreadUpDown(true);
}

otError thciInterfaceDown(void)
{
    return ThreadUpDown(false);
}

otError thciIsInterfaceEnabled(bool *aEnabled)
{
    spinel_ssize_t parsedLength;

    return thciGetSpinelProperty(SPINEL_PROP_NET_IF_UP, SPINEL_DATATYPE_BOOL_S,
                                 aEnabled, &parsedLength);
}

otError thciPersistentInfoErase(void)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    unsigned int status;
    spinel_ssize_t parsedLength;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_NET_CLEAR, kDefaultSpinelPropertyKey, NULL);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_LAST_STATUS, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_UINT_PACKED_S, &status);
    nlREQUIRE_ACTION(parsedLength > 0, done, retval = OT_ERROR_PARSE);
    nlREQUIRE_ACTION(status == SPINEL_STATUS_OK, done, retval = OT_ERROR_FAILED);

 done:
    return retval;
}

otError thciSetReceiveIp6DatagramFilterEnabled(bool aEnabled)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    bool status;
    spinel_ssize_t parsedLength;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    // NOTE: the spinel Debug-passthru property is the opposite sign from the SetReceiveIp6DatagramFilterEnabled boolean. Hence, it must
    // be negated here in order for it to be set properly on the NCP.
    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_THREAD_RLOC16_DEBUG_PASSTHRU, SPINEL_DATATYPE_BOOL_S, !aEnabled);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_THREAD_RLOC16_DEBUG_PASSTHRU, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_BOOL_S, &status);
    nlREQUIRE_ACTION(parsedLength > 0 && status == aEnabled, done, retval = OT_ERROR_FAILED);

 done:
    return retval;
}

otError thciSetIcmpEchoEnabled(bool aEnable)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    bool status;
    spinel_ssize_t parsedLength;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_IPV6_ICMP_PING_OFFLOAD, SPINEL_DATATYPE_BOOL_S, aEnable);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_IPV6_ICMP_PING_OFFLOAD, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_BOOL_S, &status);
    nlREQUIRE_ACTION(parsedLength > 0 && status == aEnable, done, retval = OT_ERROR_FAILED);

 done:
    return retval;
}

otError thciGetExtendedAddress(uint8_t *aAddress)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    uint8_t *theAddress = NULL;
    spinel_ssize_t parsedLength;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);
    nlREQUIRE_ACTION(aAddress != NULL, done, retval = OT_ERROR_INVALID_ARGS);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_GET, SPINEL_PROP_MAC_15_4_LADDR, NULL);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_MAC_15_4_LADDR, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_EUI64_S, &theAddress);
    nlREQUIRE_ACTION(parsedLength > 0 && theAddress, done, retval = OT_ERROR_FAILED);

    memcpy(aAddress, theAddress, OT_EXT_ADDRESS_SIZE);

 done:
    return retval;
}

otError thciAddUnicastAddress(otNetifAddress *aAddress)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);
    nlREQUIRE_ACTION(aAddress != NULL, done, retval = OT_ERROR_INVALID_ARGS);

    NL_LOG_DEBUG(lrTHCI, "Adding IPv6 Address %s\n", ip6addr_ntoa((const ip6_addr_t*)aAddress));

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_INSERT, SPINEL_PROP_IPV6_ADDRESS_TABLE, "6CLL",
                                &(aAddress->mAddress),
                                  aAddress->mPrefixLength,
                                ((aAddress->mPreferred) ? 0xffffffff : 0),
                                ((aAddress->mValid) ? 0xfffffff : 0));
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_INSERTED, SPINEL_PROP_IPV6_ADDRESS_TABLE, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    return retval;
}

otError thciSetLegacyPrefix(const uint8_t *aLegacyPrefix, uint8_t aPrefixLength)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;

    aPrefixLength /= 8; // convert from bits to bytes.

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_NEST_LEGACY_ULA_PREFIX, SPINEL_DATATYPE_DATA_S, aLegacyPrefix, aPrefixLength);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_NEST_LEGACY_ULA_PREFIX, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    return retval;
}

otError thciAddBorderRouter(const otBorderRouterConfig *aConfig)
{
    uint8_t flags = 0;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    const static int kPreferenceOffset  = 6;
    const static int kPreferenceMask    = 3 << kPreferenceOffset;
    const static int kPreferredFlag     = 1 << 5;
    const static int kSlaacFlag         = 1 << 4;
    const static int kDhcpFlag          = 1 << 3;
    const static int kConfigureFlag     = 1 << 2;
    const static int kDefaultRouteFlag  = 1 << 1;
    const static int kOnMeshFlag        = 1 << 0;
    otError retval = OT_ERROR_INVALID_ARGS;
    bool stable;

    nlREQUIRE(aConfig != NULL, done);
    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    flags |= (aConfig->mPreference << kPreferenceOffset) & kPreferenceMask;
    flags |= (aConfig->mPreference) ? kPreferredFlag : 0;
    flags |= (aConfig->mSlaac) ? kSlaacFlag : 0;
    flags |= (aConfig->mDhcp) ? kDhcpFlag : 0;
    flags |= (aConfig->mConfigure) ? kConfigureFlag : 0;
    flags |= (aConfig->mDefaultRoute) ? kDefaultRouteFlag : 0;
    flags |= (aConfig->mOnMesh) ? kOnMeshFlag : 0;

    stable = (aConfig->mStable) ? true : false;

    retval = AllowLocalNetworkDataChange(true);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_INSERT, SPINEL_PROP_THREAD_ON_MESH_NETS, "6CbC",
                            &aConfig->mPrefix.mPrefix,
                            aConfig->mPrefix.mLength,
                            stable,
                            flags);
    nlREQUIRE(retval == OT_ERROR_NONE, lockLocal);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_INSERTED, SPINEL_PROP_THREAD_ON_MESH_NETS, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, lockLocal);

 lockLocal:
    {
        otError status = AllowLocalNetworkDataChange(false);
        retval = (retval == OT_ERROR_NONE) ? status : retval;
    }

 done:
    return retval;
}

otError thciAddExternalRoute(const otExternalRouteConfig *aConfig)
{
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    otError retval = OT_ERROR_INVALID_ARGS;
    uint8_t flags = 0;
    const static int kPreferenceOffset = 6;
    const static int kPreferenceMask = 3 << kPreferenceOffset;
    bool stable;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    stable = aConfig->mStable;
    flags = ((uint8_t)aConfig->mPreference << kPreferenceOffset) & kPreferenceMask;

    retval = AllowLocalNetworkDataChange(true);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_INSERT, SPINEL_PROP_THREAD_OFF_MESH_ROUTES, "6CbC",
                            &aConfig->mPrefix.mPrefix,
                            aConfig->mPrefix.mLength,
                            stable,
                            flags);
    nlREQUIRE(retval == OT_ERROR_NONE, lockLocal);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_INSERTED, SPINEL_PROP_THREAD_OFF_MESH_ROUTES, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, lockLocal);

 lockLocal:
    {
        otError status = AllowLocalNetworkDataChange(false);
        retval = (retval == OT_ERROR_NONE) ? status : retval;
    }

 done:
    return retval;
}

otError thciRemoveExternalRoute(const otIp6Prefix *aPrefix)
{
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    otError retval = OT_ERROR_INVALID_ARGS;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = AllowLocalNetworkDataChange(true);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    // Clear mLastStatus to avoid reading a stale value below.
    gTHCINCPContext.mLastStatus = SPINEL_STATUS_FAILURE;

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_REMOVE, SPINEL_PROP_THREAD_OFF_MESH_ROUTES, "6C",
                            &aPrefix->mPrefix,
                            aPrefix->mLength);
    nlREQUIRE(retval == OT_ERROR_NONE, lockLocal);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_REMOVED, SPINEL_PROP_THREAD_OFF_MESH_ROUTES, &argPtr, &argLen);

    // If the NCP didn't find the route that needs to be removed then it will
    // return SPINEL_STATUS_OK which is equivalent to success.
    // This can happen if the NCP goes through reset recovery.
    if (gTHCINCPContext.mLastStatus == SPINEL_STATUS_OK)
    {
        retval = OT_ERROR_NONE;
    }

lockLocal:
    {
        otError status = AllowLocalNetworkDataChange(false);
        retval = (retval == OT_ERROR_NONE) ? status : retval;
    }

 done:
    return retval;
}

otError thciActiveScan(uint32_t aScanChannels, uint16_t aScanDuration, thciHandleActiveScanResult aCallback, void *aContext)
{
    otError retval = OT_ERROR_INVALID_ARGS;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;

    nlREQUIRE(aCallback != NULL, done);
    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    gTHCINCPContext.mScanResultCallback = aCallback;
    gTHCINCPContext.mScanResultCallbackContext = aContext;
    // Currently this process involves 3 separate transactions; 1 - to set the channel mask 2 - to set the scan duration and
    // 3 - to initiate the scan.  A future version of Spinel may reduce this down to a single call.
    retval = SetScanMaskAll(aScanChannels);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_MAC_SCAN_PERIOD, SPINEL_DATATYPE_UINT16_S, aScanDuration);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_MAC_SCAN_PERIOD, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    tid = GetNewTransactionId();

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_MAC_SCAN_STATE, SPINEL_DATATYPE_UINT8_S, SPINEL_SCAN_STATE_BEACON);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_MAC_SCAN_STATE, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    return retval;
}

otError thciDiscover(uint32_t aScanChannels, bool aJoiner, bool aEnableEUI64Filtering, thciHandleActiveScanResult aCallback, void *aContext)
{
    otError retval = OT_ERROR_INVALID_ARGS;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;

    nlREQUIRE(aCallback != NULL, done);

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    gTHCINCPContext.mScanResultCallback = aCallback;
    gTHCINCPContext.mScanResultCallbackContext = aContext;

    retval = SetScanMaskAll(aScanChannels);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_THREAD_DISCOVERY_SCAN_JOINER_FLAG, SPINEL_DATATYPE_BOOL_S, aJoiner);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_THREAD_DISCOVERY_SCAN_JOINER_FLAG, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    tid = GetNewTransactionId();

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_THREAD_DISCOVERY_SCAN_ENABLE_FILTERING, SPINEL_DATATYPE_BOOL_S, aEnableEUI64Filtering);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_THREAD_DISCOVERY_SCAN_ENABLE_FILTERING, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    tid = GetNewTransactionId();

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_THREAD_DISCOVERY_SCAN_PANID, SPINEL_DATATYPE_UINT16_S, OT_PANID_BROADCAST);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_THREAD_DISCOVERY_SCAN_PANID, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    tid = GetNewTransactionId();

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_MAC_SCAN_STATE, SPINEL_DATATYPE_UINT8_S, SPINEL_SCAN_STATE_DISCOVER);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_MAC_SCAN_STATE, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    return retval;
}

otError thciSetChannel(uint16_t aChannel)
{
    otError retval = OT_ERROR_INVALID_ARGS;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    unsigned int channel = (unsigned int)aChannel;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_PHY_CHAN, SPINEL_DATATYPE_UINT_PACKED_S, channel);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_PHY_CHAN, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    return retval;
}

otError thciSetExtendedPanId(const uint8_t *aExtendedPanId)
{
    otError retval = OT_ERROR_INVALID_ARGS;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;

    nlREQUIRE(aExtendedPanId != NULL, done);
    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_NET_XPANID, SPINEL_DATATYPE_DATA_S, aExtendedPanId, sizeof(spinel_net_xpanid_t));
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_NET_XPANID, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    return retval;
}

otError thciSetPanId(otPanId aPanId)
{
    otError retval;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_MAC_15_4_PANID, SPINEL_DATATYPE_UINT16_S, aPanId);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_MAC_15_4_PANID, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    return retval;
}

otError thciSetMasterKey(const void *aKey, uint8_t aKeyLength)
{
    otError retval;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_NET_MASTER_KEY, SPINEL_DATATYPE_DATA_S, aKey, aKeyLength);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_NET_MASTER_KEY, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    return retval;
}

otError thciGetNetworkParams(thci_network_params_t *aNetworkParams)
{
    otError retval = OT_ERROR_INVALID_ARGS;
    uint16_t len;
    uint8_t modeFlags;
    spinel_ssize_t parsedLength;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    const char *name = NULL;
    uint8_t *theAddress = NULL;
    uint8_t *theExtPanId = NULL;
    unsigned int theExtPanIdLen;

    nlREQUIRE(aNetworkParams != NULL, done);

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_VENDOR_NEST_PROP_VALUE_GET, SPINEL_PROP_VENDOR_NEST_NETWORK_PARAMS, NULL);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_VENDOR_NEST_NETWORK_PARAMS, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen,
                                           SPINEL_DATATYPE_STRUCT_S(
                                               SPINEL_DATATYPE_UTF8_S   // NetworkName
                                               SPINEL_DATATYPE_EUI64_S  // extended address
                                               SPINEL_DATATYPE_DATA_S   // ext Pan Id
                                               SPINEL_DATATYPE_UINT16_S // Pan Id
                                               SPINEL_DATATYPE_UINT16_S // Short Address
                                               SPINEL_DATATYPE_UINT8_S  // Channel
                                               SPINEL_DATATYPE_UINT32_S // Partition Id
                                               SPINEL_DATATYPE_UINT8_S  // Mode Flags
                                               ),
                                           &name,
                                           &theAddress,
                                           &theExtPanId,
                                           &theExtPanIdLen,
                                           &aNetworkParams->mPanId,
                                           &aNetworkParams->mShortAddress,
                                           &aNetworkParams->mChannel,
                                           &aNetworkParams->mPartitionId,
                                           &modeFlags);
    nlREQUIRE_ACTION(parsedLength > 0 && name != NULL, done, retval = OT_ERROR_PARSE);

    memcpy(aNetworkParams->mNetworkName, name, sizeof(aNetworkParams->mNetworkName));
    memcpy(aNetworkParams->mExtAddress.m8, theAddress, sizeof(aNetworkParams->mExtAddress.m8));
    memcpy(aNetworkParams->mExtPanId, theExtPanId, sizeof(aNetworkParams->mExtPanId));

    aNetworkParams->mRole = thciGetDeviceRole();

    aNetworkParams->mMode.mRxOnWhenIdle         = (modeFlags & SPINEL_THREAD_MODE_RX_ON_WHEN_IDLE);
    aNetworkParams->mMode.mSecureDataRequests   = (modeFlags & SPINEL_THREAD_MODE_SECURE_DATA_REQUEST);
    aNetworkParams->mMode.mDeviceType           = (modeFlags & SPINEL_THREAD_MODE_FULL_FUNCTION_DEV);
    aNetworkParams->mMode.mNetworkData          = (modeFlags & SPINEL_THREAD_MODE_FULL_NETWORK_DATA);

 done:
    return retval;
}


const otNetifAddress *thciGetUnicastAddresses(void)
{
    otError status = OT_ERROR_INVALID_ARGS;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    otIp6Address *addr;
    uint8_t prefixLength;
    uint32_t preferred, valid;
    size_t i;
    spinel_ssize_t parsedLength;
    otNetifAddress *retval = NULL;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, status = OT_ERROR_INVALID_STATE);

    status = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_GET, SPINEL_PROP_IPV6_ADDRESS_TABLE, NULL);
    nlREQUIRE(status == OT_ERROR_NONE, done);

    status = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_IPV6_ADDRESS_TABLE, &argPtr, &argLen);
    nlREQUIRE(status == OT_ERROR_NONE, done);

    for (i = 0 , parsedLength = 0 ; i < THCI_CACHED_UNICAST_ADDRESS_SIZE && argLen > parsedLength ; i++)
    {
        spinel_ssize_t subLength = spinel_datatype_unpack(argPtr+parsedLength, argLen-parsedLength, "T(6CLL).",
                                              &addr,
                                              &prefixLength,
                                              &preferred,
                                              &valid);
        nlREQUIRE_ACTION(subLength > 0, done, status = OT_ERROR_PARSE);

        parsedLength += subLength;

        memset(&gTHCINCPContext.mCachedUnicastAddresses[i], 0, sizeof(otNetifAddress));

        memcpy(&gTHCINCPContext.mCachedUnicastAddresses[i].mAddress, addr, sizeof(otIp6Address));
        gTHCINCPContext.mCachedUnicastAddresses[i].mPrefixLength = prefixLength;
        gTHCINCPContext.mCachedUnicastAddresses[i].mPreferred = (preferred) ? 1 : 0;
        gTHCINCPContext.mCachedUnicastAddresses[i].mValid = (valid) ? 1 : 0;

        if (i)
        {
            gTHCINCPContext.mCachedUnicastAddresses[i-1].mNext = &gTHCINCPContext.mCachedUnicastAddresses[i];
        }
    }

    retval = &gTHCINCPContext.mCachedUnicastAddresses[0];

 done:
    return retval;
}

const otNetifMulticastAddress *thciGetMulticastAddresses(void)
{
    otError status = OT_ERROR_INVALID_ARGS;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    otIp6Address *addr;
    size_t i;
    spinel_ssize_t parsedLength;
    otNetifMulticastAddress *retval = NULL;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, status = OT_ERROR_INVALID_STATE);

    status = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_GET, SPINEL_PROP_IPV6_MULTICAST_ADDRESS_TABLE, NULL);
    nlREQUIRE(status == OT_ERROR_NONE, done);

    status = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_IPV6_MULTICAST_ADDRESS_TABLE, &argPtr, &argLen);
    nlREQUIRE(status == OT_ERROR_NONE, done);

    for (i = 0 , parsedLength = 0 ; i < THCI_CACHED_MULTICAST_ADDRESS_SIZE && argLen > parsedLength ; i++)
    {
        spinel_ssize_t subLength = spinel_datatype_unpack(argPtr+parsedLength, argLen-parsedLength, SPINEL_DATATYPE_STRUCT_S(SPINEL_DATATYPE_IPv6ADDR_S), &addr);
        nlREQUIRE_ACTION(subLength > 0, done, status = OT_ERROR_PARSE);

        parsedLength += subLength;

        memset(&gTHCINCPContext.mCachedMulticastAddresses[i], 0, sizeof(otNetifMulticastAddress));

        memcpy(&gTHCINCPContext.mCachedMulticastAddresses[i].mAddress, addr, sizeof(otIp6Address));

        if (i)
        {
            gTHCINCPContext.mCachedMulticastAddresses[i-1].mNext = &gTHCINCPContext.mCachedMulticastAddresses[i];
        }
    }

    retval = &gTHCINCPContext.mCachedMulticastAddresses[0];

 done:
    return retval;
}

otDeviceRole thciGetDeviceRole(void)
{
    // To accommodate NetworkManager some thci API's rely on cached values
    // rather than acquiring the value from the NCP. This is one such API.
    return gTHCISDKContext.mDeviceRole;
}

void thciSetLocalDeviceRole(void)
{
    // Nothing to do here as the THCI_NCP module will capture the device role when it receives
    // a Role change message from the NCP.
}

otError thciSetLinkMode(otLinkModeConfig aMode)
{
    otError retval = OT_ERROR_INVALID_ARGS;
    uint8_t modeFlags;
    const uint8_t *argPtr = NULL;
    size_t argLen;
    uint8_t tid = GetNewTransactionId();

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    modeFlags = 0;
    modeFlags |= (aMode.mRxOnWhenIdle) ?        SPINEL_THREAD_MODE_RX_ON_WHEN_IDLE : 0;
    modeFlags |= (aMode.mSecureDataRequests) ?  SPINEL_THREAD_MODE_SECURE_DATA_REQUEST : 0;
    modeFlags |= (aMode.mDeviceType) ?          SPINEL_THREAD_MODE_FULL_FUNCTION_DEV : 0;
    modeFlags |= (aMode.mNetworkData) ?         SPINEL_THREAD_MODE_FULL_NETWORK_DATA : 0;

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_THREAD_MODE, SPINEL_DATATYPE_UINT8_S, modeFlags);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_THREAD_MODE, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    return retval;
}

otError thciSetNetworkName(const char *aNetworkName)
{
    otError retval = OT_ERROR_INVALID_ARGS;
    const uint8_t *argPtr = NULL;
    size_t argLen;
    uint8_t tid = GetNewTransactionId();

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_NET_NETWORK_NAME, SPINEL_DATATYPE_UTF8_S, aNetworkName);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_NET_NETWORK_NAME, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    return retval;
}

otError thciAddUnsecurePort(uint16_t aPort)
{
    otError retval;
    const uint8_t *argPtr = NULL;
    size_t argLen;
    uint8_t tid = GetNewTransactionId();

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_INSERT, SPINEL_PROP_THREAD_ASSISTING_PORTS, SPINEL_DATATYPE_UINT16_S, aPort);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_INSERTED, SPINEL_PROP_THREAD_ASSISTING_PORTS, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    gTHCISDKContext.mSecurityFlags |= THCI_SECURITY_FLAG_INSECURE_PORTS_ENABLED;
    gTHCISDKContext.mSecurityFlags &= ~THCI_SECURITY_FLAG_SECURE_MSG_RXD_ON_INSECURE_PORT;
    gTHCISDKContext.mInsecureSourcePort = aPort;

 done:
    return retval;
}

otError thciRemoveUnsecurePort(uint16_t aPort)
{
    otError retval;
    const uint8_t *argPtr = NULL;
    size_t argLen;
    uint8_t tid = GetNewTransactionId();

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_REMOVE, SPINEL_PROP_THREAD_ASSISTING_PORTS, SPINEL_DATATYPE_UINT16_S, aPort);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_REMOVED, SPINEL_PROP_THREAD_ASSISTING_PORTS, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    gTHCISDKContext.mSecurityFlags &= ~THCI_SECURITY_FLAG_INSECURE_PORTS_ENABLED;

    // For devices that were provisionally joined the source port had to be made insecure. This code un-does that
    // operation
    if (THCI_TEST_INSECURE_SOURCE_PORT(gTHCISDKContext.mSecurityFlags))
    {
        retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_REMOVE, SPINEL_PROP_THREAD_ASSISTING_PORTS, SPINEL_DATATYPE_UINT16_S, gTHCISDKContext.mInsecureSourcePort);
        nlREQUIRE(retval == OT_ERROR_NONE, done);

        retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_REMOVED, SPINEL_PROP_THREAD_ASSISTING_PORTS, &argPtr, &argLen);
        nlREQUIRE(retval == OT_ERROR_NONE, done);

        gTHCISDKContext.mSecurityFlags &= ~THCI_SECURITY_FLAG_INSECURE_SOURCE_PORT;
    }

 done:
    return retval;
}

otError thciSetSteeringData(const uint8_t *aSteeringDataAddr)
{
    otError retval;
    const uint8_t *argPtr = NULL;
    size_t argLen;
    uint8_t tid = GetNewTransactionId();

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_THREAD_STEERING_DATA, SPINEL_DATATYPE_EUI64_S, aSteeringDataAddr);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_THREAD_STEERING_DATA, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

 done:
    return retval;
}

otError thciGetPartitionId(uint32_t *aPartitionId)
{
    spinel_ssize_t parsedLength;

    return thciGetSpinelProperty(SPINEL_PROP_NET_PARTITION_ID, SPINEL_DATATYPE_UINT32_S,
                                     aPartitionId, &parsedLength);
}

otError thciGetMacCounters(otMacCounters *aCounters)
{
    spinel_ssize_t parsedLength;
    otError retval = OT_ERROR_NONE;
    uint32_t value;
    uint32_t i;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    // This loop requires that the order of properties in kMacCounterPropertyList match the order
    // of counters found in otMacCounters.
    for (i = 0 ; i < sizeof(kMacCounterPropertyList) / sizeof(kMacCounterPropertyList[0]) ; i++)
    {
        retval = thciGetSpinelProperty(kMacCounterPropertyList[i], SPINEL_DATATYPE_UINT32_S,
                                       &value, &parsedLength);
        nlREQUIRE(retval == OT_ERROR_NONE, done);

        ((uint32_t *)aCounters)[i] = value;
    }

 done:
    return retval;
}

otError thciGetIpCounters(otIpCounters *aCounters)
{
    spinel_ssize_t parsedLength;
    otError retval = OT_ERROR_NONE;
    uint32_t value;
    uint32_t i;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    // This loop requires that the order of properties in kMacCounterPropertyList match the order
    // of counters found in otMacCounters.
    for (i = 0 ; i < sizeof(kIpCounterPropertyList) / sizeof(kIpCounterPropertyList[0]) ; i++)
    {
        retval = thciGetSpinelProperty(kIpCounterPropertyList[i], SPINEL_DATATYPE_UINT32_S,
                                       &value, &parsedLength);
        nlREQUIRE(retval == OT_ERROR_NONE, done);

        ((uint32_t *)aCounters)[i] = value;
    }

 done:
    return retval;
}


otError thciGetVersionString(char *aString, size_t aLength)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    spinel_ssize_t parsedLength;
    const char *version = NULL;
    size_t versionLength;

    nlREQUIRE_ACTION(aString != NULL && aLength != 0, done, retval = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_GET, SPINEL_PROP_NCP_VERSION, NULL);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_NCP_VERSION, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_UTF8_S, &version);
    nlREQUIRE_ACTION(parsedLength > 0 && version != NULL, done, retval = OT_ERROR_PARSE);

    versionLength = strlen(version);

    if (aLength < versionLength + 1)
    {
        versionLength = aLength - 1;
    }

    memcpy(aString, version, versionLength);
    aString[versionLength] = '\0';

 done:
    return retval;
}

void thciSetMaxTxPower(int8_t aPower)
{
    otError status;
    const uint8_t *argPtr = NULL;
    size_t argLen;
    uint8_t tid = GetNewTransactionId();

    nlREQUIRE(gTHCINCPContext.mModuleState == kModuleStateInitialized, done);

    status = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_PHY_TX_POWER, SPINEL_DATATYPE_INT8_S, aPower);
    nlREQUIRE(status == OT_ERROR_NONE, done);

    status = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_PHY_TX_POWER, &argPtr, &argLen);
    nlREQUIRE(status == OT_ERROR_NONE, done);

 done:
    return;
}

otError thciDiagnosticsCommand(const char *aCommandString)
{
    otError retval = OT_ERROR_INVALID_ARGS;
    const uint8_t *argPtr = NULL;
    size_t argLen;
    uint8_t tid = GetNewTransactionId();
    const char *result = NULL;
    spinel_ssize_t parsedLength;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_NEST_STREAM_MFG, SPINEL_DATATYPE_UTF8_S, aCommandString);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_NEST_STREAM_MFG, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_UTF8_S, &result);
    nlREQUIRE_ACTION(parsedLength > 0 && result != NULL, done, retval = OT_ERROR_PARSE);

    NL_LOG_CRIT(lrTHCI, "NCP Diagnostics output: %s\n", result);

 done:
    return retval;
}

otError thciIsNodeCommissioned(bool *aCommissioned)
{
    spinel_ssize_t parsedLength;

    return thciGetSpinelProperty(SPINEL_PROP_NET_SAVED, SPINEL_DATATYPE_BOOL_S,
                                 aCommissioned, &parsedLength);
}

#if THCI_CONFIG_LEGACY_NCP_CREDENTIAL_RECOVERY
otError thciRecoverLegacyCredentials(otError *aResult)
{
    otError retval = OT_ERROR_INVALID_ARGS;
    const uint8_t *argPtr = NULL;
    size_t argLen;
    uint8_t tid;
    spinel_ssize_t parsedLength;
    const bool enable = true;

    nlREQUIRE(aResult != NULL, done);

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);
    nlREQUIRE(aResult, done);

    tid = GetNewTransactionId();

    retval = thciUartFrameSend(tid, SPINEL_CMD_VENDOR_NEST_PROP_VALUE_SET, SPINEL_PROP_VENDOR_NEST_LEGACY_CREDENTIALS_RECOVERY, SPINEL_DATATYPE_BOOL_S, enable);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_VENDOR_NEST_LEGACY_CREDENTIALS_RECOVERY, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_UINT_PACKED_S, aResult);
    nlREQUIRE_ACTION(parsedLength > 0, done, retval = OT_ERROR_PARSE);

 done:
    return retval;
}

otError thciEraseLegacyCredentials(otError *aResult)
{
    otError retval = OT_ERROR_INVALID_ARGS;
    const uint8_t *argPtr = NULL;
    size_t argLen;
    uint8_t tid;
    spinel_ssize_t parsedLength;
    const bool enable = true;

    nlREQUIRE(aResult != NULL, done);

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);
    nlREQUIRE(aResult, done);

    tid = GetNewTransactionId();

    retval = thciUartFrameSend(tid, SPINEL_CMD_VENDOR_NEST_PROP_VALUE_SET, SPINEL_PROP_VENDOR_NEST_LEGACY_CREDENTIALS_ERASE, SPINEL_DATATYPE_BOOL_S, enable);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_VENDOR_NEST_LEGACY_CREDENTIALS_ERASE, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_UINT_PACKED_S, aResult);
    nlREQUIRE_ACTION(parsedLength > 0, done, retval = OT_ERROR_PARSE);

 done:
    return retval;
}
#endif // THCI_CONFIG_LEGACY_NCP_CREDENTIAL_RECOVERY

static otError HostWakeSleep(spinel_host_power_state_t aPowerState)
{
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    spinel_ssize_t parsedLength;
    otError retval = OT_ERROR_INVALID_ARGS;
    uint8_t status;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_HOST_POWER_STATE, SPINEL_DATATYPE_UINT8_S, aPowerState);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_HOST_POWER_STATE, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_UINT8_S, &status);
    nlREQUIRE_ACTION(parsedLength > 0, done, retval = OT_ERROR_PARSE);
    nlREQUIRE_ACTION(status == aPowerState, done, retval = OT_ERROR_FAILED);

 done:
    return retval;
} 

otError thciHostSleep(void)
{
    otError retval;
    bool status;

    do
    {
        retval = HostWakeSleep(SPINEL_HOST_POWER_STATE_LOW_POWER);
        nlREQUIRE(retval == OT_ERROR_NONE, done);

        // Try to disable the uart for sleep.  thciUartSleepDisable will
        // only succeed if all Rx bytes have been processed.  Otherwise,
        // HostWakeSleep must be called again to process the bytes.
        status = thciUartSleepDisable();
        
    } while (status == false);

    gTHCINCPContext.mModuleState = kModuleStateHostSleep;

 done:
    return retval;
}

otError thciHostWake(void)
{
    otError retval = OT_ERROR_NONE;
    // TODO: Send the command to disable packet filtering for sleep. This will also 
    // disable the sleep state in the NCP as any frame from the host will cause
    // the NCP to treat the host as awake/online.

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateHostSleep, done, retval = OT_ERROR_INVALID_STATE);

    thciUartSleepEnable();
    gTHCINCPContext.mModuleState = kModuleStateInitialized;

 done:
    return retval;
}

void thciInitiateNCPRecovery(void)
{
    nlREQUIRE(gTHCINCPContext.mModuleState != kModuleStateResetRecovery, done);

    gTHCINCPContext.mModuleState = kModuleStateResetRecovery;

    if (gTHCISDKContext.mInitParams.mSdkQueue)
    {
        nl_eventqueue_post_event(gTHCISDKContext.mInitParams.mSdkQueue, &sNCPRecoveryEvent);
    }

 done:
    return;
}

otError thciGetLeaderWeight(uint8_t *aWeight)
{
    spinel_ssize_t len;
    return thciGetSpinelProperty(SPINEL_PROP_THREAD_LEADER_WEIGHT,
                             SPINEL_DATATYPE_UINT8_S,
                             aWeight,
                             &len);
}

otError thciGetLocalLeaderWeight(uint8_t *aWeight)
{
    spinel_ssize_t len;
    return thciGetSpinelProperty(SPINEL_PROP_THREAD_LOCAL_LEADER_WEIGHT,
                             SPINEL_DATATYPE_UINT8_S,
                             aWeight,
                             &len);
}

otError thciGetNetworkDataVersion(uint8_t *aVersion)
{
    spinel_ssize_t len;
    return thciGetSpinelProperty(SPINEL_PROP_THREAD_NETWORK_DATA_VERSION,
                             SPINEL_DATATYPE_UINT8_S,
                             aVersion,
                             &len);
}

otError thciGetStableNetworkDataVersion(uint8_t *aVersion)
{
    spinel_ssize_t len;
    return thciGetSpinelProperty(SPINEL_PROP_THREAD_STABLE_NETWORK_DATA_VERSION,
                                 SPINEL_DATATYPE_UINT8_S,
                                 aVersion,
                                 &len);
}

otError thciGetPreferredRouterId(uint8_t *aRouterId)
{
    spinel_ssize_t len;
    return thciGetSpinelProperty(SPINEL_PROP_THREAD_PREFERRED_ROUTER_ID,
                                 SPINEL_DATATYPE_UINT8_S,
                                 aRouterId,
                                 &len);
}

otError thciGetLeaderAddress(otIp6Address *aLeaderAddr)
{
    otError retval;
    spinel_ssize_t len;
    spinel_ipv6addr_t *addrPtr = NULL;

    nlREQUIRE_ACTION(aLeaderAddr != NULL, done, retval = OT_ERROR_INVALID_ARGS);

    retval = thciGetSpinelProperty(SPINEL_PROP_THREAD_LEADER_ADDR,
                                   SPINEL_DATATYPE_IPv6ADDR_S,
                                   &addrPtr,
                                   &len);

    nlREQUIRE(retval == OT_ERROR_NONE, done);
    nlREQUIRE_ACTION(addrPtr != NULL, done, retval = OT_ERROR_PARSE);

    memcpy(aLeaderAddr->mFields.m8, addrPtr, sizeof(spinel_ipv6addr_t));

done:
    return retval;
}

otError thciGetRloc16(uint16_t *aRloc)
{
    spinel_ssize_t len;
    return thciGetSpinelProperty(SPINEL_PROP_THREAD_RLOC16,
                                 SPINEL_DATATYPE_UINT16_S,
                                 aRloc,
                                 &len);
}

otError thciGetInstantRssi(int8_t *aRssi)
{
    spinel_ssize_t len;
    return thciGetSpinelProperty(SPINEL_PROP_PHY_RSSI,
                                 SPINEL_DATATYPE_INT8_S,
                                 aRssi,
                                 &len);
}

otError thciGetLeaderRouterId(uint8_t *aLeaderId)
{
    spinel_ssize_t len;
    return thciGetSpinelProperty(SPINEL_PROP_THREAD_LEADER_RID,
                                 SPINEL_DATATYPE_UINT8_S,
                                 aLeaderId,
                                 &len);

}

otError thciGetNetworkData(uint8_t *aNetworkData, uint16_t aInSize, uint16_t *aOutSize)
{
    return thciGetSpinelDataProperty(SPINEL_PROP_THREAD_NETWORK_DATA, SPINEL_DATATYPE_DATA_S,
                                     aNetworkData, aInSize, aOutSize);
}

otError thciGetStableNetworkData(uint8_t *aNetworkData, uint16_t aInSize, uint16_t *aOutSize)
{
    return thciGetSpinelDataProperty(SPINEL_PROP_THREAD_STABLE_NETWORK_DATA, SPINEL_DATATYPE_DATA_S,
                                     aNetworkData, aInSize, aOutSize);
}

otError thciGetCombinedNeighborTable(thci_neighbor_child_info_t *aTableHead, uint32_t aInSize, uint32_t *aOutSize)
{
    otError        retval;
    uint8_t        tid = GetNewTransactionId();

    const uint8_t *argPtr = NULL;
    size_t         argLen;

    spinel_ssize_t parsedLen;
    unsigned int   neighborLen = 0;

    uint32_t       numIsChild = 0;

    nlREQUIRE_ACTION(aTableHead                   != NULL                   , done, retval = OT_ERROR_INVALID_ARGS );
    nlREQUIRE_ACTION(aInSize                      != 0                      , done, retval = OT_ERROR_INVALID_ARGS );
    nlREQUIRE_ACTION(aOutSize                     != NULL                   , done, retval = OT_ERROR_INVALID_ARGS );
    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    // Get the neighbour table
    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_GET, SPINEL_PROP_THREAD_NEIGHBOR_TABLE, NULL);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_THREAD_NEIGHBOR_TABLE, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    *aOutSize = 0;

    while (argLen > 0 && neighborLen < aInSize)
    {
        otNeighborInfo *currNeighbor = &aTableHead[neighborLen].mNeighborInfo;

        // The EUI-64 is unpacked as a pointer instead of a value.
        uint64_t       *eui64;
        uint8_t         modeFlags;
        bool            isChild;

        parsedLen = spinel_datatype_unpack(argPtr, argLen,
                                           SPINEL_DATATYPE_STRUCT_S(
                                               SPINEL_DATATYPE_EUI64_S   // EUI64 Address
                                               SPINEL_DATATYPE_UINT16_S  // Rloc16
                                               SPINEL_DATATYPE_UINT32_S  // Age
                                               SPINEL_DATATYPE_UINT8_S   // Link Quality In
                                               SPINEL_DATATYPE_INT8_S    // Average RSS
                                               SPINEL_DATATYPE_UINT8_S   // Mode (flags)
                                               SPINEL_DATATYPE_BOOL_S    // Is Child
                                               SPINEL_DATATYPE_UINT32_S  // Link Frame Counter
                                               SPINEL_DATATYPE_UINT32_S  // MLE Frame Counter
                                               SPINEL_DATATYPE_INT8_S    // Most recent RSS
                                               ),
                                           &eui64,
                                           &currNeighbor->mRloc16,
                                           &currNeighbor->mAge,
                                           &currNeighbor->mLinkQualityIn,
                                           &currNeighbor->mAverageRssi,
                                           &modeFlags,
                                           &isChild,
                                           &currNeighbor->mLinkFrameCounter,
                                           &currNeighbor->mMleFrameCounter,
                                           &currNeighbor->mLastRssi);

        nlREQUIRE_ACTION(parsedLen > 0, done, retval = OT_ERROR_PARSE);

        currNeighbor->mIsChild = isChild;

        currNeighbor->mRxOnWhenIdle      = (modeFlags & SPINEL_THREAD_MODE_RX_ON_WHEN_IDLE    ) ? true : false;
        currNeighbor->mSecureDataRequest = (modeFlags & SPINEL_THREAD_MODE_SECURE_DATA_REQUEST) ? true : false;
        currNeighbor->mFullFunction      = (modeFlags & SPINEL_THREAD_MODE_FULL_FUNCTION_DEV  ) ? true : false;
        currNeighbor->mFullNetworkData   = (modeFlags & SPINEL_THREAD_MODE_FULL_NETWORK_DATA  ) ? true : false;

        memcpy(currNeighbor->mExtAddress.m8, eui64, sizeof(uint64_t));

        if (isChild)
        {
            numIsChild++;
        }

        aTableHead[neighborLen].mFoundChild = false;

        argPtr += parsedLen;
        argLen -= parsedLen;
        neighborLen++;
    }

    // Now get the child info
    tid = GetNewTransactionId();

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_GET, SPINEL_PROP_THREAD_CHILD_TABLE, NULL);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_THREAD_CHILD_TABLE, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    // For each child, find the neighbour associated with it.
    // This prevents us from having to actualy store both tables in full at the same time.
    while (argLen > 0)
    {
        uint64_t                   *eui64;
        uint8_t                     modeFlags;

        otChildInfo                 tempChild;
        thci_neighbor_child_info_t *entry = NULL;

        parsedLen = spinel_datatype_unpack(argPtr, argLen,
                                           SPINEL_DATATYPE_STRUCT_S(
                                               SPINEL_DATATYPE_EUI64_S   // EUI64 Address
                                               SPINEL_DATATYPE_UINT16_S  // Rloc16
                                               SPINEL_DATATYPE_UINT32_S  // Timeout
                                               SPINEL_DATATYPE_UINT32_S  // Age
                                               SPINEL_DATATYPE_UINT8_S   // Network Data Version
                                               SPINEL_DATATYPE_UINT8_S   // Link Quality In
                                               SPINEL_DATATYPE_INT8_S    // Average RSS
                                               SPINEL_DATATYPE_UINT8_S   // Mode (flags)
                                               SPINEL_DATATYPE_INT8_S    // Most recent RSS
                                               ),
                                           &eui64,
                                           &tempChild.mRloc16,
                                           &tempChild.mTimeout,
                                           &tempChild.mAge,
                                           &tempChild.mNetworkDataVersion,
                                           &tempChild.mLinkQualityIn,
                                           &tempChild.mAverageRssi,
                                           &modeFlags,
                                           &tempChild.mLastRssi);

        // Now find the neighbour entry
        for (uint32_t i = 0; i < neighborLen; i++)
        {
            if (aTableHead[i].mNeighborInfo.mRloc16 == tempChild.mRloc16)
            {
                entry = &aTableHead[i];
                break;
            }
        }

        if (entry != NULL)
        {
            nlREQUIRE_ACTION(entry->mNeighborInfo.mIsChild == true, done, retval = OT_ERROR_FAILED);

            // Populate the child info
            entry->mTimeout            = tempChild.mTimeout;
            entry->mChildId            = tempChild.mChildId;
            entry->mNetworkDataVersion = tempChild.mNetworkDataVersion;

            entry->mFoundChild = true;

            numIsChild--;
        }

        argPtr += parsedLen;
        argLen -= parsedLen;
    }

    // Is there a neighbour that said its a child, but no child for it?
    {
        // Need to find the offenders
        uint32_t i = 0;
        while (numIsChild > 0 && i < neighborLen)
        {
            if (aTableHead[i].mNeighborInfo.mIsChild == true &&
                aTableHead[i].mFoundChild            == false)
            {
                // This entry needs to be purged
                aTableHead[i] = aTableHead[neighborLen - 1];
                neighborLen--;
                numIsChild--;
            }
            else
            {
                i++;
            }
        }
    }

    *aOutSize = neighborLen;

done:
    if (retval != OT_ERROR_NONE)
    {
        NL_LOG_CRIT(lrTHCI, "Error getting Neighbor/Child table\n");
        *aOutSize = 0;
    }

    return retval;

}

otError thciGetChildTable(otChildInfo *aChildTableHead, uint32_t aInSize, uint32_t *aOutSize)
{
    spinel_ssize_t parsedLength;
    otError retval;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;

    nlREQUIRE_ACTION(aChildTableHead != NULL, done, retval = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(aInSize         != 0,    done, retval = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(aOutSize        != NULL, done, retval = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_GET, SPINEL_PROP_THREAD_CHILD_TABLE, NULL);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_THREAD_CHILD_TABLE, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    *aOutSize = 0;

    while (argLen > 0 && *aOutSize < aInSize)
    {
        // The EUI-64 is unpacked as a pointer instead of a value.
        uint64_t    *eui64;

        otChildInfo *currChild = &aChildTableHead[*aOutSize];
        uint8_t      modeFlags;

        parsedLength = spinel_datatype_unpack(argPtr, argLen,
                                           SPINEL_DATATYPE_STRUCT_S(
                                               SPINEL_DATATYPE_EUI64_S   // EUI64 Address
                                               SPINEL_DATATYPE_UINT16_S  // Rloc16
                                               SPINEL_DATATYPE_UINT32_S  // Timeout
                                               SPINEL_DATATYPE_UINT32_S  // Age
                                               SPINEL_DATATYPE_UINT8_S   // Network Data Version
                                               SPINEL_DATATYPE_UINT8_S   // Link Quality In
                                               SPINEL_DATATYPE_INT8_S    // Average RSS
                                               SPINEL_DATATYPE_UINT8_S   // Mode (flags)
                                               SPINEL_DATATYPE_INT8_S    // Most recent RSS
                                               ),
                                           &eui64,
                                           &currChild->mRloc16,
                                           &currChild->mTimeout,
                                           &currChild->mAge,
                                           &currChild->mNetworkDataVersion,
                                           &currChild->mLinkQualityIn,
                                           &currChild->mAverageRssi,
                                           &modeFlags,
                                           &currChild->mLastRssi);

        nlREQUIRE_ACTION(parsedLength > 0, done, retval = OT_ERROR_PARSE);

        currChild->mRxOnWhenIdle      = (modeFlags & SPINEL_THREAD_MODE_RX_ON_WHEN_IDLE    ) ? true : false;
        currChild->mSecureDataRequest = (modeFlags & SPINEL_THREAD_MODE_SECURE_DATA_REQUEST) ? true : false;
        currChild->mFullFunction      = (modeFlags & SPINEL_THREAD_MODE_FULL_FUNCTION_DEV  ) ? true : false;
        currChild->mFullNetworkData   = (modeFlags & SPINEL_THREAD_MODE_FULL_NETWORK_DATA  ) ? true : false;

        memcpy(currChild->mExtAddress.m8, eui64, sizeof(uint64_t));

        argPtr += parsedLength;
        argLen -= parsedLength;
        (*aOutSize)++;
    }

done:
    if (retval != OT_ERROR_NONE)
    {
        NL_LOG_CRIT(lrTHCI, "Error getting child table\n");
        *aOutSize = 0;
    }

    return retval;
}

otError thciGetNeighborTable(otNeighborInfo *aNeighborTableHead, uint32_t aInSize, uint32_t *aOutSize)
{
    otError retval;
    uint8_t tid = GetNewTransactionId();

    const uint8_t *argPtr = NULL;
    size_t argLen;

    spinel_ssize_t parsedLen;

    nlREQUIRE_ACTION(aNeighborTableHead != NULL, done, retval = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(aInSize            != 0,    done, retval = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(aOutSize           != NULL, done, retval = OT_ERROR_INVALID_ARGS);
    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_PROP_VALUE_GET, SPINEL_PROP_THREAD_NEIGHBOR_TABLE, NULL);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_THREAD_NEIGHBOR_TABLE, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    *aOutSize = 0;

    while (argLen > 0 && *aOutSize < aInSize)
    {
        // The EUI-64 is unpacked as a pointer instead of a value.
        uint64_t       *eui64;

        otNeighborInfo *currNeighbor = &aNeighborTableHead[*aOutSize];
        uint8_t         modeFlags;
        bool            isChild;

        parsedLen = spinel_datatype_unpack(argPtr, argLen,
                                           SPINEL_DATATYPE_STRUCT_S(
                                               SPINEL_DATATYPE_EUI64_S   // EUI64 Address
                                               SPINEL_DATATYPE_UINT16_S  // Rloc16
                                               SPINEL_DATATYPE_UINT32_S  // Age
                                               SPINEL_DATATYPE_UINT8_S   // Link Quality In
                                               SPINEL_DATATYPE_INT8_S    // Average RSS
                                               SPINEL_DATATYPE_UINT8_S   // Mode (flags)
                                               SPINEL_DATATYPE_BOOL_S    // Is Child
                                               SPINEL_DATATYPE_UINT32_S  // Link Frame Counter
                                               SPINEL_DATATYPE_UINT32_S  // MLE Frame Counter
                                               SPINEL_DATATYPE_INT8_S    // Most recent RSS
                                               ),
                                           &eui64,
                                           &currNeighbor->mRloc16,
                                           &currNeighbor->mAge,
                                           &currNeighbor->mLinkQualityIn,
                                           &currNeighbor->mAverageRssi,
                                           &modeFlags,
                                           &isChild,
                                           &currNeighbor->mLinkFrameCounter,
                                           &currNeighbor->mMleFrameCounter,
                                           &currNeighbor->mLastRssi);

        nlREQUIRE_ACTION(parsedLen > 0, done, retval = OT_ERROR_PARSE);

        currNeighbor->mIsChild = isChild;

        currNeighbor->mRxOnWhenIdle      = (modeFlags & SPINEL_THREAD_MODE_RX_ON_WHEN_IDLE    ) ? true : false;
        currNeighbor->mSecureDataRequest = (modeFlags & SPINEL_THREAD_MODE_SECURE_DATA_REQUEST) ? true : false;
        currNeighbor->mFullFunction      = (modeFlags & SPINEL_THREAD_MODE_FULL_FUNCTION_DEV  ) ? true : false;
        currNeighbor->mFullNetworkData   = (modeFlags & SPINEL_THREAD_MODE_FULL_NETWORK_DATA  ) ? true : false;

        memcpy(currNeighbor->mExtAddress.m8, eui64, sizeof(uint64_t));

        argPtr += parsedLen;
        argLen -= parsedLen;
        (*aOutSize)++;
    }

done:
    if (retval != OT_ERROR_NONE)
    {
        NL_LOG_CRIT(lrTHCI, "Error getting neighbor table\n");
        *aOutSize = 0;
    }

    return retval;
}


void thciStallOutgoingDataPackets(bool aEnable)
{
    if (gTHCISDKContext.mStallOutgoingDataPackets != aEnable)
    {
        gTHCISDKContext.mStallOutgoingDataPackets = aEnable;

        if (!gTHCISDKContext.mStallOutgoingDataPackets && !IsMessageQueueEmpty())
        {
            // post an event to restart the flow of outgoing packets.

            // Race conditions can exist between the LWIP task in LwIPOutputIP6 and the THCI task 
            // here which can result in multiple sOutgoingIPPacketEvents posted to the event queue.
            // This __sync_fetch_and_or ensures that only one sOutgoingIPPacketEvent is ever posted to the queue.
            if (!__sync_fetch_and_or(&sOutgoingIPPacketEventPosted, 1))
            {
                nl_eventqueue_post_event(gTHCISDKContext.mInitParams.mSdkQueue, &sOutgoingIPPacketEvent);
            }
        }
    }
}

#if THCI_CONFIG_LEGACY_ALARM_SUPPORT
otError thciSetLegacyNetworkWake(bool aEnable, uint8_t aReason)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    bool status;
    spinel_ssize_t parsedLength;
    // The upper bit is used to store the enable bool while the lower byte is used 
    // to store the reason code.
    uint16_t value = (aEnable)? ((0x80 << 8) | aReason) : 0;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_VENDOR_NEST_PROP_VALUE_SET, SPINEL_PROP_VENDOR_NEST_NETWORK_WAKE_CTRL, SPINEL_DATATYPE_UINT16_S, value);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_VENDOR_NEST_NETWORK_WAKE_CTRL, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_UINT16_S, &status);
    nlREQUIRE_ACTION(parsedLength > 0 && status == value, done, retval = OT_ERROR_FAILED);

 done:
    return retval;
}

otError thciSetLegacyNetworkLurk(bool aEnable)
{
    otError retval = OT_ERROR_NONE;
    uint8_t tid = GetNewTransactionId();
    const uint8_t *argPtr = NULL;
    size_t argLen;
    bool status;
    spinel_ssize_t parsedLength;

    nlREQUIRE_ACTION(gTHCINCPContext.mModuleState == kModuleStateInitialized, done, retval = OT_ERROR_INVALID_STATE);

    retval = thciUartFrameSend(tid, SPINEL_CMD_VENDOR_NEST_PROP_VALUE_SET, SPINEL_PROP_VENDOR_NEST_NETWORK_LURK_CTRL, SPINEL_DATATYPE_BOOL_S, aEnable);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    retval = thciUartWaitForResponse(tid, SPINEL_CMD_PROP_VALUE_IS, SPINEL_PROP_VENDOR_NEST_NETWORK_LURK_CTRL, &argPtr, &argLen);
    nlREQUIRE(retval == OT_ERROR_NONE, done);

    parsedLength = spinel_datatype_unpack(argPtr, argLen, SPINEL_DATATYPE_BOOL_S, &status);
    nlREQUIRE_ACTION(parsedLength > 0 && status == aEnable, done, retval = OT_ERROR_FAILED);

 done:
    return retval;
}

#endif // THCI_CONFIG_LEGACY_ALARM_SUPPORT

otError thciSendMacDataRequest(void)
{
    return UnimplementedAPI(__FUNCTION__);
}

void thciSetPollPeriod(uint32_t aPollPeriod)
{
    UnimplementedAPI(__FUNCTION__);
}

otError thciLinkAddWhitelist(const uint8_t *aExtAddr)
{
    return UnimplementedAPI(__FUNCTION__);
}

void thciLinkClearWhitelist(void)
{
    UnimplementedAPI(__FUNCTION__);
}

void thciLinkSetWhitelistEnabled(bool aEnabled)
{
    UnimplementedAPI(__FUNCTION__);
}

//NOT IMPLEMENTED - Not currently used
const char * thciGetNetworkName(void)
{
    UnimplementedAPI(__FUNCTION__);

    return NULL;
}

//NOT IMPLEMENTED - Not currently used
const uint8_t * thciGetMasterKey(uint8_t *aKeyLength)
{
    UnimplementedAPI(__FUNCTION__);

    return NULL;
}

//NOT IMPLEMENTED - Not currently used
otError thciGetChannel(uint8_t *aChannel)
{
    return UnimplementedAPI(__FUNCTION__);
}

//NOT IMPLEMENTED - Not currently used
otError thciGetParentAverageRssi(int8_t *aParentRssi)
{
    return UnimplementedAPI(__FUNCTION__);
}

//NOT IMPLEMENTED - Not currently used
otError thciGetParentLastRssi(int8_t *aLastRssi)
{
    return UnimplementedAPI(__FUNCTION__);
}

#endif /* THCI_CONFIG_USE_OPENTHREAD_ON_NCP */
