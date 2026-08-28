// PS5 Radio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "radio_ts_aac.hpp"

#include <stdbool.h>
#include <string.h>

#define TS_PID_NONE UINT16_C(0x1fff)

typedef radio_ts_aac_result_t (*psi_ready_fn)(radio_ts_aac_parser_t *parser, const uint8_t *section,
                                              size_t size);

static uint32_t mpeg_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0; i < size; ++i)
    {
        crc ^= (uint32_t)data[i] << 24U;
        for (unsigned bit = 0; bit < 8U; ++bit)
        {
            crc =
                (crc & UINT32_C(0x80000000)) != 0U ? (crc << 1U) ^ UINT32_C(0x04c11db7) : crc << 1U;
        }
    }
    return crc;
}

static radio_ts_aac_result_t pat_ready(radio_ts_aac_parser_t *parser, const uint8_t *section,
                                       size_t size)
{
    if (size < 12U || section[0] != 0x00U || (section[1] & 0x80U) == 0U ||
        mpeg_crc32(section, size) != 0U)
        return RADIO_TS_AAC_MALFORMED;
    if ((section[5] & 0x01U) == 0U)
        return RADIO_TS_AAC_OK;

    const size_t entries_end = size - 4U;
    for (size_t at = 8U; at + 4U <= entries_end; at += 4U)
    {
        const uint16_t program = (uint16_t)((section[at] << 8U) | section[at + 1U]);
        if (program == 0U)
            continue;
        const uint16_t pid = (uint16_t)(((section[at + 2U] & 0x1fU) << 8U) | section[at + 3U]);
        if (parser->pmt_pid != pid)
        {
            parser->pmt_pid = pid;
            parser->aac_pid = TS_PID_NONE;
            parser->pmt.size = 0U;
            parser->pmt.expected = 0U;
            parser->pes_started = 0U;
            parser->pes_remaining = 0U;
        }
        return RADIO_TS_AAC_OK;
    }
    return RADIO_TS_AAC_UNSUPPORTED;
}

static radio_ts_aac_result_t pmt_ready(radio_ts_aac_parser_t *parser, const uint8_t *section,
                                       size_t size)
{
    if (size < 16U || section[0] != 0x02U || (section[1] & 0x80U) == 0U ||
        mpeg_crc32(section, size) != 0U)
        return RADIO_TS_AAC_MALFORMED;
    if ((section[5] & 0x01U) == 0U)
        return RADIO_TS_AAC_OK;

    const size_t entries_end = size - 4U;
    const size_t program_info = (size_t)((section[10] & 0x0fU) << 8U) | section[11];
    size_t at = 12U + program_info;
    if (at > entries_end)
        return RADIO_TS_AAC_MALFORMED;
    while (at + 5U <= entries_end)
    {
        const uint8_t stream_type = section[at];
        const uint16_t pid = (uint16_t)(((section[at + 1U] & 0x1fU) << 8U) | section[at + 2U]);
        const size_t info = (size_t)((section[at + 3U] & 0x0fU) << 8U) | section[at + 4U];
        if (at + 5U + info > entries_end)
            return RADIO_TS_AAC_MALFORMED;
        if (stream_type == 0x0fU)
        {
            if (parser->aac_pid != pid)
            {
                parser->aac_pid = pid;
                parser->pes_started = 0U;
                parser->pes_remaining = 0U;
            }
            return RADIO_TS_AAC_OK;
        }
        at += 5U + info;
    }
    return RADIO_TS_AAC_UNSUPPORTED;
}

