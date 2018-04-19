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
 *      This file implements NCP UART version of Thread Host Control Interface.
 *
 */

#include <thci_config.h>

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <nlalignment.h>
#include <nlnew.hpp>
#include <nlassert.h>
#include <nlerlog.h>
#include <nlerevent.h>
#include <nlereventqueue.h>
#include <nlererror.h>
#include <nlertask.h>
#include <nlplatform.h>
#include <nlplatform/nltime.h>
#include <nlertime.h>
// pumice uses older console API's rather than nlplatform/nluart.h
extern "C" {
#include <nlconsole.h>
#include <nlproduct.h>
#include <nluart.h>
} // extern "C"

/* OpenThread library includes */
#include <openthread/types.h>
#include <openthread/hdlc.hpp>
#include <openthread/spinel.h>

/* thci includes */
#include <thci.h>
#include <thci_module.h>
#include <thci_module_ncp.h>
#include <thci_module_ncp_uart.h>

/**
 * SECTION - Definitions
 */

#define UART_TX_BUFFER_SIZE                 (128)
#define UART_RX_BUFFER_SIZE                 (1500)
#define UART_FRAME_BUFFER_SIZE              (1500)
#define RX_UART_FIFO_SIZE                   (128)
#define RX_UART_FIFO_NEAR_FULL_THRESHOLD    (RX_UART_FIFO_SIZE / 10)
#define MAX_NCP_APP_RESPONSE_TIME_MSEC      (3000)
#define MAX_NCP_PUTCHAR_TIME                (3000)

#if defined(BUILD_PRODUCT_ANTIGUA) || defined(BUILD_PRODUCT_T2)
#define THCI_UART_ID UART0
#else
#error THCI_UART_ID has not been defined for this product!
#endif

class UartTxBuffer : public ot::Hdlc::Encoder::BufferWriteIterator
{
public:
    UartTxBuffer(void);

    void            Clear(void);
    bool            IsEmpty(void) const;
    uint16_t        GetLength(void) const;
    const uint8_t   *GetBuffer(void) const;    

private:
    uint8_t         mBuffer[UART_TX_BUFFER_SIZE];
};

/**
 * SECTION - Prototypes
 */

static void UartRxReadyIsr(void *aContext);
static int UartRxDoneEventHandler(nl_event_t *aEvent, void *aClosure);
extern "C" void HandleLastStatusUpdate(const uint8_t *aArgPtr, unsigned int aArgLen);

/**
 * SECTION - Globals
 */

// THCI context that is common between the NCP and SOC solutions.
extern thci_sdk_context_t gTHCISDKContext; 
extern "C" const uint8_t kDontCareTransactionId;

static volatile bool                    sProvideInternalResponse;
static volatile uint8_t                 sRxEventPostedToResponseQueue;
static volatile uint8_t                 sRxEventPostedToSdkQueue;
static volatile bool                    sRxIsrDisabled;
static uint16_t                         sFrameByteCount;
static ot::Hdlc::Decoder                *sFrameDecoder;
static uint8_t                          sRxUartFifo[RX_UART_FIFO_SIZE];
static uint16_t                         sRxUartFifoHead;
static uint16_t                         sRxUartFifoTail;
static uint8_t                          sRxBuffer[UART_RX_BUFFER_SIZE];
static uint8_t                          sTxBuffer[UART_FRAME_BUFFER_SIZE];
static nl_eventqueue_t                  sResponseQueueHandle;
static nl_eventqueue_t                  *sResponseQueue[1];
static uint8_t                          sResponseCommand;
static spinel_prop_key_t                sResponseKey;
static const uint8_t                    *sResponseBuffer;
static size_t                           sResponseLength;
static bool                             sResponseReceived;
static uint8_t                          sResponseTransactionId;
static bool                             sResponseSuccess;
static bool                             sDecodeFailure;
static const nl_console_t               *sUartConsole;
static thciUartDataFrameCallback_t      sDataFrameCB;
static thciUartControlFrameCallback_t   sControlFrameCB;
static nl_time_ms_t                     (*sGetMillisecondTimeFunc)(void) = NULL;

static DEFINE_ALIGNED_VAR(sFrameDecoderBuffer, sizeof(ot::Hdlc::Decoder), uint64_t);

