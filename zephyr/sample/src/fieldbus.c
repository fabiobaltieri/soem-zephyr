#include <zephyr/kernel.h>

#include "ethercat.h"
#include "fieldbus.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(fieldbus);

void fieldbus_initialize(struct fieldbus *fb)
{
	ecx_contextt *context;

	/* Initialize the ecx_contextt data structure */
	context = &fb->context;
	context->port = &fb->port;
	context->slavelist = fb->slavelist;
	context->slavecount = &fb->slavecount;
	context->maxslave = EC_MAXSLAVE;
	context->grouplist = fb->grouplist;
	context->maxgroup = EC_MAXGROUP;
	context->esibuf = fb->esibuf;
	context->esimap = fb->esimap;
	context->esislave = 0;
	context->elist = &fb->elist;
	context->idxstack = &fb->idxstack;
	context->ecaterror = &fb->ecaterror;
	context->DCtime = &fb->DCtime;
	context->SMcommtype = fb->SMcommtype;
	context->PDOassign = fb->PDOassign;
	context->PDOdesc = fb->PDOdesc;
	context->eepSM = &fb->eepSM;
	context->eepFMMU = &fb->eepFMMU;
	context->FOEhook = NULL;
	context->EOEhook = NULL;
	context->manualstatechange = 0;
}

int fieldbus_roundtrip(struct fieldbus *fb)
{
	ecx_contextt *context;
	ec_timet start, end, diff;
	int wkc;

	context = &fb->context;

	start = osal_current_time();

	k_mutex_lock(&fb->lock, K_FOREVER);
	ecx_send_processdata(context);
	wkc = ecx_receive_processdata(context, EC_TIMEOUTRET);
	k_mutex_unlock(&fb->lock);

	end = osal_current_time();
	osal_time_diff(&start, &end, &diff);
	fb->roundtrip_time = diff.sec * 1000000 + diff.usec;

	return wkc;
}

bool fieldbus_start(struct fieldbus *fb, uint8_t *map)
{
	int ret;
	ecx_contextt *ctx;
	ec_groupt *grp;
	ec_slavet *slave;
	int i;

	ctx = &fb->context;
	grp = &fb->grouplist[0];

	ret = ecx_init(ctx, "");
	LOG_INF("ecx_init: %d", ret);

	ret = ecx_config_init(ctx, FALSE);
	LOG_INF("ecx_config_init: %d", ret);

	LOG_INF("%d slaves found", fb->slavecount);

	ecx_config_map_group(ctx, map, 0);
	LOG_INF("ecx_config_map_group: %dO+%dI bytes from %d segments",
	       grp->Obytes, grp->Ibytes, grp->nsegments);

	for (i = 0; i < fb->slavecount; i++) {
		LOG_INF("%d: %s in: %d out: %d", i,
			fb->slavelist[i+1].name,
			fb->slavelist[i+1].Ibytes,
			fb->slavelist[i+1].Obytes);
	}

	LOG_INF("ecx_configdc");
	ecx_configdc(ctx);

	LOG_INF("ecx_statecheck");
	ecx_statecheck(ctx, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);

	LOG_INF("fieldbus_roundtrip");
	fieldbus_roundtrip(fb);

	LOG_INF("setting operational state...");
	/* Act on slave 0 (a virtual slave used for broadcasting) */
	slave = &fb->slavelist[0];
	slave->state = EC_STATE_OPERATIONAL;
	ecx_writestate(ctx, 0);
	/* Poll the result ten times before giving up */
	for (i = 0; i < 10; ++i) {
		fieldbus_roundtrip(fb);
		ecx_statecheck(ctx, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE / 10);
		if (slave->state == EC_STATE_OPERATIONAL) {
			LOG_INF("all slaves are now operational");
			return true;
		}
		LOG_INF(".");
	}
	LOG_WRN("Set OP failed.");

	return false;
}

void fieldbus_stop(struct fieldbus *fb)
{
	ecx_contextt *context;
	ec_slavet *slave;

	context = &fb->context;
	slave = &fb->slavelist[0];

	LOG_INF("ecx_writestate EC_STATE_INIT");
	slave->state = EC_STATE_INIT;
	ecx_writestate(context, 0);

	LOG_INF("close");
	ecx_close(context);
}
