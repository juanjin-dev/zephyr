/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Helpers shared by the fixed channel plan regions of RP002-1.0.5 2.2.2:
 * a block of 125 kHz channels, a block of 500 kHz channels above it, and a
 * separate downlink block, all laid out by formula rather than by a CFList.
 */

#ifndef SUBSYS_LORAWAN_NATIVE_REGION_FIXED_PLAN_H_
#define SUBSYS_LORAWAN_NATIVE_REGION_FIXED_PLAN_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "region.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The three channel blocks a fixed plan is built from. */
struct lwan_fixed_plan {
	uint32_t ul_125_base_hz;
	uint32_t ul_125_step_hz;
	uint32_t ul_500_base_hz;
	uint32_t ul_500_step_hz;
	uint32_t dl_base_hz;
	uint32_t dl_step_hz;
	uint8_t ul_125_count;
	uint8_t ul_500_count;
	uint8_t dl_count;
	/* Datarate range the two uplink blocks carry. */
	uint8_t ul_125_min_dr;
	uint8_t ul_125_max_dr;
	uint8_t ul_500_dr;
};

/* Walks the octet groups of 125 kHz channels, one 500 kHz channel per pass. */
struct lwan_fixed_join_cursor {
	uint8_t group;
	uint8_t pass;
};

#define LWAN_FIXED_JOIN_GROUP_SIZE 8

/* Total uplink channels the plan describes. */
static inline uint8_t lwan_fixed_ul_count(const struct lwan_fixed_plan *plan)
{
	return plan->ul_125_count + plan->ul_500_count;
}

/**
 * @brief Bring up every channel the plan describes.
 *
 * @param plan Channel plan.
 * @param ch Output channel array.
 * @param count In: max entries; Out: number of channels written.
 * @return 0 on success, -ENOMEM if @p ch cannot hold the plan.
 */
int lwan_fixed_get_default_channels(const struct lwan_fixed_plan *plan, struct lwan_channel *ch,
				    size_t *count);

/**
 * @brief Recover a channel number from the frequency it was transmitted on.
 *
 * The 500 kHz centres fall between the 125 kHz ones, so the two blocks never
 * name the same frequency.
 *
 * @param plan Channel plan.
 * @param freq Frequency in Hz.
 * @param idx Output: channel number.
 * @return 0 on success, -EINVAL if @p freq is not in the plan.
 */
int lwan_fixed_channel_index(const struct lwan_fixed_plan *plan, uint32_t freq, size_t *idx);

/**
 * @brief RX1 frequency for an uplink that went out on @p freq.
 *
 * @param plan Channel plan.
 * @param tx_freq Frequency the uplink used.
 * @param rx1_freq Output: RX1 frequency.
 * @return 0 on success, -EINVAL if @p tx_freq is not in the plan.
 */
int lwan_fixed_rx1_frequency(const struct lwan_fixed_plan *plan, uint32_t tx_freq,
			     uint32_t *rx1_freq);

/**
 * @brief Apply a CFList Type 1 channel mask.
 *
 * A mask that leaves nothing enabled is ignored rather than applied, so a
 * Join-Accept cannot strand the device.
 *
 * @param plan Channel plan.
 * @param cflist CFList from the Join-Accept, LWAN_CFLIST_SIZE bytes.
 * @param ch Channel array to update.
 * @param count In: current count; Out: unchanged.
 * @return 0 always; an unusable CFList is logged and left unapplied.
 */
int lwan_fixed_apply_cflist(const struct lwan_fixed_plan *plan,
			    const uint8_t cflist[LWAN_CFLIST_SIZE], struct lwan_channel *ch,
			    size_t *count);

/**
 * @brief Validate and apply a ChMaskCntl + ChMask from a LinkADRReq.
 *
 * @param plan Channel plan.
 * @param ch Channel array, modified in place only on success.
 * @param count Number of entries in @p ch.
 * @param ch_mask_cntl ChMaskCntl field (0..7).
 * @param ch_mask 16-bit channel mask.
 * @return 0 on success, -EINVAL if the request is rejected.
 */
int lwan_fixed_apply_adr_channel_mask(const struct lwan_fixed_plan *plan, struct lwan_channel *ch,
				      size_t count, uint8_t ch_mask_cntl, uint16_t ch_mask);

/**
 * @brief Pick a random enabled channel in [first, first + span) carrying @p dr.
 *
 * @param ch Channel array.
 * @param count Number of channels.
 * @param first First channel to consider.
 * @param span How many channels to consider.
 * @param dr Datarate the channel has to carry.
 * @param freq Output: selected frequency.
 * @return 0 on success, -ENOENT if no channel matches.
 */
int lwan_fixed_select_in_range(const struct lwan_channel *ch, size_t count, size_t first,
			       size_t span, uint8_t dr, uint32_t *freq);

/**
 * @brief Pick the next Join-Request channel, sweeping the octet groups.
 *
 * Advances @p cur by one group per call whether or not the group yielded a
 * channel, so a plan the network has narrowed is still swept in order.
 *
 * @param plan Channel plan.
 * @param cur Join cursor, advanced in place.
 * @param ch Channel array.
 * @param count Number of channels.
 * @param dr_125 Datarate the 125 kHz groups use.
 * @param dr_500 Datarate the 500 kHz channels use.
 * @param freq Output: selected frequency.
 * @param dr Output: datarate for @p freq.
 * @return 0 on success, -ENOENT if a full sweep found no channel.
 */
int lwan_fixed_select_join_channel(const struct lwan_fixed_plan *plan,
				   struct lwan_fixed_join_cursor *cur,
				   const struct lwan_channel *ch, size_t count, uint8_t dr_125,
				   uint8_t dr_500, uint32_t *freq, uint8_t *dr);

#ifdef __cplusplus
}
#endif

#endif /* SUBSYS_LORAWAN_NATIVE_REGION_FIXED_PLAN_H_ */