const nl_event_t sUartRxDoneEvent =
{
    NL_INIT_EVENT_STATIC(((nl_event_type_t)NL_EVENT_T_RUNTIME),
        UartRxDoneEventHandler, NULL)
};

/**
 * SECTION - Implementation
 */

UartTxBuffer::UartTxBuffer(void)
    : ot::Hdlc::Encoder::BufferWriteIterator()
{
    Clear();
}

void UartTxBuffer::Clear(void)
{
    mWritePointer = mBuffer;
    mRemainingLength = sizeof(mBuffer);
}

bool UartTxBuffer::IsEmpty(void) const
{
    return mWritePointer == mBuffer;
}

uint16_t UartTxBuffer::GetLength(void) const
{
    return static_cast<uint16_t>(mWritePointer - mBuffer);
}

const uint8_t *UartTxBuffer::GetBuffer(void) const
{
    return mBuffer;
}

static void DelayMs(nl_time_ms_t aDelay)
{
    NL_CPU_spinWaitUs(1000 * aDelay);
}

static void RxISREnable(bool aForce)
{
    if (aForce || sRxIsrDisabled)
    {
        sRxIsrDisabled = false;
        uart_enable_rie(THCI_UART_ID, true);
    }
}

static void RxISRDisable(void)
{
    if (!sRxIsrDisabled)
    {
        sRxIsrDisabled = true;
        uart_enable_rie(THCI_UART_ID, false);
    }
}

/**
 * Used to get the current time in Milliseconds. 
 * WARNING: Do not call this function directly. Instead 
 * call it through the function pointer sGetMillisecondTimeFunc.
 * Otherwise AUPD will fail as it does not support nl_get_time_native.
 */
static nl_time_ms_t GetMillisecondTime(void)
{
    return (nl_time_ms_t)nltime_get_system_ms();
}

/**
 * Used when nl_get_time_native is not available as in AUPD.
 */
static nl_time_ms_t GetNoTime(void)
{
    return 0;
}

static int GetRxFifoChar(uint8_t *aByte)
{
    int retval = 0;

    nlREQUIRE_ACTION(sRxUartFifoTail != sRxUartFifoHead, done, retval = -ENODATA);

    *aByte = sRxUartFifo[sRxUartFifoTail];

    sRxUartFifoTail = (sRxUartFifoTail < RX_UART_FIFO_SIZE - 1) ? sRxUartFifoTail + 1 : 0;

 done:
    return retval;
}

static int PutRxFifoChar(uint8_t aByte)
{
    int retval = 0;
    size_t newHead = (sRxUartFifoHead < RX_UART_FIFO_SIZE - 1) ? sRxUartFifoHead + 1 : 0;

    nlREQUIRE_ACTION(newHead != sRxUartFifoTail, done, retval = -EOVERFLOW);

    sRxUartFifo[sRxUartFifoHead] = aByte;

    sRxUartFifoHead = newHead;

 done:
    return retval;    
}

static bool IsRxFifoNearFull(size_t aThreshold)
{
    size_t newHead = (sRxUartFifoHead < RX_UART_FIFO_SIZE - aThreshold) ? 
                    sRxUartFifoHead + aThreshold : 
                    aThreshold - (RX_UART_FIFO_SIZE - sRxUartFifoHead);
    bool retval = true;

    if (sRxUartFifoHead > sRxUartFifoTail)
    {
        if (newHead > sRxUartFifoHead || 
            newHead < sRxUartFifoTail)
        {
            retval = false;
        }
    }
    else
    {
        if (newHead < sRxUartFifoTail && 
            newHead > sRxUartFifoHead)
        {
            retval = false;
        }
    }

    return retval;
}

static bool IsRxFifoEmpty(void)
{
    return (sRxUartFifoTail == sRxUartFifoHead);
}

/**
 * Process received bytes stored in the FIFO.
 */
