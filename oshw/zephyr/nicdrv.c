#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/promiscuous.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/mutex.h>
#include <zephyr/kernel.h>

#undef ETH_P_ECAT
#include "oshw.h"
#include "osal.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(nicdrv);

enum {
	ECT_RED_NONE, /* No redundancy, single NIC mode */
	ECT_RED_DOUBLE, /* Double redundant NIC connection */
};

/** Primary source MAC address used for EtherCAT. */
const uint16 priMAC[3] = { 0x0101, 0x0101, 0x0101 };
/** Secondary source MAC address used for EtherCAT. */
const uint16 secMAC[3] = { 0x0404, 0x0404, 0x0404 };

/** second MAC word is used for identification */
#define RX_PRIM priMAC[1]
/** second MAC word is used for identification */
#define RX_SEC secMAC[1]

static void ecx_clear_rxbufstat(int *rxbufstat)
{
	int i;
	for (i = 0; i < EC_MAXBUF; i++) {
		rxbufstat[i] = EC_BUF_EMPTY;
	}
}

int ecx_setupnic(ecx_portt *port, const char *ifname, int secondary)
{
	int i;
	int ret, rval;
	struct timeval timeout;
	struct sockaddr_ll *addr;
	struct sockaddr local;
	int *sock;
	socklen_t addrlen;

	rval = 0;
	if (secondary) {
		if (port->redport) {
			sock = &(port->redport->sockhandle);
			*sock = -1;
			port->redstate                   = ECT_RED_DOUBLE;
			port->redport->stack.sock        = &port->redport->sockhandle;
			port->redport->stack.txbuf       = &port->txbuf;
			port->redport->stack.txbuflength = &port->txbuflength;
			port->redport->stack.tempbuf     = &port->redport->tempinbuf;
			port->redport->stack.rxbuf       = &port->redport->rxbuf;
			port->redport->stack.rxbufstat   = &port->redport->rxbufstat;
			port->redport->stack.rxsa        = &port->redport->rxsa;
			ecx_clear_rxbufstat(&(port->redport->rxbufstat[0]));
		} else {
			return 0;
		}
	} else {
		k_mutex_init(&port->getindex_mutex);
		k_mutex_init(&port->tx_mutex);
		k_mutex_init(&port->rx_mutex);
		port->sockhandle        = -1;
		port->lastidx           = 0;
		port->redstate          = ECT_RED_NONE;
		port->stack.sock        = &port->sockhandle;
		port->stack.txbuf       = &port->txbuf;
		port->stack.txbuflength = &port->txbuflength;
		port->stack.tempbuf     = &port->tempinbuf;
		port->stack.rxbuf       = &port->rxbuf;
		port->stack.rxbufstat   = &port->rxbufstat;
		port->stack.rxsa        = &port->rxsa;
		ecx_clear_rxbufstat(&(port->rxbufstat[0]));
		sock = &(port->sockhandle);
	}
	*sock = zsock_socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ECAT));
	if (*sock < 0) {
		LOG_ERR("socket: %d", errno);
		return 0;
	}

	addr = (struct sockaddr_ll *)&local;
	addr->sll_ifindex = net_if_get_by_iface(net_if_get_default());
	addr->sll_family = AF_PACKET;
	addrlen = sizeof(struct sockaddr_ll);

	LOG_INF("binding to interface %d (%p)",
	       addr->sll_ifindex, net_if_get_by_index(addr->sll_ifindex));

	timeout.tv_sec = 0;
	timeout.tv_usec = 1;
	ret = zsock_setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	if (ret < 0)
		LOG_ERR("setsockopt: %d", errno);
	ret = zsock_setsockopt(*sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	if (ret < 0)
		LOG_ERR("setsockopt: %d", errno);

	net_promisc_mode_on(net_if_get_default());

	ret = zsock_bind(*sock, &local, addrlen);
	if (ret < 0)
		LOG_ERR("bind: %d", errno);

	/* setup ethernet headers in tx buffers so we don't have to repeat it */
	for (i = 0; i < EC_MAXBUF; i++) {
		ec_setupheader(&(port->txbuf[i]));
		port->rxbufstat[i] = EC_BUF_EMPTY;
	}
	ec_setupheader(&(port->txbuf2));

	return 0;
}

int ecx_closenic(ecx_portt *port)
{
	if (port->sockhandle >= 0)
		zsock_close(port->sockhandle);
	if ((port->redport) && (port->redport->sockhandle >= 0))
		zsock_close(port->redport->sockhandle);

	return 0;
}

