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
 *      This file defines various THCI functions used for regulatory certification testing
 *
 */

#ifndef __THCI_CERT_H__
#define __THCI_CERT_H__

#include <lwip/pbuf.h>

void thci_cert_set_tx_corrupt_bits(size_t corrupted_bits);
void thci_cert_set_rx_corrupt_bits(size_t corrupted_bits);

void thci_cert_tx_corrupt(struct pbuf *buff);
void thci_cert_rx_corrupt(struct pbuf *buff);

#endif // __THCI_CERT_H__