static void UartRxFifoProcess(void)
{
    uint8_t ch;
    // The loop must terminate if the desired response is received. The logic 
    // cannot be allowed to continue reading bytes from the fifo as it is possible 
    // to corrupt the response.
    while (!sDecodeFailure && 
           !sResponseReceived && 
           !GetRxFifoChar(&ch))
    {
        sFrameByteCount++;
        sFrameDecoder->Decode(&ch, sizeof(uint8_t));

        // If the RX ISR is disabled and the Fifo has been sufficiently drained, 
        // then re-enable the ISR.
        if (sRxIsrDisabled && !IsRxFifoNearFull(2*RX_UART_FIFO_NEAR_FULL_THRESHOLD))
        {
            const bool force = true;
            RxISREnable(!force);
        }
    }
}

/**
 * Sends bytes stored in aUartBuffer to UART.
 */
static otError PutChars(UartTxBuffer *aUartBuffer)
{
    uint16_t length;
    otError retval = OT_ERROR_NONE;

    length = aUartBuffer->GetLength();

    if (length)
    {
        const uint8_t *buffer = aUartBuffer->GetBuffer();
        uint16_t put = 0;
        nl_time_ms_t timeStamp = sGetMillisecondTimeFunc();

        while (length > put)
        {
            nlREQUIRE_ACTION(sGetMillisecondTimeFunc() - timeStamp < MAX_NCP_PUTCHAR_TIME, done, retval = OT_ERROR_BUSY);

            if (nl_console_canput(sUartConsole))
            {
                nl_console_putchar(sUartConsole, (char) (buffer[put++]));
                timeStamp = sGetMillisecondTimeFunc();
            }
            else if (sRxIsrDisabled)
            {
                // If nl_console_canput fails and sRxIsrDisabled is true then it suggests that the 
                // NCP is blocked trying to send UART bytes to the host. To avoid a deadlock, drain
                // the rx fifo by calling UartRxFifoProcess.
                UartRxFifoProcess();
            }
        }
    }

 done:
    if (retval != OT_ERROR_NONE)
    {
        NL_LOG_CRIT(lrTHCI, "%s: Failed with err (%d) %d\n", __FUNCTION__, retval, sRxIsrDisabled);
    }

    aUartBuffer->Clear();

    return retval;
}

/**
 * Encodes Frame using HDLC FrameEncoder and calls PutChars to send result out UART.
 */
static otError UartSendFrame(const uint8_t *aTxFrame, const uint16_t aTxFrameLen)
{
    uint8_t byte;
    ot::Hdlc::Encoder frameEncoder;
    otError retval;
    UartTxBuffer uartTxBuffer;
    uint16_t txFramePos = 0;
    const char *step = NULL;

    uartTxBuffer.Clear();
    
    retval = frameEncoder.Init(uartTxBuffer);
    nlREQUIRE_ACTION(retval == OT_ERROR_NONE, exit, step = "Init");

    byte = aTxFrame[txFramePos++];

    while (aTxFrameLen >= txFramePos)
    {
        retval = frameEncoder.Encode(byte, uartTxBuffer);
        nlREQUIRE_ACTION(retval == OT_ERROR_NONE || retval == OT_ERROR_NO_BUFS, exit, step = "Encode");

        if (retval == OT_ERROR_NO_BUFS)
        {
            retval = PutChars(&uartTxBuffer);
            nlREQUIRE_ACTION(retval == OT_ERROR_NONE, exit, step = "PutChars1");

            continue;
        }

        byte = aTxFrame[txFramePos++];
    }

    retval = frameEncoder.Finalize(uartTxBuffer);
    nlREQUIRE_ACTION(retval == OT_ERROR_NONE || retval == OT_ERROR_NO_BUFS, exit, step = "Finalize1");

    if (retval == OT_ERROR_NO_BUFS)
    {
        retval = PutChars(&uartTxBuffer);
        nlREQUIRE_ACTION(retval == OT_ERROR_NONE, exit, step = "PutChars2");

        retval = frameEncoder.Finalize(uartTxBuffer);
        nlREQUIRE_ACTION(retval == OT_ERROR_NONE, exit, step = "Finalize2");
    }

    retval = PutChars(&uartTxBuffer);

 exit:
    if (retval != OT_ERROR_NONE)
    {
        NL_LOG_CRIT(lrTHCI, "%s: Failed %s %d\n", __FUNCTION__, step, retval);
    }

    return retval;
}

/**
 * Tries to post an event to the sdk queue
 *
 * @param[in] aFromISR True if the caller is in ISR context, false otherwise.
 *
 */
