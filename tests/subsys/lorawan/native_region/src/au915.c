/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless Technology Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include "region.h"

/* Uplink channel plan (RP002-1.0.5, 3.8.2) */
#define AU915_UL_125_BASE_HZ  915200000U
#define AU915_UL_125_STEP_HZ  200000U
#define AU915_UL_125_CH_COUNT 64

#define AU915_UL_500_BASE_HZ  915900000U
#define AU915_UL_500_STEP_HZ  1600000U
#define AU915_UL_500_CH_COUNT 8

#define AU915_UL_CH_COUNT (AU915_UL_125_CH_COUNT + AU915_UL_500_CH_COUNT)

/* Downlink channel plan (3.8.2) and RX2 defaults (3.8.7) */
#define AU915_DL_BASE_HZ  923300000U
#define AU915_DL_STEP_HZ  600000U
#define AU915_DL_CH_COUNT 8
#define AU915_RX2_DR      8

#define AU915_UL_DR_MAX         6
#define AU915_UL_DWELL_DR_MIN   2
#define AU915_DL_DR_MIN         8
#define AU915_DL_DR_MAX         13
#define AU915_MAX_RX1_DR_OFFSET 5
#define AU915_MAX_TX_POWER_IDX  14
#define AU915_MAX_POWER_DBM     30

#define AU915_JOIN_DR_125 2
#define AU915_JOIN_DR_500 6

/* A datarate only the 125 kHz channels carry, for the checks that need one */
#define AU915_UL_DR_125 2

#define CH_MASK_CNTL_BANKS   5
#define CH_MASK_CNTL_ALL_ON  6
#define CH_MASK_CNTL_ALL_OFF 7

static const struct lwan_region_ops *au915;
static struct lwan_channel ch[CONFIG_LORAWAN_NATIVE_MAX_CHANNELS];
static size_t ch_count;

static void reset_channels(void)
{
	memset(ch, 0, sizeof(ch));
	ch_count = ARRAY_SIZE(ch);
	zassert_ok(au915->get_default_channels(ch, &ch_count));
}

static size_t enabled_count(void)
{
	size_t n = 0;

	for (size_t i = 0; i < ARRAY_SIZE(ch); i++) {
		if (ch[i].enabled) {
			n++;
		}
	}

	return n;
}

static uint32_t channel_freq(size_t idx)
{
	if (idx < AU915_UL_125_CH_COUNT) {
		return AU915_UL_125_BASE_HZ + (uint32_t)idx * AU915_UL_125_STEP_HZ;
	}

	return AU915_UL_500_BASE_HZ +
	       (uint32_t)(idx - AU915_UL_125_CH_COUNT) * AU915_UL_500_STEP_HZ;
}

static void *suite_setup(void)
{
	au915 = lwan_region_get(LORAWAN_REGION_AU915);
	zassume_not_null(au915);

	return NULL;
}

/* 3.8.2 asks for at least 72 channels, all enabled after a reset. */
ZTEST(lorawan_au915_region, test_default_channels)
{
	reset_channels();

	zassert_equal(ch_count, AU915_UL_CH_COUNT);
	zassert_equal(enabled_count(), AU915_UL_CH_COUNT);

	for (size_t i = 0; i < AU915_UL_CH_COUNT; i++) {
		zassert_equal(ch[i].frequency, channel_freq(i), "channel %zu", i);
	}

	/* The plan ends where 3.8.2 says it does. */
	zassert_equal(ch[AU915_UL_125_CH_COUNT - 1].frequency, 927800000U);
	zassert_equal(ch[AU915_UL_CH_COUNT - 1].frequency, 927100000U);
}

