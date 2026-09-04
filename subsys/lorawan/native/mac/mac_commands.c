/*
 * Copyright (c) 2026 Carlo Caione <ccaione@baylibre.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include <lorawan.h>
#include "mac_commands.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(lorawan_native_mac, CONFIG_LORAWAN_LOG_LEVEL);

/* LinkCheckReq on-the-wire length: CID only, no payload */
#define MAC_CMD_LINK_CHECK_REQ_LEN	1

/* LinkADRAns on-the-wire length: CID plus a status byte */
#define MAC_CMD_LINK_ADR_ANS_LEN	2

/* Downlink command payload lengths in bytes, excluding the CID */
struct mac_dl_cmd_info {
	uint8_t cid;
	uint8_t payload_len;
};

static const struct mac_dl_cmd_info dl_cmd_table[] = {
	{ MAC_CMD_RESET,		1 },
	{ MAC_CMD_LINK_CHECK,		2 },	/* Margin + GwCnt */
	{ MAC_CMD_LINK_ADR,		4 },
	{ MAC_CMD_DUTY_CYCLE,		1 },
	{ MAC_CMD_RX_PARAM_SETUP,	4 },
	{ MAC_CMD_DEV_STATUS,		0 },
	{ MAC_CMD_NEW_CHANNEL,		5 },
	{ MAC_CMD_RX_TIMING_SETUP,	1 },
	{ MAC_CMD_TX_PARAM_SETUP,	1 },
	{ MAC_CMD_DL_CHANNEL,		4 },
	{ MAC_CMD_REKEY,		1 },
	{ MAC_CMD_ADR_PARAM_SETUP,	1 },
	{ MAC_CMD_DEVICE_TIME,		5 },
	{ MAC_CMD_FORCE_REJOIN,		2 },
	{ MAC_CMD_REJOIN_PARAM,		1 },
	{ MAC_CMD_PING_SLOT_INFO,	0 },	/* PingSlotInfoAns is empty */
	{ MAC_CMD_PING_SLOT_CHAN,	4 },
	{ MAC_CMD_BEACON_TIMING,	3 },
	{ MAC_CMD_BEACON_FREQ,		3 },
	{ MAC_CMD_DEVICE_MODE,		1 },
};

static lorawan_link_check_ans_cb_t link_check_cb;

void mac_cmd_set_link_check_cb(lorawan_link_check_ans_cb_t cb)
{
	link_check_cb = cb;
}

static void mac_cmd_handle_link_check_ans(struct lwan_ctx *ctx,
					  uint8_t margin, uint8_t gw_cnt)
{
	ctx->mac.link_check_margin = margin;
	ctx->mac.link_check_gw_cnt = gw_cnt;
	ctx->mac.link_check_ans_valid = true;

	LOG_INF("LinkCheckAns: margin=%u dB, gateways=%u", margin, gw_cnt);
}

void mac_cmd_deliver_link_check_ans(struct lwan_ctx *ctx)
{
	uint8_t margin;
	uint8_t gw_cnt;

	if (!ctx->mac.link_check_ans_valid) {
		return;
	}

	margin = ctx->mac.link_check_margin;
	gw_cnt = ctx->mac.link_check_gw_cnt;
	ctx->mac.link_check_ans_valid = false;

	if (link_check_cb != NULL) {
		link_check_cb(margin, gw_cnt);
	}
}

bool mac_cmd_has_pending_delivery(struct lwan_ctx *ctx)
{
	return ctx->mac.link_check_ans_valid;
}

/* LinkADRReq fields (TS001 5.2) */
#define LINK_ADR_DR_MASK		GENMASK(7, 4)
#define LINK_ADR_TX_POWER_MASK		GENMASK(3, 0)
#define LINK_ADR_CH_MASK_CNTL_MASK	GENMASK(6, 4)