static void PostRxDoneEventToSdkQueue(bool aFromISR)
{
    // Only post the event if the sdk queue is available.
    if (gTHCISDKContext.mInitParams.mSdkQueue)
    {
        // Only post the event if the event is not already in the queue. Otherwise, 
        // these events could overflow the queue.
        if (aFromISR)
        {
            if (!sRxEventPostedToSdkQueue)
            {
                sRxEventPostedToSdkQueue = 1;
                nl_eventqueue_post_event_from_isr(gTHCISDKContext.mInitParams.mSdkQueue,  &sUartRxDoneEvent);
            }
        }
        else
        {
            if (!__sync_fetch_and_or(&sRxEventPostedToSdkQueue, 1))
            {
                nl_eventqueue_post_event(gTHCISDKContext.mInitParams.mSdkQueue,  &sUartRxDoneEvent);
            }
        }
    }
}

/**
 * Tries to post an event to the local response queue
 *
 * @param[in] aFromISR True if the caller is in ISR context, false otherwise.
 *
 */
static void PostRxDoneEventToResponseQueue(bool aFromISR)
{
    if (sProvideInternalResponse)
    {
        // Only post the event if the event has not already been posted.  Otherwise,
        // these events could overflow the queue.
        if (sResponseQueueHandle)
        {
            if (aFromISR)
            {
                if (!sRxEventPostedToResponseQueue)
                {
                    sRxEventPostedToResponseQueue = 1;
                    nl_eventqueue_post_event_from_isr(sResponseQueueHandle,  &sUartRxDoneEvent);
                }
            }
            else
            {
                // posting to the response queue from outside ISR context is not supported and is likely a bug.
                NL_LOG_CRIT(lrTHCI, "ERROR: Tried to post to response queue from outside ISR context.\n");
            } 
        }
    }
}

/**
 * Called in ISR context as a Rx callback by the UART module.
 *
 * @param[in] aContext   A pointer to the new received byte.
 *
 */
static void UartRxReadyIsr(void *aContext)
{
    const bool fromISR = true;

    nlREQUIRE(!sDecodeFailure, done);

    if (sProvideInternalResponse || gTHCISDKContext.mInitParams.mSdkQueue)
    {
        if (sProvideInternalResponse)
        {
            PostRxDoneEventToResponseQueue(fromISR);
        }
        else
        {
            PostRxDoneEventToSdkQueue(fromISR);
        }

        // Add the character to the fifo even if the event wasn't posted.
        // AUPD has no event queue but needs Internal response support.
        PutRxFifoChar(*((uint8_t *)aContext));

        if (IsRxFifoNearFull(RX_UART_FIFO_NEAR_FULL_THRESHOLD))
        {
            RxISRDisable();
        }
    }
    else
    {
        // let the character drop. This can happen in AUPD when the Task is no longer 
        // waiting for an internal response but bytes continue to arrive from the NCP.
    }

 done:
    return;
}

/**
 * Called to process UartRxReadyIsr interrupt.
 *
 * @param[in] aEvent    The event object that generated the call to this handler.
 * @param[in] aClosure  A application specific context object associated with the event.
 */
static int UartRxDoneEventHandler(nl_event_t *aEvent, void *aClosure)
{
    (void)aEvent;
    (void)aClosure;

    sRxEventPostedToSdkQueue = 0;

    UartRxFifoProcess();

    if (!sDecodeFailure && 
        !IsRxFifoEmpty())
    {
        const bool fromISR = false;
        // post an event to the sdk queue so that the task will return later to finish 
        // emptying the fifo.
        PostRxDoneEventToSdkQueue(fromISR);
    }

    return 0;
}

static bool CompareResponse(uint8_t aHeader, unsigned int aCommand, spinel_prop_key_t aKey)
{
    bool retval = false;

    if (sResponseTransactionId != kDontCareTransactionId) {
        if (SPINEL_HEADER_GET_TID(aHeader) == sResponseTransactionId)
        {
            retval = true; // when the tid matches and is not the Don't care tid then it is matched.

            // When the TID matches but the command and key do not it indicates operation failure.
            // aKey is almost certainly SPINEL_PROP_LAST_STATUS in the unmatched case.
            if (sResponseCommand == aCommand && sResponseKey == aKey)
            {
                sResponseSuccess = true;
            }
        }
    }
    else if (sResponseCommand == aCommand && sResponseKey == aKey)
    {
        retval = true; // when the tid is the Don't care tid then the command and key must match.
        sResponseSuccess = true;
    }

    return retval;
}

