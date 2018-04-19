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

/* guard against multiple inclusion is deliberately left out */

#ifdef NLER_SET_LOG_REGION_DEFAULTS
    #ifndef NLER_THCI_LOG_REGION_DEFAULT_SET
    #define NLER_THCI_LOG_REGION_DEFAULT_SET
        nlLPDEBG,   /* lrTHCI */
    #endif
#else
    #ifndef NLER_THCI_LOG_REGION_DEFINED
    #define NLER_THCI_LOG_REGION_DEFINED
        lrTHCI,
    #endif
#endif
