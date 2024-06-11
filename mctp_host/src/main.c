/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/mctp/mctp.h>
#include <zephyr/mctp/mctp-serial.h>
#include <zephyr/pldm/platform.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mctp_host);

#define LOCAL_HELLO_EID 20
#define LOCAL_SENSOR_EID 21
#define LOCAL_PDR_EID 22

#define REMOTE_HELLO_EID 10
#define REMOTE_SENSOR_EID 11
#define REMOTE_PDR_EID 12


static void rx_message(uint8_t eid, bool tag_owner,
                       uint8_t msg_tag, void *data, void *msg,
                       size_t len)
{
	LOG_INF("received message %s for endpoint %d, msg_tag %d, len %zu", (char *)msg, eid,
		msg_tag, len);
}


const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(arduino_serial));

static uint8_t msg[sizeof(struct pldm_msg_hdr) +
				    sizeof(PLDM_GET_SENSOR_READING_REQ_BYTES)];
static struct pldm_msg *pldm_msg = (struct pldm_msg *)msg;
const size_t payload_length = PLDM_GET_SENSOR_READING_REQ_BYTES;

int main(void)
{
	printf("Hello MCTP! %s\n", CONFIG_BOARD_TARGET);

	int rc;
	struct mctp_binding_serial *serial;
	struct mctp *mctp;


	mctp = mctp_init();
	assert(mctp != NULL);

	serial = mctp_serial_init();
	assert(serial);

	mctp_serial_open(serial, uart);

	mctp_register_bus(mctp, mctp_binding_serial_core(serial), LOCAL_HELLO_EID);
	mctp_register_bus(mctp, mctp_binding_serial_core(serial), LOCAL_SENSOR_EID);
	mctp_set_rx_all(mctp, rx_message, NULL);

	while (true) {
		mctp_message_tx(mctp, REMOTE_HELLO_EID, false, 0, "hello", sizeof("hello"));

		encode_get_sensor_reading_req(0x09, 0, 0, pldm_msg);
		mctp_message_tx(mctp, REMOTE_SENSOR_EID, false, 0, msg, sizeof(msg));

		k_msleep(1);
		for (int i = 0; i < 10000; i++) {
			rc = mctp_serial_read(serial);
		}
		k_msleep(1000);
	}

	return 0;
}