void ec_setupheader(void *p)
{
	ec_etherheadert *bp;
	bp = p;
	bp->da0 = htons(0xffff);
	bp->da1 = htons(0xffff);
	bp->da2 = htons(0xffff);
	bp->sa0 = htons(priMAC[0]);
	bp->sa1 = htons(priMAC[1]);
	bp->sa2 = htons(priMAC[2]);
	bp->etype = htons(ETH_P_ECAT);
}

uint8 ecx_getindex(ecx_portt *port)
{
	uint8 idx;
	uint8 cnt;

	k_mutex_lock(&port->getindex_mutex, K_FOREVER);

	idx = port->lastidx + 1;
	/* index can't be larger than buffer array */
	if (idx >= EC_MAXBUF) {
		idx = 0;
	}
	cnt = 0;
	/* try to find unused index */
	while ((port->rxbufstat[idx] != EC_BUF_EMPTY) && (cnt < EC_MAXBUF)) {
		idx++;
		cnt++;
		if (idx >= EC_MAXBUF)
		{
			idx = 0;
		}
	}
	port->rxbufstat[idx] = EC_BUF_ALLOC;
	if (port->redstate != ECT_RED_NONE)
		port->redport->rxbufstat[idx] = EC_BUF_ALLOC;
	port->lastidx = idx;

	k_mutex_unlock(&port->getindex_mutex);

	return idx;
}

void ecx_setbufstat(ecx_portt *port, uint8 idx, int bufstat)
{
	port->rxbufstat[idx] = bufstat;
	if (port->redstate != ECT_RED_NONE)
		port->redport->rxbufstat[idx] = bufstat;
}

int ecx_outframe(ecx_portt *port, uint8 idx, int stacknumber)
{
	int lp, rval;
	ec_stackT *stack;

	struct sockaddr_ll dst = { 0 };
	dst.sll_ifindex = net_if_get_by_iface(net_if_get_default());

	if (!stacknumber) {
		stack = &(port->stack);
	} else {
		stack = &(port->redport->stack);
	}
	lp = (*stack->txbuflength)[idx];
	(*stack->rxbufstat)[idx] = EC_BUF_TX;

	rval = zsock_sendto(*stack->sock, (*stack->txbuf)[idx], lp, 0,
		      (const struct sockaddr *)&dst, sizeof(struct sockaddr_ll));
	if (rval == -1) {
		LOG_WRN("sendto: %d", errno);
		(*stack->rxbufstat)[idx] = EC_BUF_EMPTY;
	}

	return rval;
}

int ecx_outframe_red(ecx_portt *port, uint8 idx)
{
	ec_comt *datagramP;
	ec_etherheadert *ehp;
	int rval;

	struct sockaddr_ll dst = { 0 };
	dst.sll_ifindex = net_if_get_by_iface(net_if_get_default());

	ehp = (ec_etherheadert *)&(port->txbuf[idx]);
	/* rewrite MAC source address 1 to primary */
	ehp->sa1 = htons(priMAC[1]);
	/* transmit over primary socket*/
	rval = ecx_outframe(port, idx, 0);
	if (port->redstate != ECT_RED_NONE) {
		k_mutex_lock(&port->tx_mutex, K_FOREVER);
		ehp = (ec_etherheadert *)&(port->txbuf2);
		/* use dummy frame for secondary socket transmit (BRD) */
		datagramP = (ec_comt*)&(port->txbuf2[ETH_HEADERSIZE]);
		/* write index to frame */
		datagramP->index = idx;
		/* rewrite MAC source address 1 to secondary */
		ehp->sa1 = htons(secMAC[1]);
		/* transmit over secondary socket */
		port->redport->rxbufstat[idx] = EC_BUF_TX;

		rval = zsock_sendto(port->redport->sockhandle, &(port->txbuf2), port->txbuflength2 , 0,
			      (const struct sockaddr *)&dst, sizeof(struct sockaddr_ll));
		if (rval == -1) {
			LOG_WRN("sendto (red): %d", errno);
			port->redport->rxbufstat[idx] = EC_BUF_EMPTY;
		}
		k_mutex_unlock(&port->tx_mutex);
	}

	return rval;
}

static int ecx_recvpkt(ecx_portt *port, int stacknumber)
{
	int lp, bytesrx;
	ec_stackT *stack;

	if (!stacknumber) {
		stack = &(port->stack);
	} else {
		stack = &(port->redport->stack);
	}
	lp = sizeof(port->tempinbuf);
	bytesrx = zsock_recv(*stack->sock, (*stack->tempbuf), lp, 0);
	port->tempinbufs = bytesrx;

	return (bytesrx > 0);
}

