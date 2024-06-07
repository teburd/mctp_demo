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

#define LOCAL_HELLO_EID 8
#define LOCAL_SENSOR_EID 9
#define REMOTE_HELLO_EID 5
#define REMOTE_SENSOR_EID 6

static uint8_t msg[sizeof(struct pldm_msg_hdr) +
				    sizeof(PLDM_GET_SENSOR_READING_MIN_RESP_BYTES+3)];
static struct pldm_msg *pldm_msg = (struct pldm_msg *)msg;
static int32_t reading = -5041;
const size_t payload_length = PLDM_GET_SENSOR_READING_MIN_RESP_BYTES+3;

static void rx_message(uint8_t eid, bool tag_owner,
                       uint8_t msg_tag, void *data, void *msg,
                       size_t len)
{
	switch (eid) {
		case REMOTE_HELLO_EID:
			LOG_INF("got mctp message %s for eid %d, replying to 5 with \"world\"",
				(char*) msg, eid);
			mctp_message_tx(mctp, 5, false, 0, "world", sizeof("world"));
			break;
		case REMOTE_SENSOR_EID:
			LOG_INF("got pldm message for eid %d, replying with sensor data", eid);


			encode_get_sensor_reading_resp(0x09, 0, PLDM_SENSOR_DATA_SIZE_SINT32,
						       PLDM_SENSOR_ENABLED,
						       PLDM_NO_EVENT_GENERATION,
						       PLDM_SENSOR_NORMAL,
						       PLDM_SENSOR_UNKNOWN,
						       PLDM_SENSOR_NORMAL,
						       (const uint8_t*) reading,
						       pldm_msg, payload_length);

			mctp_message_tx(mctp, 9, false, 0, msg, sizeof(msg));
			break;
		default:
			LOG_INF("Unknown endpoint %d", eid);
			break;
	}
}

const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(arduino_serial));

int main(void)
{
	LOG_INF("MCTP Endpoint %d on %s\n", LOCAL_HELLO_EID, CONFIG_BOARD_TARGET);
	int rc = 0;


	mctp = mctp_init();
	assert(mctp != NULL);

	serial = mctp_serial_init();
	assert(serial);

	mctp_serial_open(serial, uart);

	mctp_register_bus(mctp, mctp_binding_serial_core(serial), LOCAL_HELLO_EID);
	mctp_register_bus(mctp, mctp_binding_serial_core(serial), LOCAL_SENSOR_EID);
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