// upon receiving a complete frame from the UART this function will get called.
static void HandleFrame(void *context, uint8_t *aBuf, uint16_t aBufLength)
{
    uint8_t header = 0;
    unsigned int command = 0;
    spinel_ssize_t parsedLength;
    spinel_prop_key_t key;
    const uint8_t *argPtr = NULL;
    unsigned int argLen = 0;

    sFrameByteCount = 0;

    parsedLength = spinel_datatype_unpack(aBuf, aBufLength, "CiiD", &header, &command, &key, &argPtr, &argLen);
    nlREQUIRE_ACTION(parsedLength == aBufLength, done, NL_LOG_CRIT(lrTHCI, "Failed to parse incoming frame\n"));

    if (sProvideInternalResponse && CompareResponse(header, command, key))
    {        
        sResponseReceived           = true;
        sResponseBuffer             = argPtr;
        sResponseLength             = argLen;

        // Often when the NCP fails a request it will return a last status frame with the same TID as the request.
        // The status value can provide insight as to why the previous request failed.
        if (!sResponseSuccess && key == SPINEL_PROP_LAST_STATUS)
        {
            HandleLastStatusUpdate(argPtr, argLen);
        }
    }
    else
    {
        if (key == SPINEL_PROP_STREAM_NET || key == SPINEL_PROP_STREAM_NET_INSECURE)
        {
            if (sDataFrameCB)
            {
                sDataFrameCB(command, key, argPtr, argLen);
            }
        }
        else
        {
            if (sControlFrameCB)
            {
                sControlFrameCB(header, command, key, argPtr, argLen);
            }
        }
    }

 done:
    return;
}

// In the event of a frame decoder error during receive this function will get called.
static void HandleError(void *aContext, otError aError, uint8_t *aFrame, uint16_t aFrameLength)
{
    sResponseSuccess        = false;
    sResponseReceived       = true;
    sDecodeFailure          = true;
    sFrameByteCount         = 0;

    NL_LOG_CRIT(lrTHCI, "ERROR: thci_module_ncp_uart.cpp::HandleError() %d %d.\n", aError, aFrameLength);
#if 0 // useful frame debug code.
    if (aFrame)
    {
        uint16_t i;

        for (i = 0 ; i <= aFrameLength - 8 ; i += 8)
        {
            NL_LOG_CRIT(lrTHCI, "frame bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n", 
                aFrame[i],
                aFrame[i+1],
                aFrame[i+2],
                aFrame[i+3],
                aFrame[i+4],
                aFrame[i+5],
                aFrame[i+6],
                aFrame[i+7]);
        }

        for ( ; i < aFrameLength ; i++)
        {
            NL_LOG_CRIT(lrTHCI, "frame bytes: %02x\n", aFrame[i]);
        }
    }
#endif

    thciInitiateNCPRecovery();
}

static void UartEnable(void)
{
    const uart_callback_config_t rxCBConfig =
    {
        UART_RX_CALLBACK,
        UartRxReadyIsr,
        NULL
    };
    const nl_console_config_t uartCfg = {
        THCI_CONFIG_UART_OPERATIONAL_BAUD_RATE,
        NL_CONSOLE_FLOWCONTROL_ENABLE
    };
    const bool force = true;

    sDecodeFailure = false;
    sRxUartFifoHead = sRxUartFifoTail = 0;
    sFrameByteCount = 0;

    sUartConsole = NL_PRODUCT_CONSOLE(6LOWPAN);

    // Install ISR callback
    uart_install_callback(THCI_UART_ID, true, &rxCBConfig);    
    // Enable the UART
    nl_console_enable(sUartConsole, true, &uartCfg);
}

static void UartDisable(void)
{
    const uart_callback_config_t rxCBConfig =
    {
        UART_RX_CALLBACK,
        NULL,
        NULL
    };

    if (sUartConsole)
    {
        // Disable the UART
        nl_console_enable(sUartConsole, false, NULL);
    
        // Remove ISR callback
        uart_install_callback(THCI_UART_ID, false, &rxCBConfig);
    }
}