/* The 125 kHz channels carry DR0 to DR5 and the 500 kHz ones DR6 alone. */
ZTEST(lorawan_au915_region, test_default_channel_datarates)
{
	reset_channels();

	for (size_t i = 0; i < AU915_UL_125_CH_COUNT; i++) {
		zassert_equal(ch[i].min_dr, 0, "channel %zu", i);
		zassert_equal(ch[i].max_dr, 5, "channel %zu", i);
	}

	for (size_t i = AU915_UL_125_CH_COUNT; i < AU915_UL_CH_COUNT; i++) {
		zassert_equal(ch[i].min_dr, AU915_JOIN_DR_500, "channel %zu", i);
		zassert_equal(ch[i].max_dr, AU915_JOIN_DR_500, "channel %zu", i);
	}
}

ZTEST(lorawan_au915_region, test_default_channels_need_room)
{
	size_t count = AU915_UL_CH_COUNT - 1;

	zassert_equal(au915->get_default_channels(ch, &count), -ENOMEM);
}

/* Uplink datarates (Table 39) with the payloads of Table 44 */
ZTEST(lorawan_au915_region, test_tx_params)
{
	static const enum lora_datarate sf[] = {SF_12, SF_11, SF_10, SF_9, SF_8, SF_7, SF_8};
	static const enum lora_signal_bandwidth bw[] = {BW_125_KHZ, BW_125_KHZ, BW_125_KHZ,
							BW_125_KHZ, BW_125_KHZ, BW_125_KHZ,
							BW_500_KHZ};
	static const uint8_t payload[] = {51, 51, 51, 115, 222, 222, 222};
	struct lwan_dr_params p;
	int8_t power;

	for (uint8_t dr = 0; dr <= AU915_UL_DR_MAX; dr++) {
		zassert_ok(au915->get_tx_params(dr, 0, &p, &power), "DR%u", dr);
		zassert_equal(p.sf, sf[dr], "DR%u", dr);
		zassert_equal(p.bw, bw[dr], "DR%u", dr);
		zassert_equal(p.max_payload, payload[dr], "DR%u", dr);
		zassert_equal(power, AU915_MAX_POWER_DBM, "DR%u", dr);
	}
}

/*
 * The same datarates under the 400 ms dwell time, which 3.8.2 makes the state
 * a device assumes from boot. Table 44 leaves DR0 and DR1 without a payload
 * in that column, so the lookup refuses them.
 */
ZTEST(lorawan_au915_region, test_dwell_tx_params)
{
	static const enum lora_datarate sf[] = {SF_10, SF_9, SF_8, SF_7, SF_8};
	static const enum lora_signal_bandwidth bw[] = {BW_125_KHZ, BW_125_KHZ, BW_125_KHZ,
							BW_125_KHZ, BW_500_KHZ};
	static const uint8_t payload[] = {11, 53, 125, 222, 222};
	struct lwan_dr_params p;
	int8_t power;

	zassert_not_null(au915->get_dwell_tx_params, "the band caps its uplink dwell time");
	zassert_true(au915->default_ul_dwell_time, "3.8.2 makes the cap the boot state");

	for (uint8_t dr = AU915_UL_DWELL_DR_MIN; dr <= AU915_UL_DR_MAX; dr++) {
		size_t i = dr - AU915_UL_DWELL_DR_MIN;

		zassert_ok(au915->get_dwell_tx_params(dr, 0, &p, &power), "DR%u", dr);
		zassert_equal(p.sf, sf[i], "DR%u", dr);
		zassert_equal(p.bw, bw[i], "DR%u", dr);
		zassert_equal(p.max_payload, payload[i], "DR%u", dr);
	}

	for (uint8_t dr = 0; dr < AU915_UL_DWELL_DR_MIN; dr++) {
		zassert_equal(au915->get_dwell_tx_params(dr, 0, &p, &power), -EINVAL, "DR%u", dr);
	}
}

/*
 * 3.8.2 asks for DownlinkDwellTime 0 always, the downlink channels being
 * 500 kHz wide with no limit on them, so there is no dwell time variant of
 * the RX1 lookup to route to.
 */
ZTEST(lorawan_au915_region, test_no_downlink_dwell_time)
{
	zassert_is_null(au915->get_dwell_rx1_params);
}

