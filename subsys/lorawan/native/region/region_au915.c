/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * AU915-928 regional parameters from LoRaWAN Regional Parameters RP002-1.0.5,
 * section 3.8
 * https://resources.lora-alliance.org/technical-specifications/rp002-1-0-5-regional-parameters
 *
 * 3.8.6 leaves a 400 ms cap on an uplink's time on air to the local
 * regulations, so the uplink datarates come in two tables: the plain one for
 * a session the network has freed of the cap through TxParamSetupReq, and
 * the dwell time one, which the device assumes from boot and which has no
 * room for DR0 or DR1.
 */

#include <zephyr/kernel.h>

#include "fixed_plan.h"
#include <lorawan.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lorawan_native_au915, CONFIG_LORAWAN_LOG_LEVEL);

/* Uplink channel plan (3.8.2) */
#define AU915_UL_125_BASE_HZ  KHZ(915200)
#define AU915_UL_125_STEP_HZ  KHZ(200)
#define AU915_UL_125_CH_COUNT 64

#define AU915_UL_500_BASE_HZ  KHZ(915900)
#define AU915_UL_500_STEP_HZ  KHZ(1600)
#define AU915_UL_500_CH_COUNT 8

/* Downlink channel plan (3.8.2) and RX2 defaults (3.8.7) */
#define AU915_DL_BASE_HZ  KHZ(923300)
#define AU915_DL_STEP_HZ  KHZ(600)
#define AU915_DL_CH_COUNT 8

#define AU915_RX2_FREQ AU915_DL_BASE_HZ
#define AU915_RX2_DR   8 /* DR8 = SF12/500kHz, in the downlink datarate space */

/*
 * TXPower table (Table 42): index 0 is the 30 dBm EIRP the band allows by
 * default and each further index drops 2 dB, down to 2 dBm at index 14.
 */
#define AU915_MAX_POWER_DBM    30
#define AU915_MAX_TX_POWER_IDX 14

/*
 * Uplink datarates (Table 39), the DR0..DR6 set 3.8.3 lists as the minimum a
 * certified device implements. DR7 is LR-FHSS, which no Zephyr LoRa driver
 * offers, and DR9 and DR10 are SF6 and SF5, which 3.8.3 tells a device not to
 * use before the network asks for them; both also pull in downlink DR14 and
 * DR0.
 *
 * Maximum payloads are the repeater-compatible N values of Table 44.
 */
static const struct lwan_dr_params au915_ul_dr_table[] = {
	[0] = {.sf = SF_12, .bw = BW_125_KHZ, .max_payload = 51},
	[1] = {.sf = SF_11, .bw = BW_125_KHZ, .max_payload = 51},
	[2] = {.sf = SF_10, .bw = BW_125_KHZ, .max_payload = 51},
	[3] = {.sf = SF_9, .bw = BW_125_KHZ, .max_payload = 115},
	[4] = {.sf = SF_8, .bw = BW_125_KHZ, .max_payload = 222},
	[5] = {.sf = SF_7, .bw = BW_125_KHZ, .max_payload = 222},
	[6] = {.sf = SF_8, .bw = BW_500_KHZ, .max_payload = 222},
};

#define AU915_UL_DR_COUNT ARRAY_SIZE(au915_ul_dr_table)
#define AU915_UL_DR_MAX   (AU915_UL_DR_COUNT - 1)

/*
 * The same datarates under the 400 ms dwell time the band applies to its
 * uplink channels. Table 44 leaves DR0 and DR1 without a payload at all in
 * the UplinkDwellTime = 1 column, so the table starts at DR2 and the lookup
 * refuses anything below it. 3.8.2 tells a device to assume the limit is in
 * force from boot until a TxParamSetupReq says otherwise.
 */
#define AU915_UL_DWELL_DR_MIN 2

static const struct lwan_dr_params au915_ul_dwell_dr_table[] = {
	[0] = {.sf = SF_10, .bw = BW_125_KHZ, .max_payload = 11},
	[1] = {.sf = SF_9, .bw = BW_125_KHZ, .max_payload = 53},
	[2] = {.sf = SF_8, .bw = BW_125_KHZ, .max_payload = 125},
	[3] = {.sf = SF_7, .bw = BW_125_KHZ, .max_payload = 222},
	[4] = {.sf = SF_8, .bw = BW_500_KHZ, .max_payload = 222},
};

#define AU915_UL_DWELL_DR_MAX                                                                      \
	(AU915_UL_DWELL_DR_MIN + ARRAY_SIZE(au915_ul_dwell_dr_table) - 1)