/* LinkADRAns status bits */
#define LINK_ADR_ANS_CH_MASK_ACK	BIT(0)
#define LINK_ADR_ANS_DR_ACK		BIT(1)
#define LINK_ADR_ANS_POWER_ACK		BIT(2)
#define LINK_ADR_ANS_ALL		(LINK_ADR_ANS_CH_MASK_ACK | \
					 LINK_ADR_ANS_DR_ACK | \
					 LINK_ADR_ANS_POWER_ACK)

/*
 * A datarate or power of 15 asks to keep whatever is in use, which is always
 * answerable (TS001 5.2).
 */
#define LINK_ADR_KEEP_CURRENT		0x0F

static void mac_cmd_queue_link_adr_ans(struct lwan_ctx *ctx, uint8_t status)
{
	if (ctx->mac.link_adr_ans_count >= LWAN_MAX_LINK_ADR_ANS) {
		LOG_WRN("LinkADRAns queue full, dropping answer");
		return;
	}

	ctx->mac.link_adr_ans[ctx->mac.link_adr_ans_count++] = status;
}

static void mac_cmd_handle_link_adr_req(struct lwan_ctx *ctx, const uint8_t *payload)
{
	const struct lwan_region_ops *region = ctx->region;
	uint8_t dr = FIELD_GET(LINK_ADR_DR_MASK, payload[0]);
	uint8_t tx_power = FIELD_GET(LINK_ADR_TX_POWER_MASK, payload[0]);
	uint16_t ch_mask = sys_get_le16(&payload[1]);
	uint8_t ch_mask_cntl = FIELD_GET(LINK_ADR_CH_MASK_CNTL_MASK, payload[3]);
	struct lwan_channel channels[LWAN_MAX_CHANNELS];
	uint8_t status = 0;

	if (dr == LINK_ADR_KEEP_CURRENT || region->validate_dr(dr) == 0) {
		status |= LINK_ADR_ANS_DR_ACK;
	}

	if (tx_power == LINK_ADR_KEEP_CURRENT ||
	    region->validate_tx_power(tx_power) == 0) {
		status |= LINK_ADR_ANS_POWER_ACK;
	}

	/*
	 * Try the mask against a copy: the answer has to say whether it would
	 * have been taken, and the whole request is refused unless all three
	 * parts are, so nothing may reach ctx->channels before then.
	 */
	memcpy(channels, ctx->channels, sizeof(channels));
	if (region->apply_adr_channel_mask(channels, ctx->channel_count,
					   ch_mask_cntl, ch_mask) == 0) {
		status |= LINK_ADR_ANS_CH_MASK_ACK;
	}

	if (status == LINK_ADR_ANS_ALL) {
		memcpy(ctx->channels, channels, sizeof(ctx->channels));

		if (dr != LINK_ADR_KEEP_CURRENT) {
			ctx->current_dr = (enum lorawan_datarate)dr;
		}
		if (tx_power != LINK_ADR_KEEP_CURRENT) {
			ctx->mac.tx_power_idx = tx_power;
		}
		/*
		 * NbTrans asks for a number of repetitions of each uplink,
		 * which this stack does not do yet, so that field is answered
		 * and then left alone rather than silently promised.
		 */
		LOG_INF("LinkADRReq accepted: dr=%u power=%u chmask=0x%04X cntl=%u",
			dr, tx_power, ch_mask, ch_mask_cntl);
	} else {
		LOG_WRN("LinkADRReq refused: dr_ack=%u power_ack=%u chmask_ack=%u",
			!!(status & LINK_ADR_ANS_DR_ACK),
			!!(status & LINK_ADR_ANS_POWER_ACK),
			!!(status & LINK_ADR_ANS_CH_MASK_ACK));
	}

	mac_cmd_queue_link_adr_ans(ctx, status);
}

static const struct mac_dl_cmd_info *mac_cmd_dl_info(uint8_t cid)
{
	for (size_t i = 0; i < ARRAY_SIZE(dl_cmd_table); i++) {
		if (dl_cmd_table[i].cid == cid) {
			return &dl_cmd_table[i];
		}
	}

	return NULL;
}