otError thciUartEnable(thciUartDataFrameCallback_t aDataCB, thciUartControlFrameCallback_t aControlCB)
{
    otError retval = OT_ERROR_NONE;
    const bool force = true;

    sFrameDecoder = new (&sFrameDecoderBuffer) ot::Hdlc::Decoder(sRxBuffer, sizeof(sRxBuffer), HandleFrame, HandleError, NULL),

    UartEnable();

    // initialize the callbacks for data and control frames. 
    // For AUPD these should be NULL.
    sDataFrameCB    = aDataCB;
    sControlFrameCB = aControlCB;

    if (sDataFrameCB || sControlFrameCB)
    {
        // A QueueHandle is created only if a data or control callback has been
        // provided.
        if (sResponseQueueHandle == NULL)
        {
            sResponseQueueHandle = nl_eventqueue_create(&sResponseQueue, sizeof(sResponseQueue));
            nlREQUIRE_ACTION(sResponseQueueHandle != NULL, done, retval = OT_ERROR_FAILED);

            nl_eventqueue_disable_event_counting(sResponseQueueHandle);
        }

        // Set up Time function
        sGetMillisecondTimeFunc = GetMillisecondTime;
    }
    else
    {
        // Configure for AUPD operation and avoid calling GetMillisecondTime
        sGetMillisecondTimeFunc = GetNoTime;
    }

    RxISREnable(force);

 done:
    return retval;
}

void thciUartSleepEnable(void)
{
    const bool force = true;

    UartEnable();
    RxISREnable(force);
}

void thciUartDisable(void)
{
    UartDisable();

    sDecodeFailure = false;

    sRxUartFifoHead = sRxUartFifoTail = 0;
}

bool thciUartSleepDisable(void)
{
    bool retval = false;
    const bool force = true;

    RxISRDisable();

    if (IsRxFifoEmpty() && sFrameByteCount == 0)
    {
        UartDisable();
        retval = true;
    }
    else
    {
        RxISREnable(!force);
    }

    return retval;
}

otError thciUartFrameSend(uint8_t aTransactionID, uint32_t aCommand, spinel_prop_key_t aKey, const char *aFormat, ...)
{
    spinel_ssize_t packedLen;
    va_list args;
    uint16_t txBufferLen;
    otError error = OT_ERROR_NONE;

    // the transaction ID resides at the bit 0 - 3 so the other two fields can simply be OR'd in to form the header.
    aTransactionID |= SPINEL_HEADER_FLAG | SPINEL_HEADER_IID_0;

    // starting a new frame
    txBufferLen = 0;

    // pack the common frame header {header, command, key}
    packedLen = spinel_datatype_pack(&sTxBuffer[txBufferLen], sizeof(sTxBuffer) - txBufferLen, "Cii", aTransactionID, aCommand, aKey);
    nlREQUIRE_ACTION(packedLen != -1, done, NL_LOG_CRIT(lrTHCI, "ERROR: %s failed spinel_datatype_pack\n", __FUNCTION__); error = OT_ERROR_PARSE);

    txBufferLen += packedLen;
    
    if (aFormat)
    {
        va_start(args, aFormat);
        packedLen = spinel_datatype_vpack(&sTxBuffer[txBufferLen], sizeof(sTxBuffer) - txBufferLen, aFormat, args);
        va_end(args);

        nlREQUIRE_ACTION(packedLen != -1, done, NL_LOG_CRIT(lrTHCI, "ERROR: %s failed spinel_datatype_vpack\n", __FUNCTION__); error = OT_ERROR_PARSE);

        txBufferLen += packedLen;
    }

    error = UartSendFrame(sTxBuffer, txBufferLen);

 done:
    return error;
}

