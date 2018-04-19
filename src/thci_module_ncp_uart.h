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

/*
 *    @file
 *      This file declares NCP UART version of Thread Host Control Interface.
 *
 */

#ifndef __THCI_MODULE_NCP_UART_H_INCLUDED__
#define __THCI_MODULE_NCP_UART_H_INCLUDED__

#include <thci_config.h>

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP

#include <openthread/types.h>
#include <openthread/spinel.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef void (*thciUartDataFrameCallback_t)(unsigned int aCommand, spinel_prop_key_t aKey, const uint8_t *aArgPtr, unsigned int aArgLen);
typedef void (*thciUartControlFrameCallback_t)(uint8_t aHeader, unsigned int aCommand, spinel_prop_key_t aKey, const uint8_t *aArgPtr, unsigned int aArgLen);

otError thciUartEnable(thciUartDataFrameCallback_t aDataCB, thciUartControlFrameCallback_t aControlCB);
void    thciUartDisable(void);
void    thciUartSleepEnable(void);
bool    thciUartSleepDisable(void);
otError thciUartFrameSend(uint8_t aTransactionID, uint32_t aCommand, spinel_prop_key_t aKey, const char *aFormat, ...);
otError thciUartWaitForResponse(uint8_t aTransactionID, uint8_t aCommand, spinel_prop_key_t aKey, const uint8_t **aBuffer, size_t *aLength);
otError thciUartWaitForResponseIgnoreTimeout(uint8_t aTransactionID, uint8_t aCommand, spinel_prop_key_t aKey, const uint8_t **aBuffer, size_t *aLength);

#ifdef __cplusplus
}
#endif

#endif // THCI_CONFIG_USE_OPENTHREAD_ON_NCP

#endif // __THCI_MODULE_NCP_UART_H_INCLUDED__
