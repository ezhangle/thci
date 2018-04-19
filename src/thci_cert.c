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

#include <thci.h>
#include <thci_cert.h>
#include <nlassert.h>

#include <lwip/pbuf.h>

static size_t s_tx_corrupted_bits = 0;
static size_t s_rx_corrupted_bits = 0;

static void thci_cert_pbuf_bit_flip(struct pbuf *buff, size_t offset)
{
    size_t bit_offset = offset & 0x07; //offset % 8
    size_t byte_offset = offset >> 3; //offset / 8

    bool flipped = false;

    while (buff)
    {
        if (buff->len > byte_offset)
        {
            uint8_t *payload = (uint8_t *)buff->payload;
            payload[byte_offset] ^= 0x01 << bit_offset;

            break;
        }
        else
        {
            byte_offset -= buff->len;
            buff = buff->next;
        }
    }

}

static void thci_cert_pbuf_corrupt(struct pbuf *buff, size_t bits)
{
    size_t flipped[3];
    int i = 0;

    nlREQUIRE(buff != NULL, done);
    nlREQUIRE(bits <= 3, done);

    while (i < bits)
    {
        // Skip the IPv6 header, start corruption at byte 41
        size_t offset = rand() % ((buff->tot_len - 40) << 3);
        offset += 40 << 3;

        for (int j = 0; j < i; j++)
        {
            if (flipped[j] == offset)
            {
                // If we already used this offset, try again
                continue;
            }
        }

        thci_cert_pbuf_bit_flip(buff, offset);

        flipped[i] = offset;
        i++;
    }

done:
    return;
}

void thci_cert_set_tx_corrupt_bits(size_t corrupted_bits)
{
    s_tx_corrupted_bits = corrupted_bits;
}

void thci_cert_set_rx_corrupt_bits(size_t corrupted_bits)
{
    s_rx_corrupted_bits = corrupted_bits;
}

void thci_cert_tx_corrupt(struct pbuf *buff)
{
    NL_LOG_CRIT(lrTHCI, "s_tx_corrupted_bits %d\n", s_tx_corrupted_bits);
    thci_cert_pbuf_corrupt(buff, s_tx_corrupted_bits);
}

void thci_cert_rx_corrupt(struct pbuf *buff)
{
    NL_LOG_CRIT(lrTHCI, "s_rx_corrupted_bits %d\n", s_rx_corrupted_bits);
    thci_cert_pbuf_corrupt(buff, s_rx_corrupted_bits);
}
