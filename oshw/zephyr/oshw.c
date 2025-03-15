#include <zephyr/net/net_ip.h>
#include <zephyr/kernel.h>

#include "oshw.h"

uint16 oshw_htons(uint16 host)
{
	uint16 network = htons(host);
	return network;
}

uint16 oshw_ntohs(uint16 network)
{
	uint16 host = ntohs(network);
	return host;
}
