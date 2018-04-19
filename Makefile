#
#    Copyright 2016-2018 Nest Labs Inc. All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#

#
#    Description:
#      This is a make file for Thread Host Control Interface.
#

include pre.mak

ifeq ($(BUILD_FEATURE_THCI),1)

include thci.mak

.DEFAULT_GOAL = all


VPATH                                          = \
    src                                          \

ARCHIVES = thci

thci_SOURCES = $(THCI_SOURCES)

thci_INCLUDES =                                  \
    src                                          \
    $(OpenThreadIncludePaths)                    \
    $(NlOpenThreadIncludePaths)                  \
    $(AppsIncludePaths)                          \
    $(NlERIncludePaths)                          \
    $(LwIPIncludePaths)                          \
    $(NlLwipIncludePath)                         \
    $(NlPlatformIncludePaths)                    \
    $(FreeRTOSIncludePaths)                      \
    $(NlIoIncludePaths)                          \
    $(NlAssertIncludePaths)                      \
    $(NlUtilitiesIncludePaths)                   \
    $(NlSystemIncludePath)                       \
    $(NlEnvIncludePaths)                         \
    $(NlTHCIProjectIncludePaths)                 \
    $(nlTHCIProjectNCPIncludePath)               \
    $(ThciIncludePaths)                          \
    $(WicedIncludePaths)                         \
    $(NULL)

ifeq ($(BUILD_FEATURE_THCI_NCP_HOST),1)

thci_INCLUDES +=                                 \
    $(MarbleFirmwareSpinelIncludePaths)          \
    $(NULL)

endif

thci_DEFINES = $(THCI_DEFINES)

thci_HEADERS = $(foreach headerfile,$(THCI_INCLUDES),include/$(headerfile))

endif # ifeq ($(BUILD_FEATURE_THCI),1)

include post.mak