static radio_ts_aac_result_t psi_append(radio_ts_aac_parser_t *parser, radio_ts_psi_t *psi,
                                        const uint8_t *data, size_t size, psi_ready_fn ready,
                                        size_t *consumed)
{
    *consumed = 0U;
    while (*consumed < size)
    {
        if (psi->size == 0U && data[*consumed] == 0xffU)
        {
            *consumed = size;
            return RADIO_TS_AAC_OK;
        }
        size_t wanted = psi->expected != 0U ? psi->expected : 3U;
        if (wanted > sizeof(psi->data))
            return RADIO_TS_AAC_MALFORMED;
        const size_t missing = wanted - psi->size;
        const size_t available = size - *consumed;
        const size_t copy = missing < available ? missing : available;
        memcpy(psi->data + psi->size, data + *consumed, copy);
        psi->size += copy;
        *consumed += copy;
        if (psi->size < wanted)
            return RADIO_TS_AAC_OK;

        if (psi->expected == 0U)
        {
            const size_t section_length = (size_t)((psi->data[1] & 0x0fU) << 8U) | psi->data[2];
            psi->expected = 3U + section_length;
            if (section_length < 4U || psi->expected > sizeof(psi->data))
                return RADIO_TS_AAC_MALFORMED;
            continue;
        }

        const radio_ts_aac_result_t result = ready(parser, psi->data, psi->expected);
        psi->size = 0U;
        psi->expected = 0U;
        if (result != RADIO_TS_AAC_OK)
            return result;
    }
    return RADIO_TS_AAC_OK;
}

static radio_ts_aac_result_t psi_feed(radio_ts_aac_parser_t *parser, radio_ts_psi_t *psi,
                                      const uint8_t *data, size_t size, bool start,
                                      psi_ready_fn ready)
{
    if (start)
    {
        if (size == 0U)
            return RADIO_TS_AAC_MALFORMED;
        const size_t pointer = data[0];
        ++data;
        --size;
        if (pointer > size)
            return RADIO_TS_AAC_MALFORMED;
        if (psi->size != 0U && pointer != 0U)
        {
            size_t consumed = 0U;
            const radio_ts_aac_result_t result =
                psi_append(parser, psi, data, pointer, ready, &consumed);
            if (result != RADIO_TS_AAC_OK)
                return result;
            if (psi->size != 0U || consumed != pointer)
                return RADIO_TS_AAC_MALFORMED;
        }
        else if (psi->size != 0U)
        {
            psi->size = 0U;
            psi->expected = 0U;
        }
        data += pointer;
        size -= pointer;
    }
    else if (psi->size == 0U)
        return RADIO_TS_AAC_OK;

    while (size != 0U)
    {
        size_t consumed = 0U;
        const radio_ts_aac_result_t result = psi_append(parser, psi, data, size, ready, &consumed);
        if (result != RADIO_TS_AAC_OK)
            return result;
        if (consumed == 0U)
            return RADIO_TS_AAC_MALFORMED;
        data += consumed;
        size -= consumed;
    }
    return RADIO_TS_AAC_OK;
}