void mac_cmd_process_dl_fopts(struct lwan_ctx *ctx,
			      const uint8_t *fopts, size_t fopts_len)
{
	size_t pos = 0;

	while (pos < fopts_len) {
		uint8_t cid = fopts[pos];
		const struct mac_dl_cmd_info *info = mac_cmd_dl_info(cid);

		if (info == NULL) {
			/* Unknown CID: length unknown, cannot parse further */
			LOG_WRN("Unknown DL MAC command 0x%02X, dropping %zu byte(s)",
				cid, fopts_len - pos);
			return;
		}

		if (pos + 1 + info->payload_len > fopts_len) {
			LOG_WRN("Truncated DL MAC command 0x%02X", cid);
			return;
		}

		switch (cid) {
		case MAC_CMD_LINK_CHECK:
			mac_cmd_handle_link_check_ans(ctx, fopts[pos + 1],
						      fopts[pos + 2]);
			break;
		case MAC_CMD_LINK_ADR:
			mac_cmd_handle_link_adr_req(ctx, &fopts[pos + 1]);
			break;
		default:
			LOG_DBG("Unhandled DL MAC command 0x%02X", cid);
			break;
		}

		pos += 1 + info->payload_len;
	}
}

size_t mac_cmd_next_ul_fopts_len(const struct lwan_ctx *ctx)
{
	size_t pos = 0;

	if (ctx->mac.link_check_pending &&
	    pos + MAC_CMD_LINK_CHECK_REQ_LEN <= LWAN_MAX_FOPTS_LEN) {
		pos += MAC_CMD_LINK_CHECK_REQ_LEN;
	}

	for (uint8_t i = 0; i < ctx->mac.link_adr_ans_count; i++) {
		if (pos + MAC_CMD_LINK_ADR_ANS_LEN > LWAN_MAX_FOPTS_LEN) {
			break;
		}
		pos += MAC_CMD_LINK_ADR_ANS_LEN;
	}

	return pos;
}

uint8_t mac_cmd_next_payload_size(const struct lwan_ctx *ctx,
				  uint8_t max_payload)
{
	size_t fopts_len = mac_cmd_next_ul_fopts_len(ctx);

	if (fopts_len >= max_payload) {
		return 0;
	}

	return (uint8_t)(max_payload - fopts_len);
}

size_t mac_cmd_build_ul_fopts(struct lwan_ctx *ctx,
			      uint8_t *buf, size_t max_len)
{
	size_t pos = 0;

	/* Reset the snapshot — the previous frame's emit state is now stale. */
	ctx->mac.ul_built_link_check_req = false;
	ctx->mac.ul_built_link_adr_ans = 0;

	if (ctx->mac.link_check_pending &&
	    pos + MAC_CMD_LINK_CHECK_REQ_LEN <= max_len) {
		buf[pos++] = MAC_CMD_LINK_CHECK;
		ctx->mac.ul_built_link_check_req = true;
	}

	/*
	 * The network expects one answer per request of the block, so they go
	 * out in the order the requests arrived and none is merged away.
	 */
	for (uint8_t i = 0; i < ctx->mac.link_adr_ans_count; i++) {
		if (pos + MAC_CMD_LINK_ADR_ANS_LEN > max_len) {
			break;
		}
		buf[pos++] = MAC_CMD_LINK_ADR;
		buf[pos++] = ctx->mac.link_adr_ans[i];
		ctx->mac.ul_built_link_adr_ans++;
	}

	return pos;
}

void mac_cmd_commit_ul_fopts(struct lwan_ctx *ctx)
{
	if (ctx->mac.ul_built_link_check_req) {
		ctx->mac.link_check_pending = false;
		ctx->mac.ul_built_link_check_req = false;
	}

	if (ctx->mac.ul_built_link_adr_ans > 0) {
		uint8_t sent = ctx->mac.ul_built_link_adr_ans;
		uint8_t left = ctx->mac.link_adr_ans_count - sent;

		memmove(ctx->mac.link_adr_ans, &ctx->mac.link_adr_ans[sent], left);
		ctx->mac.link_adr_ans_count = left;
		ctx->mac.ul_built_link_adr_ans = 0;
	}
}