/* DR7 is LR-FHSS and DR9 and DR10 are SF6 and SF5, none of them carried. */
ZTEST(lorawan_au915_region, test_unusable_uplink_datarates_rejected)
{
	struct lwan_dr_params p;
	int8_t power;

	for (uint8_t dr = 0; dr <= AU915_UL_DR_MAX; dr++) {
		zassert_ok(au915->validate_dr(dr), "DR%u", dr);
	}

	for (uint8_t dr = AU915_UL_DR_MAX + 1; dr <= 15; dr++) {
		zassert_equal(au915->get_tx_params(dr, 0, &p, &power), -EINVAL, "DR%u", dr);
		zassert_equal(au915->get_dwell_tx_params(dr, 0, &p, &power), -EINVAL, "DR%u", dr);
		zassert_equal(au915->validate_dr(dr), -EINVAL, "DR%u", dr);
	}
}

/* TXPower: 30 dBm EIRP at index 0, 2 dB per step, 2 dBm at index 14 (Table 42) */
ZTEST(lorawan_au915_region, test_tx_power_table)
{
	struct lwan_dr_params p;
	int8_t power;

	for (uint8_t idx = 0; idx <= AU915_MAX_TX_POWER_IDX; idx++) {
		zassert_ok(au915->validate_tx_power(idx), "index %u", idx);
		zassert_ok(au915->get_tx_params(AU915_UL_DR_125, idx, &p, &power), "index %u", idx);
		zassert_equal(power, AU915_MAX_POWER_DBM - 2 * idx, "index %u", idx);
	}

	zassert_equal(au915->validate_tx_power(AU915_MAX_TX_POWER_IDX + 1), -EINVAL);
}

/* 3.8.7 makes RX1 a function of the uplink channel alone. */
ZTEST(lorawan_au915_region, test_rx1_frequency_wraps_every_eight_channels)
{
	for (size_t i = 0; i < AU915_UL_CH_COUNT; i++) {
		uint32_t expect =
			AU915_DL_BASE_HZ + (uint32_t)(i % AU915_DL_CH_COUNT) * AU915_DL_STEP_HZ;

		for (uint8_t dr = 0; dr <= AU915_UL_DR_MAX; dr++) {
			for (uint8_t off = 0; off <= AU915_MAX_RX1_DR_OFFSET; off++) {
				struct lwan_dr_params p;
				uint32_t freq;

				zassert_ok(
					au915->get_rx1_params(channel_freq(i), dr, off, &freq, &p),
					"channel %zu DR%u offset %u", i, dr, off);
				zassert_equal(freq, expect, "channel %zu DR%u offset %u", i, dr,
					      off);
			}
		}
	}
}

ZTEST(lorawan_au915_region, test_rx1_rejects_frequency_outside_the_plan)
{
	struct lwan_dr_params p;
	uint32_t freq;

	/* Between two 125 kHz centres. */
	zassert_equal(
		au915->get_rx1_params(AU915_UL_125_BASE_HZ + 100000, AU915_UL_DR_125, 0, &freq, &p),
		-EINVAL);
	/* One step past the last 125 kHz channel, and not a 500 kHz centre. */
	zassert_equal(au915->get_rx1_params(AU915_UL_125_BASE_HZ +
						    AU915_UL_125_CH_COUNT * AU915_UL_125_STEP_HZ,
					    AU915_UL_DR_125, 0, &freq, &p),
		      -EINVAL);
	/* The US902-928 plan, which starts below this one. */
	zassert_equal(au915->get_rx1_params(902300000U, AU915_UL_DR_125, 0, &freq, &p), -EINVAL);
}

