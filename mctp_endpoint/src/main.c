/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/mctp/mctp.h>
#include <zephyr/mctp/mctp-serial.h>
#include <zephyr/pldm/platform.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mctp_endpoint);



static struct mctp_binding_serial *serial;
static struct mctp *mctp;

#define MCTP_EID 8

static void rx_message(uint8_t eid, bool tag_owner,
                       uint8_t msg_tag, void *data, void *msg,
                       size_t len)
{
	LOG_INF("got message %s for eid %d, replying to 5 with \"world\"", (char*) msg, eid);
	mctp_message_tx(mctp, 5, false, 0, "world", sizeof("world"));
}


const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(arduino_serial));

int main(void)
{
	LOG_INF("MCTP Endpoint %d on %s\n", MCTP_EID, CONFIG_BOARD_TARGET);
	int rc = 0;


	mctp = mctp_init();
	assert(mctp != NULL);

	serial = mctp_serial_init();
	assert(serial);

	mctp_serial_open(serial, uart);

	mctp_register_bus(mctp, mctp_binding_serial_core(serial), MCTP_EID);
	mctp_set_rx_all(mctp, rx_message, NULL);

	while (true) {
	    for (int i = 0; i < 1000; i++) {
		    rc = mctp_serial_read(serial);
	    }
	    k_yield();
	}

	LOG_INF("exiting");
	return 0;
}
