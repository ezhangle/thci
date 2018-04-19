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
 *      Specifies Configuration Thread Host Control Interface API.
 *
 */

#ifndef __THCI_CONFIG_H_INCLUDED__
#define __THCI_CONFIG_H_INCLUDED__


#ifndef THCI_CONFIG_H_IN
#error "This file should only be included from within thci_config.h."
#endif


/**
 * Define this to 1 if you want NCP implementation.
 * This is an implementation with Thread running on a different processor than THCI.
 * Define this to 0 for SOC implementation.
 */
#ifndef THCI_CONFIG_USE_OPENTHREAD_ON_NCP
#define THCI_CONFIG_USE_OPENTHREAD_ON_NCP 0
#endif /* THCI_CONFIG_USE_OPENTHREAD_ON_NCP */

/**
 * Define this if you want to log NCP log messages on the host's logging feature.
 */
#ifndef THCI_CONFIG_LOG_NCP_LOGS
#define THCI_CONFIG_LOG_NCP_LOGS 1
#endif /* THCI_CONFIG_LOG_NCP_LOGS */

/**
 * Define as 1 to enable OpenThread border router feature and its APIs.
 */
#ifndef THCI_CONFIG_ENABLE_BORDER_ROUTER
#define THCI_CONFIG_ENABLE_BORDER_ROUTER  1
#endif /* THCI_CONFIG_ENABLE_BORDER_ROUTER */

/**
 * Define this to the size of the message queue for outgoing IP 
 * messages. This will determine how many messages may be queued to
 * THCI before it will return an error.
 */
#ifndef THCI_CONFIG_MESSAGE_QUEUE_SIZE
#define THCI_CONFIG_MESSAGE_QUEUE_SIZE 16
#endif /* THCI_CONFIG_MESSAGE_QUEUE_SIZE */

/**
 * Define this to the number of bytes used by the NCP ring buffer for
 * outgoing IP messages. This will be used to store a copy of each IP 
 * message requested for TX. When this ring buffer is full or does not
 * have enough space for the next request, the calling task is blocked
 * until space becomes available.
 */
#ifndef THCI_CONFIG_NCP_TX_MESSAGE_RING_BUFFER_SIZE
#define THCI_CONFIG_NCP_TX_MESSAGE_RING_BUFFER_SIZE (5 * NL_THCI_PAYLOAD_MTU)
#endif /* THCI_CONFIG_NCP_TX_MESSAGE_RING_BUFFER_SIZE */

/**
 * Define as 1 to use OpenThread's as FTD (Full Thread Device). Define
 * to 0 to use Openthread's as MTD (Minimal Thread Device).
 */
#ifndef THCI_ENABLE_FTD
#define THCI_ENABLE_FTD  0
#endif

/**
 * Define as 1 to enable 6lowpan legacy alarming support feature and its API's.
 */
#ifndef THCI_CONFIG_LEGACY_ALARM_SUPPORT
#define THCI_CONFIG_LEGACY_ALARM_SUPPORT 0
#endif

/**
 * Define as 1 to enable Spinel Vendor support commands and properties.
 */
#ifndef THCI_CONFIG_SPINEL_VENDOR_SUPPORT
#define THCI_CONFIG_SPINEL_VENDOR_SUPPORT 1
#endif

/**
 * Define as 1 to enable NCP credential recovery from legacy credential storage.
 */
#ifndef THCI_CONFIG_LEGACY_NCP_CREDENTIAL_RECOVERY
#define THCI_CONFIG_LEGACY_NCP_CREDENTIAL_RECOVERY 0
#endif

/**
 * NCP UART's app baud rate
 */
#ifndef THCI_CONFIG_UART_OPERATIONAL_BAUD_RATE
#define THCI_CONFIG_UART_OPERATIONAL_BAUD_RATE 115200
#endif

/**
 * Define as 1 to allow a product to execute thciInitialize without resetting 
 * the NCP. When set to 0 thciInitialize will always reset the NCP.
 */
#ifndef THCI_CONFIG_INITIALIZE_WITHOUT_NCP_RESET
#define THCI_CONFIG_INITIALIZE_WITHOUT_NCP_RESET 0
#endif

#endif /* __THCI_CONFIG_H_INCLUDED__ */

