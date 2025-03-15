#include <zephyr/device.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_if.h>
#include <zephyr/kernel.h>

#include "ethercat.h"
#include "fieldbus.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

static struct fieldbus fieldbus;
static uint8_t map[4096];

static uint8_t slave_get(struct fieldbus *fb, int slave_num, int offset)
{
	struct ec_slave *slave;
	uint8_t ret;

	if (slave_num > fb->slavecount) {
		LOG_ERR("invalid slave number: %d", slave_num);
		return 0;
	}

	slave = &fb->slavelist[slave_num];

	if (offset >= slave->Ibytes) {
		LOG_ERR("invalid offset for slave %d (%s): %d",
			slave_num, slave->name, offset);
		return 0;
	}

	k_mutex_lock(&fb->lock, K_FOREVER);
	ret = slave->inputs[offset];
	k_mutex_unlock(&fb->lock);

	return ret;
}

static void slave_set(struct fieldbus *fb, int slave_num, int offset, uint8_t val)
{
	struct ec_slave *slave;

	if (slave_num > fb->slavecount) {
		LOG_ERR("invalid slave number: %d", slave_num);
		return;
	}

	slave = &fb->slavelist[slave_num];

	if (offset >= slave->Obytes) {
		LOG_ERR("invalid offset for slave %d (%s): %d",
			slave_num, slave->name, offset);
		return;
	}

	k_mutex_lock(&fb->lock, K_FOREVER);
	slave->outputs[offset] = val;
	k_mutex_unlock(&fb->lock);
}

static bool fieldbus_dump(struct fieldbus *fb)
{
#if 0
	ec_groupt *grp;
	uint32 n;

	grp = &fb->grouplist[0];

	printk("  O:");
	for (n = 0; n < grp->Obytes; ++n) {
		printk(" %02X", grp->outputs[n]);
	}
	printk("  I:");
	for (n = 0; n < grp->Ibytes; ++n) {
		printk(" %02X", grp->inputs[n]);
	}
	printk("  T: %lld\r", (long long)fb->DCtime);

#endif
	return true;
}

static void ec_loop(void *p1, void *p2, void *p3)
{
	struct fieldbus *fb = p1;
	int wkc, expected_wkc;
	ec_groupt *grp;
	int i = 0;

	grp = &fb->grouplist[0];

	while (!fb->started) {
		k_msleep(100);
	}

	for (;;) {
		wkc = fieldbus_roundtrip(fb);
		expected_wkc = grp->outputsWKC * 2 + grp->inputsWKC;
		if (wkc < expected_wkc) {
			LOG_WRN("WKC wrong: %d expected %d", wkc, expected_wkc);
		}

		if (i % 100 == 0) {
			LOG_INF("iteration %d RTT %dus WKC %d", i, fb->roundtrip_time, wkc);
			fieldbus_dump(fb);
		}
		i++;
		k_msleep(5);
	}
}

#define EC_LOOP_TH_SIZE 1024
K_THREAD_DEFINE(ec_loop_th, EC_LOOP_TH_SIZE,
		ec_loop, &fieldbus, NULL, NULL,
		K_PRIO_COOP(10), 0, 0);

int main(void)
{
	struct net_if *iface;

	k_mutex_init(&fieldbus.lock);

	fieldbus_initialize(&fieldbus);

	iface = net_if_get_default();

	LOG_INF("waiting for interface to be up...");
	for (;;) {
		k_sleep(K_SECONDS(1));
		if (net_if_is_up(iface)) {
			break;
		}
		LOG_INF("still waiting...");
	}

	fieldbus_start(&fieldbus, map);

	fieldbus.started = true;

	uint8_t in = 0;
	uint8_t out = 0;
	for (;;) {
		in = slave_get(&fieldbus, 3, 0);
		LOG_INF("in: %x", in);
		if (in & BIT(2)) {
			out = BIT(1);
		} else {
			out = 0;
		}
		slave_set(&fieldbus, 2, 0, out);
		k_msleep(200);
	}

	k_sleep(K_FOREVER);

	return 0;
}

#ifdef CONFIG_NET_DHCPV4
static int start_dhcp(void)
{
	struct net_if *iface;

	iface = net_if_get_default();
	net_dhcpv4_start(iface);

	return 0;
}

SYS_INIT(start_dhcp, APPLICATION, 95);
#endif
