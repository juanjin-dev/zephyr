/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "lorawan.h"
#include "mac/mac_commands.h"
#include "region/region.h"

/* TxParamSetupReq, and the answer it is owed */
#define CID_TX_PARAM_SETUP	0x09
#define TX_PARAM_DL_DWELL	BIT(5)
#define TX_PARAM_UL_DWELL	BIT(4)

/* A region that caps the dwell time, and one that does not. The lookups
 * are never called here: only their presence decides whether the command
 * is taken.
 */
static int stub_tx_params(uint8_t dr, uint8_t tx_power_idx, struct lwan_dr_params *p,
			  int8_t *power_dbm)
{
	ARG_UNUSED(dr);
	ARG_UNUSED(tx_power_idx);
	ARG_UNUSED(p);
	ARG_UNUSED(power_dbm);

	return 0;
}

static int stub_rx1_params(uint32_t tx_freq, uint8_t tx_dr, uint8_t offset, uint32_t *rx1_freq,
			   struct lwan_dr_params *p)
{
	ARG_UNUSED(tx_freq);
	ARG_UNUSED(tx_dr);
	ARG_UNUSED(offset);
	ARG_UNUSED(rx1_freq);
	ARG_UNUSED(p);

	return 0;
}

static const struct lwan_region_ops capped_ops = {
	.get_tx_params = stub_tx_params,
	.get_rx1_params = stub_rx1_params,
	.get_dwell_tx_params = stub_tx_params,
	.get_dwell_rx1_params = stub_rx1_params,
};

static const struct lwan_region_ops uncapped_ops = {
	.get_tx_params = stub_tx_params,
	.get_rx1_params = stub_rx1_params,
};

static struct lwan_ctx ctx;

static void feed(uint8_t param)
{
	uint8_t fopts[] = {CID_TX_PARAM_SETUP, param};

	mac_cmd_process_dl_fopts(&ctx, fopts, sizeof(fopts));
}

static void before(void *unused)
{
	ARG_UNUSED(unused);

	memset(&ctx, 0, sizeof(ctx));
	ctx.region = &capped_ops;
}

ZTEST(lorawan_native_mac, test_tx_param_setup_sets_dwell)
{
	feed(TX_PARAM_UL_DWELL | TX_PARAM_DL_DWELL | 5U);

	zassert_true(ctx.mac.ul_dwell_time);
	zassert_true(ctx.mac.dl_dwell_time);
	zassert_equal(ctx.mac.max_eirp_dbm, 16);
	zassert_true(ctx.mac.tx_param_setup_ans_pending);
}

ZTEST(lorawan_native_mac, test_tx_param_setup_clears_dwell)
{
	feed(TX_PARAM_UL_DWELL | TX_PARAM_DL_DWELL | 5U);
	feed(0U);

	zassert_false(ctx.mac.ul_dwell_time);
	zassert_false(ctx.mac.dl_dwell_time);
}

ZTEST(lorawan_native_mac, test_tx_param_setup_eirp_table)
{
	/* The index reaches both ends of the TS001 MaxEIRP table. */
	feed(0U);
	zassert_equal(ctx.mac.max_eirp_dbm, 8);

	feed(15U);
	zassert_equal(ctx.mac.max_eirp_dbm, 36);
}

ZTEST(lorawan_native_mac, test_tx_param_setup_uplink_only_dwell)
{
	feed(TX_PARAM_UL_DWELL | 5U);

	zassert_true(ctx.mac.ul_dwell_time);
	zassert_false(ctx.mac.dl_dwell_time);
}

ZTEST(lorawan_native_mac, test_uncapped_region_ignores_the_command)
{
	ctx.region = &uncapped_ops;

	feed(TX_PARAM_UL_DWELL | TX_PARAM_DL_DWELL | 15U);

	zassert_false(ctx.mac.ul_dwell_time);
	zassert_false(ctx.mac.dl_dwell_time);
	zassert_equal(ctx.mac.max_eirp_dbm, 0);
	zassert_false(ctx.mac.tx_param_setup_ans_pending, "an unanswered command was answered");
}

ZTEST(lorawan_native_mac, test_answer_rides_the_next_uplink)
{
	uint8_t fopts[LWAN_MAX_FOPTS_LEN];
	size_t len;

	feed(TX_PARAM_UL_DWELL | 5U);
	zassert_equal(mac_cmd_next_ul_fopts_len(&ctx), 1);

	len = mac_cmd_build_ul_fopts(&ctx, fopts, sizeof(fopts));
	zassert_equal(len, 1);
	zassert_equal(fopts[0], CID_TX_PARAM_SETUP);
}

ZTEST(lorawan_native_mac, test_answer_survives_a_failed_uplink)
{
	uint8_t fopts[LWAN_MAX_FOPTS_LEN];

	feed(TX_PARAM_UL_DWELL | 5U);
	(void)mac_cmd_build_ul_fopts(&ctx, fopts, sizeof(fopts));

	/* No commit: the transmission failed, so the answer is still owed. */
	zassert_true(ctx.mac.tx_param_setup_ans_pending);
	zassert_equal(mac_cmd_next_ul_fopts_len(&ctx), 1);
}

ZTEST(lorawan_native_mac, test_answer_is_dropped_once_sent)
{
	uint8_t fopts[LWAN_MAX_FOPTS_LEN];

	feed(TX_PARAM_UL_DWELL | 5U);
	(void)mac_cmd_build_ul_fopts(&ctx, fopts, sizeof(fopts));
	mac_cmd_commit_ul_fopts(&ctx);

	zassert_false(ctx.mac.tx_param_setup_ans_pending);
	zassert_equal(mac_cmd_next_ul_fopts_len(&ctx), 0);
}

ZTEST(lorawan_native_mac, test_answer_goes_out_before_a_request)
{
	uint8_t fopts[LWAN_MAX_FOPTS_LEN];
	size_t len;

	ctx.mac.link_check_pending = true;
	feed(TX_PARAM_UL_DWELL | 5U);

	len = mac_cmd_build_ul_fopts(&ctx, fopts, sizeof(fopts));
	zassert_equal(len, 2);
	zassert_equal(fopts[0], CID_TX_PARAM_SETUP, "the answer did not go first");
}

ZTEST_SUITE(lorawan_native_mac, NULL, NULL, before, NULL, NULL);
