/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#include "fixed_plan.h"
#include <lorawan.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lorawan_native_fixed_plan, CONFIG_LORAWAN_LOG_LEVEL);

/*
 * ChMaskCntl encoding, identical in US902-928 Table 23 and AU915-928 Table 43:
 *   0..4 = ChMask applies to a block of 16 channels starting at cntl * 16.
 *   5    = bits 0..7 each switch a bank of CH_BANK_SIZE 125 kHz channels
 *          together with the 500 kHz channel above it; bit 8 and bits 10..15
 *          are RFU, and bit 9 names a bank beyond the plan stored here.
 *   6    = all 125 kHz channels on, ChMask applies to the 500 kHz block.
 *   7    = all 125 kHz channels off, ChMask applies to the 500 kHz block.
 */
#define CH_MASK_CNTL_BLOCK_MAX 4
#define CH_MASK_CNTL_BANKS     5
#define CH_MASK_CNTL_ALL_ON    6
#define CH_MASK_CNTL_ALL_OFF   7

#define CH_BLOCK_SIZE 16
#define CH_BANK_SIZE  8
#define CH_BANK_COUNT 8

int lwan_fixed_get_default_channels(const struct lwan_fixed_plan *plan, struct lwan_channel *ch,
				    size_t *count)
{
	uint8_t total = lwan_fixed_ul_count(plan);

	if (*count < total) {
		return -ENOMEM;
	}

	for (uint8_t i = 0; i < plan->ul_125_count; i++) {
		ch[i] = (struct lwan_channel){
			.frequency = plan->ul_125_base_hz + (uint32_t)i * plan->ul_125_step_hz,
			.min_dr = plan->ul_125_min_dr,
			.max_dr = plan->ul_125_max_dr,
			.enabled = true,
		};
	}

	for (uint8_t i = 0; i < plan->ul_500_count; i++) {
		ch[plan->ul_125_count + i] = (struct lwan_channel){
			.frequency = plan->ul_500_base_hz + (uint32_t)i * plan->ul_500_step_hz,
			.min_dr = plan->ul_500_dr,
			.max_dr = plan->ul_500_dr,
			.enabled = true,
		};
	}

	*count = total;
	return 0;
}

int lwan_fixed_channel_index(const struct lwan_fixed_plan *plan, uint32_t freq, size_t *idx)
{
	if (freq >= plan->ul_125_base_hz) {
		uint32_t delta = freq - plan->ul_125_base_hz;

		if (delta % plan->ul_125_step_hz == 0 &&
		    delta / plan->ul_125_step_hz < plan->ul_125_count) {
			*idx = delta / plan->ul_125_step_hz;
			return 0;
		}
	}

	if (freq >= plan->ul_500_base_hz) {
		uint32_t delta = freq - plan->ul_500_base_hz;

		if (delta % plan->ul_500_step_hz == 0 &&
		    delta / plan->ul_500_step_hz < plan->ul_500_count) {
			*idx = plan->ul_125_count + delta / plan->ul_500_step_hz;
			return 0;
		}
	}

	return -EINVAL;
}

int lwan_fixed_rx1_frequency(const struct lwan_fixed_plan *plan, uint32_t tx_freq,
			     uint32_t *rx1_freq)
{
	size_t idx;
	int ret;

	ret = lwan_fixed_channel_index(plan, tx_freq, &idx);
	if (ret != 0) {
		LOG_WRN("TX frequency %u is not in the channel plan", tx_freq);
		return ret;
	}

	*rx1_freq = plan->dl_base_hz + (uint32_t)(idx % plan->dl_count) * plan->dl_step_hz;
	return 0;
}

static bool cflist_bit(const uint8_t cflist[LWAN_CFLIST_SIZE], size_t idx)
{
	uint16_t mask = sys_get_le16(&cflist[(idx / CH_BLOCK_SIZE) * 2]);

	return (mask & BIT(idx % CH_BLOCK_SIZE)) != 0;
}

int lwan_fixed_apply_cflist(const struct lwan_fixed_plan *plan,
			    const uint8_t cflist[LWAN_CFLIST_SIZE], struct lwan_channel *ch,
			    size_t *count)
{
	size_t span = MIN(*count, (size_t)lwan_fixed_ul_count(plan));
	bool any = false;

	if (cflist[LWAN_CFLIST_TYPE_OFFSET] != LWAN_CFLIST_TYPE_CH_MASK) {
		LOG_WRN("Unsupported CFList type: %u", cflist[LWAN_CFLIST_TYPE_OFFSET]);
		return 0;
	}

	for (size_t i = 0; i < span; i++) {
		if (cflist_bit(cflist, i)) {
			any = true;
			break;
		}
	}

	/*
	 * A CFList that leaves no channel enabled would strand the device on
	 * a session it can never transmit in, so keep the plan the
	 * Join-Accept arrived with instead.
	 */
	if (!any) {
		LOG_WRN("CFList disables every channel, ignoring it");
		return 0;
	}

	for (size_t i = 0; i < span; i++) {
		ch[i].enabled = cflist_bit(cflist, i);
	}

	return 0;
}

/*
 * The ChMask bits that name a channel this device stores. The tables reach
 * channel 79 and a tenth bank, both above the 72 channels the plans ask for
 * as a minimum.
 */
