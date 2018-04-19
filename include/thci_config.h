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
 *      Specifies Default Configuration of the Thread Host Control Interface API.
 *
 */

#ifndef __THCI_DEFAULT_CONFIG_H_INCLUDED__
#define __THCI_DEFAULT_CONFIG_H_INCLUDED__

#ifndef THCI_HAVE_PROJECT_SPECIFIC_CONFIG
#define THCI_HAVE_PROJECT_SPECIFIC_CONFIG 0
#endif

#define THCI_CONFIG_H_IN

#if THCI_HAVE_PROJECT_SPECIFIC_CONFIG
#include <thci-project-config.h>
#endif

#include <thci_default_config.h>

#undef THCI_CONFIG_H_IN

#endif /* __THCI_CONFIG_H_INCLUDED__ */