/*
 * Downlink datarates (Table 40). The band numbers its downlink datarates in a
 * space of their own that starts at DR8, so the table is indexed by
 * dr - AU915_DL_DR_MIN rather than by dr.
 */
#define AU915_DL_DR_MIN 8

static const struct lwan_dr_params au915_dl_dr_table[] = {
	[0] = {.sf = SF_12, .bw = BW_500_KHZ, .max_payload = 53},
	[1] = {.sf = SF_11, .bw = BW_500_KHZ, .max_payload = 129},
	[2] = {.sf = SF_10, .bw = BW_500_KHZ, .max_payload = 222},
	[3] = {.sf = SF_9, .bw = BW_500_KHZ, .max_payload = 222},
	[4] = {.sf = SF_8, .bw = BW_500_KHZ, .max_payload = 222},
	[5] = {.sf = SF_7, .bw = BW_500_KHZ, .max_payload = 222},
};

#define AU915_DL_DR_COUNT ARRAY_SIZE(au915_dl_dr_table)
#define AU915_DL_DR_MAX   (AU915_DL_DR_MIN + AU915_DL_DR_COUNT - 1)

/* RX1 downlink datarate mapping (Table 46) */
#define AU915_MAX_RX1_DR_OFFSET 5

/* clang-format off */
static const uint8_t au915_rx1_dr[AU915_UL_DR_COUNT][AU915_MAX_RX1_DR_OFFSET + 1] = {
	[0] = {  8,  8,  8,  8,  8,  8 },
	[1] = {  9,  8,  8,  8,  8,  8 },
	[2] = { 10,  9,  8,  8,  8,  8 },
	[3] = { 11, 10,  9,  8,  8,  8 },
	[4] = { 12, 11, 10,  9,  8,  8 },
	[5] = { 13, 12, 11, 10,  9,  8 },
	[6] = { 13, 13, 12, 11, 10,  9 },
};
/* clang-format on */

/* Join-Request datarates (3.8.2): DR2 on the 125 kHz channels, DR6 on the 500 kHz ones */
#define AU915_JOIN_DR_125 2
#define AU915_JOIN_DR_500 6

static struct lwan_fixed_join_cursor au915_join_cursor;

static const struct lwan_fixed_plan au915_plan = {
	.ul_125_base_hz = AU915_UL_125_BASE_HZ,
	.ul_125_step_hz = AU915_UL_125_STEP_HZ,
	.ul_125_count = AU915_UL_125_CH_COUNT,
	.ul_500_base_hz = AU915_UL_500_BASE_HZ,
	.ul_500_step_hz = AU915_UL_500_STEP_HZ,
	.ul_500_count = AU915_UL_500_CH_COUNT,
	.dl_base_hz = AU915_DL_BASE_HZ,
	.dl_step_hz = AU915_DL_STEP_HZ,
	.dl_count = AU915_DL_CH_COUNT,
	.ul_125_min_dr = 0,
	.ul_125_max_dr = 5,
	.ul_500_dr = AU915_JOIN_DR_500,
};

/* Table 42: index 0 is the band's maximum EIRP and each further index drops 2 dB. */
static int8_t au915_power_dbm(uint8_t tx_power_idx)
{
	/* An index past the table steps the power down no further than its end. */
	uint8_t idx = MIN(tx_power_idx, AU915_MAX_TX_POWER_IDX);

	return AU915_MAX_POWER_DBM - (int8_t)(2 * idx);
}

static int au915_get_tx_params(uint8_t dr, uint8_t tx_power_idx, struct lwan_dr_params *p,
			       int8_t *power_dbm)
{
	if (dr > AU915_UL_DR_MAX) {
		return -EINVAL;
	}

	*p = au915_ul_dr_table[dr];
	*power_dbm = au915_power_dbm(tx_power_idx);
	return 0;
}

static int au915_get_dwell_tx_params(uint8_t dr, uint8_t tx_power_idx, struct lwan_dr_params *p,
				     int8_t *power_dbm)
{
	if (dr < AU915_UL_DWELL_DR_MIN || dr > AU915_UL_DWELL_DR_MAX) {
		return -EINVAL;
	}

	*p = au915_ul_dwell_dr_table[dr - AU915_UL_DWELL_DR_MIN];
	*power_dbm = au915_power_dbm(tx_power_idx);
	return 0;
}

static int au915_get_rx1_params(uint32_t tx_freq, uint8_t tx_dr, uint8_t offset, uint32_t *rx1_freq,
				struct lwan_dr_params *p)
{
	uint8_t rx1_dr;
	int ret;