int ecx_inframe(ecx_portt *port, uint8 idx, int stacknumber)
{
	uint16  l;
	int     rval;
	uint8   idxf;
	ec_etherheadert *ehp;
	ec_comt *ecp;
	ec_stackT *stack;
	ec_bufT *rxbuf;

	if (!stacknumber) {
		stack = &(port->stack);
	} else {
		stack = &(port->redport->stack);
	}
	rval = EC_NOFRAME;
	rxbuf = &(*stack->rxbuf)[idx];
	/* check if requested index is already in buffer ? */
	if ((idx < EC_MAXBUF) && ((*stack->rxbufstat)[idx] == EC_BUF_RCVD)) {
		l = (*rxbuf)[0] + ((uint16)((*rxbuf)[1] & 0x0f) << 8);
		/* return WKC */
		rval = ((*rxbuf)[l] + ((uint16)(*rxbuf)[l + 1] << 8));
		/* mark as completed */
		(*stack->rxbufstat)[idx] = EC_BUF_COMPLETE;
	} else {
		k_mutex_lock(&port->rx_mutex, K_FOREVER);
		/* non blocking call to retrieve frame from socket */
		if (ecx_recvpkt(port, stacknumber)) {
			rval = EC_OTHERFRAME;
			ehp = (ec_etherheadert*)(stack->tempbuf);
			/* check if it is an EtherCAT frame */
			if (ehp->etype == htons(ETH_P_ECAT)) {
				ecp =(ec_comt*)(&(*stack->tempbuf)[ETH_HEADERSIZE]);
				l = etohs(ecp->elength) & 0x0fff;
				idxf = ecp->index;
				/* found index equals requested index ? */
				if (idxf == idx) {
					/* yes, put it in the buffer array (strip ethernet header) */
					memcpy(rxbuf, &(*stack->tempbuf)[ETH_HEADERSIZE], (*stack->txbuflength)[idx] - ETH_HEADERSIZE);
					/* return WKC */
					rval = ((*rxbuf)[l] + ((uint16)((*rxbuf)[l + 1]) << 8));
					/* mark as completed */
					(*stack->rxbufstat)[idx] = EC_BUF_COMPLETE;
					/* store MAC source word 1 for redundant routing info */
					(*stack->rxsa)[idx] = ntohs(ehp->sa1);
				} else {
					/* check if index exist and someone is waiting for it */
					if (idxf < EC_MAXBUF && (*stack->rxbufstat)[idxf] == EC_BUF_TX) {
						rxbuf = &(*stack->rxbuf)[idxf];
						/* put it in the buffer array (strip ethernet header) */
						memcpy(rxbuf, &(*stack->tempbuf)[ETH_HEADERSIZE], (*stack->txbuflength)[idxf] - ETH_HEADERSIZE);
						/* mark as received */
						(*stack->rxbufstat)[idxf] = EC_BUF_RCVD;
						(*stack->rxsa)[idxf] = ntohs(ehp->sa1);
					} else {
						/* strange things happened */
					}
				}
			}
		}
		k_mutex_unlock(&port->rx_mutex);
	}
	/* WKC if matching frame found */
	return rval;
}