static uint16_t adr_mask_valid_bits(const struct lwan_fixed_plan *plan, size_t span,
				    uint8_t ch_mask_cntl)
{
	size_t first;

	switch (ch_mask_cntl) {
	case CH_MASK_CNTL_BANKS:
		return (uint16_t)BIT_MASK(CH_BANK_COUNT);
	case CH_MASK_CNTL_ALL_ON:
	case CH_MASK_CNTL_ALL_OFF:
		first = plan->ul_125_count;
		break;
	default:
		first = (size_t)ch_mask_cntl * CH_BLOCK_SIZE;
		break;
	}

	if (first >= span) {
		return 0;
	}

	return (uint16_t)BIT_MASK(MIN(span - first, (size_t)CH_BLOCK_SIZE));
}

/* The state channel @p idx takes if this request is applied. */
static bool adr_mask_result(const struct lwan_fixed_plan *plan, size_t idx, uint8_t ch_mask_cntl,
			    uint16_t ch_mask, bool current)
{
	size_t first;

	switch (ch_mask_cntl) {
	case CH_MASK_CNTL_BANKS: {
		size_t bank = (idx < plan->ul_125_count) ? idx / CH_BANK_SIZE
							 : idx - plan->ul_125_count;

		return (ch_mask & BIT(bank)) != 0;
	}
	case CH_MASK_CNTL_ALL_ON:
	case CH_MASK_CNTL_ALL_OFF:
		if (idx < plan->ul_125_count) {
			return ch_mask_cntl == CH_MASK_CNTL_ALL_ON;
		}

		return (ch_mask & BIT(idx - plan->ul_125_count)) != 0;
	default:
		first = (size_t)ch_mask_cntl * CH_BLOCK_SIZE;

		if (idx < first || idx >= first + CH_BLOCK_SIZE) {
			return current;
		}

		return (ch_mask & BIT(idx - first)) != 0;
	}
}

int lwan_fixed_apply_adr_channel_mask(const struct lwan_fixed_plan *plan, struct lwan_channel *ch,
				      size_t count, uint8_t ch_mask_cntl, uint16_t ch_mask)
{
	size_t span = MIN(count, (size_t)lwan_fixed_ul_count(plan));
	bool any = false;

	if (ch_mask_cntl > CH_MASK_CNTL_ALL_OFF) {
		return -EINVAL;
	}

	if (ch_mask_cntl <= CH_MASK_CNTL_BLOCK_MAX &&
	    (size_t)ch_mask_cntl * CH_BLOCK_SIZE >= span) {
		/* The block addresses channels this device does not have. */
		return -EINVAL;
	}

	if ((ch_mask & ~adr_mask_valid_bits(plan, span, ch_mask_cntl)) != 0) {
		return -EINVAL;
	}

	/*
	 * Every channel is a default channel here, so there is no pair to
	 * protect the way the dynamic plans protect theirs. What a device
	 * must not accept is a mask that leaves it nothing to transmit on,
	 * so the result is counted before anything is committed.
	 */
	for (size_t i = 0; i < span; i++) {
		if (adr_mask_result(plan, i, ch_mask_cntl, ch_mask, ch[i].enabled)) {
			any = true;
			break;
		}
	}

	if (!any) {
		return -EINVAL;
	}

	for (size_t i = 0; i < span; i++) {
		ch[i].enabled = adr_mask_result(plan, i, ch_mask_cntl, ch_mask, ch[i].enabled);
	}

	return 0;
}

int lwan_fixed_select_in_range(const struct lwan_channel *ch, size_t count, size_t first,
			       size_t span, uint8_t dr, uint32_t *freq)
{
	uint8_t candidates[LWAN_MAX_CHANNELS];
	uint8_t candidate_count = 0;

	for (size_t i = first; i < MIN(count, first + span); i++) {
		if (!ch[i].enabled || ch[i].frequency == 0 || dr < ch[i].min_dr ||
		    dr > ch[i].max_dr) {
			continue;
		}

		candidates[candidate_count++] = i;
	}

	if (candidate_count == 0) {
		return -ENOENT;
	}

	*freq = ch[candidates[sys_rand8_get() % candidate_count]].frequency;

	return 0;
}

int lwan_fixed_select_join_channel(const struct lwan_fixed_plan *plan,
				   struct lwan_fixed_join_cursor *cur,
				   const struct lwan_channel *ch, size_t count, uint8_t dr_125,
				   uint8_t dr_500, uint32_t *freq, uint8_t *dr)
{
	uint8_t groups_125 = plan->ul_125_count / LWAN_FIXED_JOIN_GROUP_SIZE;
	uint8_t group_count = groups_125 + 1;

	/*
	 * The plans ask the device to probe the octet groups of 125 kHz
	 * channels in turn and to close each pass on one 500 kHz channel, so
	 * the cursor walks one group per call and the draw is random only
	 * inside the group. A full turn without a usable channel means the
	 * plan has none.
	 */
	for (uint8_t tries = 0; tries < group_count; tries++) {
		uint8_t group = cur->group;
		size_t first;
		size_t span;
		uint8_t join_dr;
		int ret;

		if (group == groups_125) {
			first = plan->ul_125_count + cur->pass;
			span = 1;
			join_dr = dr_500;
		} else {
			first = (size_t)group * LWAN_FIXED_JOIN_GROUP_SIZE;
			span = LWAN_FIXED_JOIN_GROUP_SIZE;
			join_dr = dr_125;
		}

		ret = lwan_fixed_select_in_range(ch, count, first, span, join_dr, freq);

		if (cur->group < groups_125) {
			cur->group++;
		} else {
			cur->group = 0;
			cur->pass = (cur->pass + 1) % plan->ul_500_count;
		}

		if (ret == 0) {
			*dr = join_dr;
			return 0;
		}
	}

	return -ENOENT;
}
