#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(control);

#include <pb_encode.h>
#include <pb_decode.h>
#include "pubsub.pb.h"

/* Bus and target address for our i2c controller device */
const uint8_t I2C_ADDR = 0x7F;


/* Registers for our i2c target device */
const uint8_t WHO_AM_I_ADDR = 0x01;
const uint8_t WHO_AM_I_VAL = 0x50;

const uint8_t MSG_OUT_FIFO_ADDR = 0x10;
const uint8_t MSG_OUT_BYTE_CNTL_ADDR = 0x11;
const uint8_t MSG_OUT_BYTE_CNTH_ADDR = 0x12;

const uint8_t MSG_IN_FIFO_ADDR = 0x20;


/* override the default memcpy as zephyr will call this before relocation happens */
__boot_func
void  z_early_memcpy(void *dst, const void *src, size_t n)
{
	/* attempt word-sized copying only if buffers have identical alignment */
	unsigned char *d_byte = (unsigned char *)dst;
	const unsigned char *s_byte = (const unsigned char *)src;
	/* do byte-sized copying until finished */

	while (n > 0) {
		*(d_byte++) = *(s_byte++);
		n--;
	}

	return (void)dst;
}

__boot_func
void z_early_memset(void *dst, int c, size_t n)
{
	/* do byte-sized initialization until word-aligned or finished */

	unsigned char *d_byte = (unsigned char *)dst;
	unsigned char c_byte = (unsigned char)c;

	while (n > 0) {
		*(d_byte++) = c_byte;
		n--;
	}
	return (void)dst;
}


const struct i2c_dt_spec i2c_device = {
	.addr = I2C_ADDR,
	.bus = DEVICE_DT_GET(DT_ALIAS(i2c_controller)),
};

int read_pubsub(PubSub *message, uint8_t *buf, size_t buf_len)
{
	int ret = 0;

	pb_istream_t stream = pb_istream_from_buffer(buf, buf_len);

	*message = (PubSub)PubSub_init_zero;

	ret = pb_decode(&stream, PubSub_fields, message);
	if (!ret) {
		LOG_ERR("decoding failed: %s", PB_GET_ERROR(&stream));
		return -EINVAL;
	}

	return 0;
}

static uint8_t in_fifo[256];

int main(void)
{
	int res;
	uint8_t whoami;
	uint8_t fifo_len_buf[2];

	LOG_INF("i2c controller");
	res = i2c_reg_read_byte_dt(&i2c_device, WHO_AM_I_ADDR, &whoami);
	if (res != 0) {
		LOG_ERR("failed to read who_am_i");
		return -EIO;
	}

	if (whoami != WHO_AM_I_VAL) {
		LOG_ERR("WHO_AM_I value %x did not match expected %x", whoami, WHO_AM_I_VAL);
		return -EIO;
	}

	LOG_INF("periodically polling for pubsub");

	while (1) {
		k_sleep(K_SECONDS(1));

		LOG_INF("reading fifo len");
		res = i2c_write_read_dt(&i2c_device, &MSG_OUT_BYTE_CNTL_ADDR, 1, fifo_len_buf, 2);
		if (res != 0) {
			LOG_ERR("Failed to read message out byte count, %d", res);
			continue;
		}

		uint16_t fifo_len = (uint16_t)fifo_len_buf[0] + ((uint16_t)fifo_len_buf[1] >> 8);

		if (fifo_len == 0) {
			continue;
		}

		/* Read some bytes from the remote FIFO */
		LOG_INF("%d bytes available, reading!", fifo_len);
		res = i2c_write_read_dt(&i2c_device, &MSG_OUT_FIFO_ADDR, 1, in_fifo, fifo_len);
		if (res != 0) {
			LOG_WRN("Failed to read message fifo, %d", res);
			continue;
		}
		LOG_HEXDUMP_INF(in_fifo, fifo_len, "read fifo");

		static PubSub message;

		res = read_pubsub(&message, in_fifo, fifo_len);
		if (res > 0) {
			LOG_INF("decoded message, result %d, pubsub published %d, topic %d",
				res, message.publisher_id, message.which_topic_msg);
		}
	}

	return 0;
}