/* Table 46 */
ZTEST(lorawan_au915_region, test_rx1_datarate_mapping)
{
	/* clang-format off */
	static const uint8_t expect[][AU915_MAX_RX1_DR_OFFSET + 1] = {
		{  8,  8,  8,  8,  8,  8 },
		{  9,  8,  8,  8,  8,  8 },
		{ 10,  9,  8,  8,  8,  8 },
		{ 11, 10,  9,  8,  8,  8 },
		{ 12, 11, 10,  9,  8,  8 },
		{ 13, 12, 11, 10,  9,  8 },
		{ 13, 13, 12, 11, 10,  9 },
	};
	/* clang-format on */
	struct lwan_dr_params rx1;
	struct lwan_dr_params rx2;
	uint32_t freq;

	for (uint8_t dr = 0; dr <= AU915_UL_DR_MAX; dr++) {
		for (uint8_t off = 0; off <= AU915_MAX_RX1_DR_OFFSET; off++) {
			zassert_ok(
				au915->get_rx1_params(AU915_UL_125_BASE_HZ, dr, off, &freq, &rx1),
				"DR%u offset %u", dr, off);

			/*
			 * The table gives a downlink datarate, so compare
			 * against what RX2 reports for that same index.
			 */
			zassert_ok(au915->get_rx2_params(expect[dr][off], &freq, &rx2));
			zassert_equal(rx1.sf, rx2.sf, "DR%u offset %u", dr, off);
			zassert_equal(rx1.bw, rx2.bw, "DR%u offset %u", dr, off);
			zassert_equal(rx1.max_payload, rx2.max_payload, "DR%u offset %u", dr, off);
		}
	}
}

/* 3.8.7 reserves RX1DROffset 6 and 7 and tells the device not to accept them. */
ZTEST(lorawan_au915_region, test_rx1_rejects_reserved_offsets)
{
	struct lwan_dr_params p;
	uint32_t freq;

	for (uint8_t off = AU915_MAX_RX1_DR_OFFSET + 1; off <= 7; off++) {
		zassert_equal(au915->get_rx1_params(AU915_UL_125_BASE_HZ, AU915_UL_DR_125, off,
						    &freq, &p),
			      -EINVAL, "offset %u", off);
		zassert_equal(au915->validate_dl_settings(off, AU915_RX2_DR), -EINVAL, "offset %u",
			      off);
	}
}

/* Downlink datarates (Table 40) */
ZTEST(lorawan_au915_region, test_rx2_params)
{
	static const enum lora_datarate expect[] = {SF_12, SF_11, SF_10, SF_9, SF_8, SF_7};
	static const uint8_t payload[] = {53, 129, 222, 222, 222, 222};
	struct lwan_dr_params p;
	uint32_t freq;

	for (uint8_t dr = AU915_DL_DR_MIN; dr <= AU915_DL_DR_MAX; dr++) {
		zassert_ok(au915->get_rx2_params(dr, &freq, &p), "DR%u", dr);
		zassert_equal(freq, AU915_DL_BASE_HZ, "DR%u", dr);
		zassert_equal(p.sf, expect[dr - AU915_DL_DR_MIN], "DR%u", dr);
		zassert_equal(p.bw, BW_500_KHZ, "DR%u", dr);
		zassert_equal(p.max_payload, payload[dr - AU915_DL_DR_MIN], "DR%u", dr);
	}
}

ZTEST(lorawan_au915_region, test_rx2_rejects_uplink_datarate_space)
{
	struct lwan_dr_params p;
	uint32_t freq;

	for (uint8_t dr = 0; dr < AU915_DL_DR_MIN; dr++) {
		zassert_equal(au915->get_rx2_params(dr, &freq, &p), -EINVAL, "DR%u", dr);
		zassert_equal(au915->validate_dl_settings(0, dr), -EINVAL, "DR%u", dr);
	}

	for (uint8_t dr = AU915_DL_DR_MAX + 1; dr <= 15; dr++) {
		zassert_equal(au915->get_rx2_params(dr, &freq, &p), -EINVAL, "DR%u", dr);
		zassert_equal(au915->validate_dl_settings(0, dr), -EINVAL, "DR%u", dr);
	}
}

ZTEST(lorawan_au915_region, test_default_rx2_datarate)
{
	zassert_equal(au915->default_rx2_dr, AU915_RX2_DR);
}

