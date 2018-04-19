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
 *      Implements Thread Host Control Interface update API.
 *
 */

#ifndef __THCI_UPDATE_H_INCLUDED__
#define __THCI_UPDATE_H_INCLUDED__

#include <thci_config.h>

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP

#include <stdint.h>
#include <stddef.h>

#include <nlplatform.h>

#ifdef __cplusplus
extern "C" {
#endif



/**
 * Update the NCP firmware with the image specified.
 *
 * @param[in]  aImageLoc        The location of the firmware image in the Filesystem.
 *
 * @retval  0 on success, error code otherwise.  
 */
int thciFirmwareUpdate(nlfs_image_location_t aImageLoc);

/**
 * Query the NCP for the bootloader version.
 *
 * @param[in]  aBuffer      A buffer to hold the resulting string.
 * @param[in]  aLength      The size of aBuffer in bytes.    
 *
 * @retval  0 on success, error code otherwise.  
 */
int thciGetBootloaderVersion(char *aBuffer, size_t aLength);

/**
 * Query the NCP for the NCP version without initializing THCI.
 *
 * @param[in]  aBuffer      A buffer to hold the resulting string.
 * @param[in]  aLength      The size of aBuffer in bytes.    
 *
 * @retval  0 on success, error code otherwise.  
 */
int thciGetNCPVersionTest(char *aBuffer, size_t aLength);

/**
 * Reset the NCP using Gpio's.
 *
 * @param[in]  aStartBootloader      true to reset the NCP in bootloader mode,
 *                                   false to reset the NCP in application mode.
 *
 */
void thciHardResetNcp(bool aStartBootloader);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // THCI_CONFIG_USE_OPENTHREAD_ON_NCP

#endif /* __THCI_UPDATE_H_INCLUDED__ */