static radio_ts_aac_result_t packet_ready(radio_ts_aac_parser_t *parser, const uint8_t *packet)
{
    if (packet[0] != 0x47U || (packet[1] & 0x80U) != 0U)
        return RADIO_TS_AAC_MALFORMED;
    const bool start = (packet[1] & 0x40U) != 0U;
    const uint16_t pid = (uint16_t)(((packet[1] & 0x1fU) << 8U) | packet[2]);
    const uint8_t adaptation = (uint8_t)((packet[3] >> 4U) & 0x03U);
    if (adaptation == 0U)
        return RADIO_TS_AAC_MALFORMED;

    size_t at = 4U;
    if ((adaptation & 0x02U) != 0U)
    {
        const size_t length = packet[at++];
        if (at + length > RADIO_TS_PACKET_BYTES)
            return RADIO_TS_AAC_MALFORMED;
        if (length != 0U && (packet[at] & 0x80U) != 0U)
            parser->pes_started = 0U;
        at += length;
    }
    if ((adaptation & 0x01U) == 0U || at == RADIO_TS_PACKET_BYTES)
        return RADIO_TS_AAC_OK;

    const uint8_t *payload = packet + at;
    size_t size = RADIO_TS_PACKET_BYTES - at;
    if (pid == 0U)
        return psi_feed(parser, &parser->pat, payload, size, start, pat_ready);
    if (pid == parser->pmt_pid)
        return psi_feed(parser, &parser->pmt, payload, size, start, pmt_ready);
    if (pid != parser->aac_pid)
        return RADIO_TS_AAC_OK;

    if (start)
    {
        parser->pes_started = 0U;
        parser->pes_remaining = 0U;
        if (size < 9U || payload[0] != 0x00U || payload[1] != 0x00U || payload[2] != 0x01U ||
            (payload[3] & 0xe0U) != 0xc0U)
            return RADIO_TS_AAC_MALFORMED;
        const size_t header = 9U + payload[8];
        if (header > size)
            return RADIO_TS_AAC_UNSUPPORTED;
        const uint32_t packet_length = (uint32_t)(payload[4] << 8U) | payload[5];
        const uint32_t header_after_length = 3U + payload[8];
        if (packet_length != 0U && packet_length < header_after_length)
            return RADIO_TS_AAC_MALFORMED;
        parser->pes_remaining =
            packet_length == 0U ? UINT32_MAX : packet_length - header_after_length;
        payload += header;
        size -= header;
        parser->pes_started = 1U;
    }
    if (parser->pes_started == 0U || size == 0U)
        return RADIO_TS_AAC_OK;
    if (size > parser->pes_remaining)
        size = parser->pes_remaining;
    if (size == 0U)
    {
        parser->pes_started = 0U;
        return RADIO_TS_AAC_OK;
    }
    if (parser->output(payload, size, parser->user_data) != 0)
        return RADIO_TS_AAC_CALLBACK;
    if (parser->pes_remaining != UINT32_MAX)
    {
        parser->pes_remaining -= (uint32_t)size;
        if (parser->pes_remaining == 0U)
            parser->pes_started = 0U;
    }
    return RADIO_TS_AAC_OK;
}

void radio_ts_aac_init(radio_ts_aac_parser_t *parser, radio_ts_aac_output_fn output,
                       void *user_data)
{
    if (parser == nullptr)
        return;
    memset(parser, 0, sizeof(*parser));
    parser->output = output;
    parser->user_data = user_data;
    parser->pmt_pid = TS_PID_NONE;
    parser->aac_pid = TS_PID_NONE;
}

void radio_ts_aac_reset(radio_ts_aac_parser_t *parser)
{
    if (parser == nullptr)
        return;
    const radio_ts_aac_output_fn output = parser->output;
    void *user_data = parser->user_data;
    radio_ts_aac_init(parser, output, user_data);
}

radio_ts_aac_result_t radio_ts_aac_feed(radio_ts_aac_parser_t *parser, const uint8_t *data,
                                        size_t size)
{
    if (parser == nullptr || parser->output == nullptr || (data == nullptr && size != 0U))
        return RADIO_TS_AAC_INVALID;
    while (size != 0U)
    {
        const size_t missing = RADIO_TS_PACKET_BYTES - parser->packet_size;
        const size_t copy = size < missing ? size : missing;
        memcpy(parser->packet + parser->packet_size, data, copy);
        parser->packet_size += copy;
        data += copy;
        size -= copy;
        if (parser->packet_size != RADIO_TS_PACKET_BYTES)
            continue;

        if (parser->packet[0] != 0x47U)
        {
            size_t sync = 1U;
            while (sync < RADIO_TS_PACKET_BYTES && parser->packet[sync] != 0x47U)
                ++sync;
            memmove(parser->packet, parser->packet + sync, RADIO_TS_PACKET_BYTES - sync);
            parser->packet_size = RADIO_TS_PACKET_BYTES - sync;
            continue;
        }
        const radio_ts_aac_result_t result = packet_ready(parser, parser->packet);
        parser->packet_size = 0U;
        if (result != RADIO_TS_AAC_OK)
            return result;
    }
    return RADIO_TS_AAC_OK;
}