/* CFList type 1 (3.8.4): five ChMask fields covering channels 0 to 79 */
static void apply_cflist(uint16_t m0, uint16_t m1, uint16_t m2, uint16_t m3, uint16_t m4)
{
	uint8_t cflist[LWAN_CFLIST_SIZE] = {0};

	sys_put_le16(m0, &cflist[0]);
	sys_put_le16(m1, &cflist[2]);
	sys_put_le16(m2, &cflist[4]);
	sys_put_le16(m3, &cflist[6]);
	sys_put_le16(m4, &cflist[8]);
	cflist[LWAN_CFLIST_TYPE_OFFSET] = LWAN_CFLIST_TYPE_CH_MASK;

	zassert_ok(au915->apply_cflist(cflist, ch, &ch_count));
}

/* A network running sub-band 2 leaves channels 8 to 15 and 65 enabled. */
ZTEST(lorawan_au915_region, test_cflist_enables_the_named_channels)
{
	reset_channels();

	apply_cflist(0xff00, 0, 0, 0, BIT(1));

	for (size_t i = 0; i < AU915_UL_CH_COUNT; i++) {
		bool expect = (i >= 8 && i < 16) || i == AU915_UL_125_CH_COUNT + 1;

		zassert_equal(ch[i].enabled, expect, "channel %zu", i);
	}
	zassert_equal(enabled_count(), 9);
}

ZTEST(lorawan_au915_region, test_cflist_leaving_nothing_enabled_is_ignored)
{
	reset_channels();

	apply_cflist(0, 0, 0, 0, 0);

	zassert_equal(enabled_count(), AU915_UL_CH_COUNT);
}

ZTEST(lorawan_au915_region, test_cflist_of_another_type_is_ignored)
{
	uint8_t cflist[LWAN_CFLIST_SIZE] = {0};

	reset_channels();

	cflist[LWAN_CFLIST_TYPE_OFFSET] = LWAN_CFLIST_TYPE_FREQ;
	zassert_ok(au915->apply_cflist(cflist, ch, &ch_count));

	zassert_equal(enabled_count(), AU915_UL_CH_COUNT);
}

/* ChMaskCntl 5: bit k pairs the bank at k * 8 with the 500 kHz channel 64 + k. */
ZTEST(lorawan_au915_region, test_adr_mask_banks_pair_each_octet_with_a_500_khz_channel)
{
	reset_channels();

	zassert_ok(au915->apply_adr_channel_mask(ch, ch_count, CH_MASK_CNTL_BANKS, BIT(2)));

	for (size_t i = 0; i < AU915_UL_125_CH_COUNT; i++) {
		zassert_equal(ch[i].enabled, (i >= 16 && i < 24), "channel %zu", i);
	}

	zassert_true(ch[AU915_UL_125_CH_COUNT + 2].enabled);
	zassert_equal(enabled_count(), 9);
}

/* Table 43 reaches channel 79, and this device stores up to 71. */
ZTEST(lorawan_au915_region, test_adr_mask_rejects_bits_above_channel_71)
{
	static const uint8_t cntls[] = {4, CH_MASK_CNTL_ALL_ON, CH_MASK_CNTL_ALL_OFF,
					CH_MASK_CNTL_BANKS};

	reset_channels();

	ARRAY_FOR_EACH(cntls, i) {
		for (uint8_t bit = 8; bit < 16; bit++) {
			zassert_equal(au915->apply_adr_channel_mask(ch, ch_count, cntls[i],
								    BIT(0) | BIT(bit)),
				      -EINVAL, "cntl %u bit %u", cntls[i], bit);
		}
	}

	zassert_equal(enabled_count(), AU915_UL_CH_COUNT);
}

ZTEST(lorawan_au915_region, test_adr_mask_leaving_nothing_enabled_is_refused)
{
	reset_channels();

	zassert_equal(au915->apply_adr_channel_mask(ch, ch_count, CH_MASK_CNTL_ALL_OFF, 0x0000),
		      -EINVAL);
	zassert_equal(enabled_count(), AU915_UL_CH_COUNT);
}