static otError thciUartWaitForResponseInternal(bool aAvoidNCPRecovery, uint8_t aTransactionID, uint8_t aCommand, spinel_prop_key_t aKey, const uint8_t **aBuffer, size_t *aLength)
{
    const uint32_t timeoutMsec = MAX_NCP_APP_RESPONSE_TIME_MSEC;
    nl_event_t *theEvent = NULL;
    otError retval = OT_ERROR_NO_FRAME_RECEIVED;

    nlREQUIRE(!sDecodeFailure, done);

    // register what is being looked for.
    sResponseTransactionId  = aTransactionID;
    sResponseCommand        = aCommand;
    sResponseKey            = aKey;
    sResponseSuccess        = false;
    sResponseReceived       = false;

    // Drive the rx pipe until the desired response is received or a timeout expires.
    sProvideInternalResponse = true;

    if (sResponseQueueHandle)
    {
        // Due to the way events can be posted
        // to the SDKQueue or the sResponseQueueHandle, it is necessary to look at whether such an 
        // event has already been posted prior to blocking on the queue waiting for the next
        // event.  If the event was posted to the other SDKQueue while the task blocks on the 
        // sResponseQueueHandle then the task will never unblock. To solve this the RxFifo
        // is examined by the task and if it is not empty the task shall call UartRxFifoProcess
        // without getting the event. 
        // NOTE: This solution works provided that the SDKQueue and the 
        //       sResponseQueueHandle are managed by the same task.
        while (NULL != (theEvent = nl_eventqueue_get_event_with_timeout(sResponseQueueHandle, ((IsRxFifoEmpty()) ? timeoutMsec : 0))) || 
               !IsRxFifoEmpty())
        {
            if (theEvent)
            {
                sRxEventPostedToResponseQueue = 0;
            }

            UartRxFifoProcess();

            if (sResponseReceived)
            {
                *aBuffer    = sResponseBuffer;
                *aLength    = sResponseLength;
                retval      = (sResponseSuccess) ? OT_ERROR_NONE : OT_ERROR_FAILED;
                break;
            }
        }

        // Pull any event that might have made it into the internal Queue, after exiting the loop above,
        // and push it onto the sdkQueue.
        if (NULL != (theEvent = nl_eventqueue_get_event_with_timeout(sResponseQueueHandle, 0)))
        {
            const bool fromISR = false;
            // The order of operations is important as there is an ISR that read/writes these variables.
            // 1. Move the event to the SDK queue.
            // 2. Clear sProvideInternalResponse so that the ISR will no longer try to post to the response queue.
            // 3. Clear sRxEventPostedToResponseQueue to indicate that the response queue is empty.
            PostRxDoneEventToSdkQueue(fromISR);

            sProvideInternalResponse = false;
            sRxEventPostedToResponseQueue = 0;
        }

        if (!sResponseReceived)
        {
            NL_LOG_CRIT(lrTHCI, "Wait for NCP response timed out. %d\n", timeoutMsec);

            if (!aAvoidNCPRecovery)
            {
                thciInitiateNCPRecovery();
            }
        }
    }
    else
    {
        // This logic is used in AUPD which lacks support for nler queues.
        const uint32_t timeoutFraction = 300;
        const uint32_t numFractions = timeoutMsec / timeoutFraction + 1;
        uint32_t i;

        for (i = 0 ; i < numFractions ; i++)
        {
            if (!IsRxFifoEmpty())
            {
                UartRxFifoProcess();
            }

            if (sResponseReceived)
            {
                *aBuffer    = sResponseBuffer;
                *aLength    = sResponseLength;
                retval      = (sResponseSuccess) ? OT_ERROR_NONE : OT_ERROR_FAILED;
                break;
            }

            DelayMs(timeoutFraction);
        }
    }

 done:
    // clear relevant State before exit
    sResponseReceived = false;
    sProvideInternalResponse = false;

    return retval;
}

otError thciUartWaitForResponse(uint8_t aTransactionID, uint8_t aCommand, spinel_prop_key_t aKey, const uint8_t **aBuffer, size_t *aLength)
{
    const bool avoidNCPRecovery = false;

    return thciUartWaitForResponseInternal(avoidNCPRecovery, aTransactionID, aCommand, aKey, aBuffer, aLength);
}

otError thciUartWaitForResponseIgnoreTimeout(uint8_t aTransactionID, uint8_t aCommand, spinel_prop_key_t aKey, const uint8_t **aBuffer, size_t *aLength)
{
    const bool avoidNCPRecovery = true;

    return thciUartWaitForResponseInternal(avoidNCPRecovery, aTransactionID, aCommand, aKey, aBuffer, aLength);
}

#endif // THCI_CONFIG_USE_OPENTHREAD_ON_NCP
