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
 *      Implements Thread Host Control Interface Shell entry.
 *
 */

#ifndef __THCI_SHELL_H_INCLUDED__
#define __THCI_SHELL_H_INCLUDED__


#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

int thciShellHandleCommand(int argc, const char * argv[]);

int thciShellMfgStart(void);

int thciShellMfgSetChannel(uint16_t inChannel);

int thciShellMfgSetPower(int inPower);

int thciShellMfgSetGpio(uint16_t inPin, uint8_t inValue);

int thciShellMfgGetGpio(uint16_t inPin);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif /* __THCI_SHELL_H_INCLUDED__ */
