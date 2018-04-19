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

THCI_INCLUDES =                                  \
    thci.h                                       \
    thci_safe_api.h                              \
    thci_config.h                                \
    thci_default_config.h                        \
    thci_logregions.h                            \
    thci_module.h                                \
    thci_notification.h                          \
    thci_shell.h                                 \

ifeq ($(BUILD_FEATURE_THCI_CERT),1)

THCI_INCLUDES +=                                 \
    thci_cert.h                                  \

endif

THCI_INCLUDES +=                                 \
    $(NULL)

THCI_SOURCES =                                   \
    thci.c                                       \
    thci_module_ncp.c                            \
    thci_module_soc.c                            \
    thci_module_ncp_uart.cpp                     \
    thci_module_ncp_update.c                     \
    thci_shell.c                                 \
    thci_safe_api.c                              \

ifeq ($(BUILD_FEATURE_THCI_CERT),1)

THCI_SOURCES +=                                  \
    thci_cert.c

endif

THCI_SOURCES += $(NULL)