static int ecx_waitinframe_red(ecx_portt *port, uint8 idx, osal_timert *timer)
{
	osal_timert timer2;
	int wkc  = EC_NOFRAME;
	int wkc2 = EC_NOFRAME;
	int primrx, secrx;

	/* if not in redundant mode then always assume secondary is OK */
	if (port->redstate == ECT_RED_NONE)
		wkc2 = 0;
	do {
		/* only read frame if not already in */
		if (wkc <= EC_NOFRAME)
			wkc  = ecx_inframe(port, idx, 0);
		/* only try secondary if in redundant mode */
		if (port->redstate != ECT_RED_NONE) {
			/* only read frame if not already in */
			if (wkc2 <= EC_NOFRAME)
				wkc2 = ecx_inframe(port, idx, 1);
		}
		/* wait for both frames to arrive or timeout */
	} while (((wkc <= EC_NOFRAME) || (wkc2 <= EC_NOFRAME)) && !osal_timer_is_expired(timer));
	/* only do redundant functions when in redundant mode */
	if (port->redstate != ECT_RED_NONE) {
		/* primrx if the received MAC source on primary socket */
		primrx = 0;
		if (wkc > EC_NOFRAME) primrx = port->rxsa[idx];
		/* secrx if the received MAC source on psecondary socket */
		secrx = 0;
		if (wkc2 > EC_NOFRAME) secrx = port->redport->rxsa[idx];

		/* primary socket got secondary frame and secondary socket got primary frame */
		/* normal situation in redundant mode */
		if ((primrx == RX_SEC) && (secrx == RX_PRIM)) {
			/* copy secondary buffer to primary */
			memcpy(&(port->rxbuf[idx]), &(port->redport->rxbuf[idx]), port->txbuflength[idx] - ETH_HEADERSIZE);
			wkc = wkc2;
		}
		/* primary socket got nothing or primary frame, and secondary socket got secondary frame */
		/* we need to resend TX packet */
		if (((primrx == 0) && (secrx == RX_SEC)) ||
		    ((primrx == RX_PRIM) && (secrx == RX_SEC))) {
			/* If both primary and secondary have partial connection retransmit the primary received
			 * frame over the secondary socket. The result from the secondary received frame is a combined
			 * frame that traversed all slaves in standard order. */
			if ((primrx == RX_PRIM) && (secrx == RX_SEC)) {
				/* copy primary rx to tx buffer */
				memcpy(&(port->txbuf[idx][ETH_HEADERSIZE]), &(port->rxbuf[idx]), port->txbuflength[idx] - ETH_HEADERSIZE);
			}
			osal_timer_start(&timer2, EC_TIMEOUTRET);
			/* resend secondary tx */
			ecx_outframe(port, idx, 1);
			do {
				/* retrieve frame */
				wkc2 = ecx_inframe(port, idx, 1);
			} while ((wkc2 <= EC_NOFRAME) && !osal_timer_is_expired(&timer2));
			if (wkc2 > EC_NOFRAME) {
				/* copy secondary result to primary rx buffer */
				memcpy(&(port->rxbuf[idx]), &(port->redport->rxbuf[idx]), port->txbuflength[idx] - ETH_HEADERSIZE);
				wkc = wkc2;
			}
		}
	}

	/* return WKC or EC_NOFRAME */
	return wkc;
}

int ecx_waitinframe(ecx_portt *port, uint8 idx, int timeout)
{
	int wkc;
	osal_timert timer;

	osal_timer_start(&timer, timeout);
	wkc = ecx_waitinframe_red(port, idx, &timer);

	return wkc;
}

int ecx_srconfirm(ecx_portt *port, uint8 idx, int timeout)
{
	int wkc = EC_NOFRAME;
	osal_timert timer1, timer2;

	osal_timer_start(&timer1, timeout);
	do {
		/* tx frame on primary and if in redundant mode a dummy on secondary */
		ecx_outframe_red(port, idx);
		if (timeout < EC_TIMEOUTRET) {
			osal_timer_start(&timer2, timeout);
		} else {
			/* normally use partial timeout for rx */
			osal_timer_start(&timer2, EC_TIMEOUTRET);
		}
		/* get frame from primary or if in redundant mode possibly from secondary */
		wkc = ecx_waitinframe_red(port, idx, &timer2);
		/* wait for answer with WKC>=0 or otherwise retry until timeout */
	} while ((wkc <= EC_NOFRAME) && !osal_timer_is_expired(&timer1));

	return wkc;
}

#ifdef EC_VER1
int ec_setupnic(const char *ifname, int secondary)
{
	return ecx_setupnic(&ecx_port, ifname, secondary);
}

int ec_closenic(void)
{
	return ecx_closenic(&ecx_port);
}

uint8 ec_getindex(void)
{
	return ecx_getindex(&ecx_port);
}

void ec_setbufstat(uint8 idx, int bufstat)
{
	ecx_setbufstat(&ecx_port, idx, bufstat);
}

int ec_outframe(uint8 idx, int stacknumber)
{
	return ecx_outframe(&ecx_port, idx, stacknumber);
}

int ec_outframe_red(uint8 idx)
{
	return ecx_outframe_red(&ecx_port, idx);
}

int ec_inframe(uint8 idx, int stacknumber)
{
	return ecx_inframe(&ecx_port, idx, stacknumber);
}

int ec_waitinframe(uint8 idx, int timeout)
{
	return ecx_waitinframe(&ecx_port, idx, timeout);
}

int ec_srconfirm(uint8 idx, int timeout)
{
	return ecx_srconfirm(&ecx_port, idx, timeout);
}
#endif
