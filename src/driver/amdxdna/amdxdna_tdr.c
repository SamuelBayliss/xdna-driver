// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 */

#include "amdxdna_drm.h"
#include "amdxdna_tdr.h"

uint timeout_in_sec = 2;
module_param(timeout_in_sec, uint, 0644);
MODULE_PARM_DESC(timeout_in_sec, "Seconds to timeout and recovery, default 2; 0 - No TDR");

#define TDR_TIMEOUT_JIFF msecs_to_jiffies(timeout_in_sec * 1000)

static void amdxdna_tdr_work(struct work_struct *work)
{
	struct amdxdna_tdr *tdr = to_tdr(work);
	struct amdxdna_client *client;
	struct amdxdna_hwctx *hwctx;
	struct amdxdna_dev *xdna;
	bool active = false;
	int stop_cnt = 0;
	int ctx_cnt = 0;
	int next;
	int idx;

	xdna = tdr_to_xdna_dev(tdr);
	mutex_lock(&xdna->dev_lock);
	list_for_each_entry(client, &xdna->client_list, node) {
		next = 0;
		idx = srcu_read_lock(&client->hwctx_srcu);
		idr_for_each_entry_continue(&client->hwctx_idr, hwctx, next) {
			u64 completed = hwctx->completed; /* To avoid race */
			u64 last = hwctx->tdr_last_completed;
			u64 submitted = hwctx->submitted;

			XDNA_DBG(xdna, "%s submitted %lld completed %lld last %lld",
				 hwctx->name, submitted, completed, last);
			ctx_cnt++;
			if (submitted == completed) {
				stop_cnt++;
				continue;
			}

			if (last != completed) {
				hwctx->tdr_last_completed = completed;
				active = true;
				break;
			}
		}
		srcu_read_unlock(&client->hwctx_srcu, idx);
		if (active)
			break;
	}
	mutex_unlock(&xdna->dev_lock);

	/* Recover the device when all running context are inactive */
	if (ctx_cnt != stop_cnt && !active) {
		XDNA_WARN(xdna, "Recovering... Count %d", ++tdr->tdr_counter);
		xdna->dev_info->ops->recover(xdna);
	}
}

static void amdxdna_tdr_timer(struct timer_list *t)
{
	struct amdxdna_tdr *tdr = from_timer(tdr, t, timer);

	queue_work(system_long_wq, &tdr->tdr_work);

	mod_timer(t, jiffies + TDR_TIMEOUT_JIFF);
}

void amdxdna_tdr_start(struct amdxdna_tdr *tdr)
{
	struct amdxdna_dev *xdna = tdr_to_xdna_dev(tdr);

	if (!xdna->dev_info->ops->recover) {
		XDNA_DBG(xdna, "Not support recovery, watchdog NOT started");
		return;
	}

	if (!timeout_in_sec) {
		XDNA_DBG(xdna, "timeout_in_sec is zero, watchdog NOT started");
		return;
	}

	timer_setup(&tdr->timer, amdxdna_tdr_timer, 0);
	INIT_WORK(&tdr->tdr_work, amdxdna_tdr_work);

	tdr->timer.expires = jiffies + TDR_TIMEOUT_JIFF;
	add_timer(&tdr->timer);
	tdr->started = 1;
	XDNA_DBG(xdna, "Check activities in every %d secs", timeout_in_sec);
}

void amdxdna_tdr_stop(struct amdxdna_tdr *tdr)
{
	struct amdxdna_dev *xdna = tdr_to_xdna_dev(tdr);

	if (!tdr->started)
		return;

	timer_delete_sync(&tdr->timer);
	XDNA_DBG(xdna, "Timer stopped");
}