	if (tx_dr > AU915_UL_DR_MAX || offset > AU915_MAX_RX1_DR_OFFSET) {
		return -EINVAL;
	}

	/* 3.8.7: RX1 channel = uplink channel modulo the eight downlink channels. */
	ret = lwan_fixed_rx1_frequency(&au915_plan, tx_freq, rx1_freq);
	if (ret != 0) {
		return ret;
	}

	rx1_dr = au915_rx1_dr[tx_dr][offset];

	*p = au915_dl_dr_table[rx1_dr - AU915_DL_DR_MIN];
	return 0;
}

static int au915_get_rx2_params(uint8_t dr, uint32_t *freq, struct lwan_dr_params *p)
{
	if (dr < AU915_DL_DR_MIN || dr > AU915_DL_DR_MAX) {
		return -EINVAL;
	}

	*freq = AU915_RX2_FREQ;
	*p = au915_dl_dr_table[dr - AU915_DL_DR_MIN];
	return 0;
}

static int au915_validate_dl_settings(uint8_t rx1_dr_offset, uint8_t rx2_datarate)
{
	if (rx1_dr_offset > AU915_MAX_RX1_DR_OFFSET) {
		return -EINVAL;
	}

	if (rx2_datarate < AU915_DL_DR_MIN || rx2_datarate > AU915_DL_DR_MAX) {
		return -EINVAL;
	}

	return 0;
}

static int au915_validate_dr(uint8_t dr)
{
	if (dr > AU915_UL_DR_MAX) {
		return -EINVAL;
	}

	return 0;
}

static int au915_validate_tx_power(uint8_t tx_power_idx)
{
	if (tx_power_idx > AU915_MAX_TX_POWER_IDX) {
		return -EINVAL;
	}

	return 0;
}

static int au915_get_default_channels(struct lwan_channel *ch, size_t *count)
{
	return lwan_fixed_get_default_channels(&au915_plan, ch, count);
}

static int au915_apply_cflist(const uint8_t cflist[LWAN_CFLIST_SIZE], struct lwan_channel *ch,
			      size_t *count)
{
	return lwan_fixed_apply_cflist(&au915_plan, cflist, ch, count);
}

static int au915_apply_adr_channel_mask(struct lwan_channel *ch, size_t count, uint8_t ch_mask_cntl,
					uint16_t ch_mask)
{
	return lwan_fixed_apply_adr_channel_mask(&au915_plan, ch, count, ch_mask_cntl, ch_mask);
}

static int au915_select_data_channel(const struct lwan_channel *ch, size_t count, uint8_t dr,
				     uint32_t *freq, int32_t *delay_ms)
{
	ARG_UNUSED(delay_ms);

	/*
	 * The band has no duty cycle and no listen-before-talk, so a channel
	 * that matches the datarate is always free and -ENOBUFS never
	 * arises.
	 */
	return lwan_fixed_select_in_range(ch, count, 0, count, dr, freq);
}

static int au915_select_join_channel(const struct lwan_channel *ch, size_t count, uint32_t *freq,
				     uint8_t *dr, int32_t *delay_ms)
{
	ARG_UNUSED(delay_ms);

	return lwan_fixed_select_join_channel(&au915_plan, &au915_join_cursor, ch, count,
					      AU915_JOIN_DR_125, AU915_JOIN_DR_500, freq, dr);
}

static void au915_record_tx(uint32_t freq, uint32_t airtime_ms)
{
	ARG_UNUSED(freq);
	ARG_UNUSED(airtime_ms);

	/* The band holds no duty cycle, so a transmission buys no off-time. */
}

const struct lwan_region_ops au915_ops = {
	.default_rx2_dr = AU915_RX2_DR,
	.default_ul_dwell_time = true,
	.get_default_channels = au915_get_default_channels,
	.get_tx_params = au915_get_tx_params,
	.get_dwell_tx_params = au915_get_dwell_tx_params,
	.get_rx1_params = au915_get_rx1_params,
	.get_rx2_params = au915_get_rx2_params,
	.validate_dl_settings = au915_validate_dl_settings,
	.apply_cflist = au915_apply_cflist,
	.validate_dr = au915_validate_dr,
	.validate_tx_power = au915_validate_tx_power,
	.apply_adr_channel_mask = au915_apply_adr_channel_mask,
	.select_join_channel = au915_select_join_channel,
	.select_data_channel = au915_select_data_channel,
	.record_tx = au915_record_tx,
};