ZTEST(lorawan_au915_region, test_adr_mask_rejects_reserved_control_value)
{
	reset_channels();

	for (uint8_t cntl = CH_MASK_CNTL_ALL_OFF + 1; cntl < 16; cntl++) {
		zassert_equal(au915->apply_adr_channel_mask(ch, ch_count, cntl, 0xffff), -EINVAL,
			      "cntl %u", cntl);
	}
}

/* 3.8.2: DR2 on a 125 kHz channel, DR6 on a 500 kHz one, a new group per call. */
ZTEST(lorawan_au915_region, test_join_channel_sweeps_the_octet_groups)
{
	bool group_seen[AU915_UL_125_CH_COUNT / 8] = {false};
	uint32_t freq;
	int32_t delay_ms;
	uint8_t dr;

	reset_channels();

	for (size_t i = 0; i < AU915_UL_125_CH_COUNT / 8; i++) {
		zassert_ok(au915->select_join_channel(ch, ch_count, &freq, &dr, &delay_ms));
		zassert_equal(dr, AU915_JOIN_DR_125, "pass %zu", i);
		zassert_true(freq >= AU915_UL_125_BASE_HZ, "pass %zu", i);

		size_t idx = (freq - AU915_UL_125_BASE_HZ) / AU915_UL_125_STEP_HZ;

		zassert_false(group_seen[idx / 8], "group %zu drawn twice", idx / 8);
		group_seen[idx / 8] = true;
	}

	/* The pass closes on one 500 kHz channel, at the datarate they carry. */
	zassert_ok(au915->select_join_channel(ch, ch_count, &freq, &dr, &delay_ms));
	zassert_equal(dr, AU915_JOIN_DR_500);
	zassert_true(freq >= AU915_UL_500_BASE_HZ);
}

ZTEST(lorawan_au915_region, test_join_channel_without_an_enabled_channel)
{
	uint32_t freq;
	int32_t delay_ms;
	uint8_t dr;

	reset_channels();

	for (size_t i = 0; i < AU915_UL_CH_COUNT; i++) {
		ch[i].enabled = false;
	}

	zassert_equal(au915->select_join_channel(ch, ch_count, &freq, &dr, &delay_ms), -ENOENT);
}

ZTEST(lorawan_au915_region, test_data_channel_selection_follows_the_datarate)
{
	uint32_t freq;
	int32_t delay_ms;

	reset_channels();

	/* DR6 only lives on the 500 kHz channels. */
	for (int i = 0; i < 32; i++) {
		zassert_ok(au915->select_data_channel(ch, ch_count, AU915_JOIN_DR_500, &freq,
						      &delay_ms));
		zassert_true(freq >= AU915_UL_500_BASE_HZ, "picked %u", freq);
	}

	/* DR2 only lives on the 125 kHz ones. */
	for (int i = 0; i < 32; i++) {
		zassert_ok(au915->select_data_channel(ch, ch_count, AU915_UL_DR_125, &freq,
						      &delay_ms));

		size_t idx = (freq - AU915_UL_125_BASE_HZ) / AU915_UL_125_STEP_HZ;

		zassert_true(idx < AU915_UL_125_CH_COUNT, "picked %u", freq);
	}
}

/* The band keeps the air clear by hopping, so nothing gates a transmission. */
ZTEST(lorawan_au915_region, test_no_duty_cycle_or_lbt)
{
	uint32_t freq;
	int32_t delay_ms;

	zassert_is_null(au915->get_lbt_params);
	zassert_is_null(au915->clamp_tx_power);
	zassert_equal(au915->max_lbt_retries, 0);

	reset_channels();

	for (int i = 0; i < 16; i++) {
		zassert_ok(au915->select_data_channel(ch, ch_count, AU915_UL_DR_125, &freq,
						      &delay_ms));
		au915->record_tx(freq, 400);
	}
}

ZTEST_SUITE(lorawan_au915_region, NULL, suite_setup, NULL, NULL, NULL);
