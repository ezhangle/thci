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
 *      Implements Thread Host Control Interface SoC.
 *
 */

#ifndef __THCI_MODULE_SOC_H_INCLUDED__
#define __THCI_MODULE_SOC_H_INCLUDED__

#include <openthread/openthread.h>

#ifdef __cplusplus
extern "C" {
#endif

otInstance *thciGetOtInstance(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif /* __THCI_MODULE_SOC_H_INCLUDED__ */
