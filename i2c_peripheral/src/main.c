#include <zephyr/drivers/i2c.h>
#include <zephyr/rtio/rtio_spsc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(periph);

#include <pb_encode.h>
#include <pb_decode.h>
#include "pubsub.pb.h"

/* Bus and address for our i2c target device */
const struct device *i2c_bus = DEVICE_DT_GET(DT_ALIAS(i2c_target));
const uint8_t I2C_ADDR = 0x7F;

/* Registers for our i2c target device */
const uint8_t UNSET_ADDR = 0x00;

const uint8_t WHO_AM_I_ADDR = 0x01;
const uint8_t WHO_AM_I_VAL = 0x50;

const uint8_t MSG_OUT_FIFO_ADDR = 0x10;
const uint8_t MSG_OUT_BYTE_CNTL_ADDR = 0x11;
const uint8_t MSG_OUT_BYTE_CNTH_ADDR = 0x12;

const uint8_t MSG_IN_FIFO_ADDR = 0x20;

const uint8_t INVALID_ADDR = 0xFE;

struct pub_msg {
	uint16_t len;
	uint8_t buf[32];
};

RTIO_SPSC_DEFINE(msg_pool, struct pub_msg, 4);

#define FIFO_SIZE 32

struct i2c_pubsub_target {
	struct i2c_target_config config;

	uint8_t xfer_reg_addr;

	uint16_t in_idx;
	uint8_t in_buf[FIFO_SIZE];

	uint16_t out_idx;
	struct pub_msg *msg;
};

int pubsub_write_requested(struct i2c_target_config *cfg)
{
	int ret;
	struct i2c_pubsub_target *data = CONTAINER_OF(cfg, struct i2c_pubsub_target, config);

	LOG_INF("write requested, reg addr %x", data->xfer_reg_addr);

	switch(data->xfer_reg_addr) {
		case UNSET_ADDR:
		case MSG_IN_FIFO_ADDR:
			ret = 0;
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

int pubsub_write_received(struct i2c_target_config *cfg, uint8_t val)
{
	int ret = 0;
	struct i2c_pubsub_target *data = CONTAINER_OF(cfg, struct i2c_pubsub_target, config);

	LOG_INF("write received, reg addr %x, data %x", data->xfer_reg_addr,
		val);

	switch(data->xfer_reg_addr) {
		case UNSET_ADDR:
			data->xfer_reg_addr = val;
			break;
		case MSG_IN_FIFO_ADDR:
			if (data->in_idx < FIFO_SIZE) {
			    data->in_buf[data->in_idx] = val;
			    data->in_idx++;
			} else {
				data->xfer_reg_addr = INVALID_ADDR;
				ret = -ENOMEM;
			}
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

int pubsub_read_register(struct i2c_pubsub_target *data, uint8_t *val)
{
	int ret = 0;

	switch(data->xfer_reg_addr) {
		case WHO_AM_I_ADDR:
			*val = WHO_AM_I_VAL;
			data->xfer_reg_addr = INVALID_ADDR;
			break;
		case MSG_OUT_FIFO_ADDR:
			if (data->msg == NULL) {
				ret = -EINVAL;
				break;
			}

			if (data->out_idx < data->msg->len) {
				*val = data->msg->buf[data->out_idx];
				data->out_idx++;
			}

			if (data->out_idx >= data->msg->len) {
				data->xfer_reg_addr = INVALID_ADDR;
			}
			break;
		case MSG_OUT_BYTE_CNTL_ADDR:
			if (data->msg == NULL) {
				*val = 0;
			} else {
				*val = (uint8_t)data->msg->len;
			}
			data->xfer_reg_addr++;
			break;
		case MSG_OUT_BYTE_CNTH_ADDR:
			if (data->msg == NULL) {
				*val = 0;
			} else {
				*val = ((uint8_t)data->msg->len >> 8);
			}
			data->xfer_reg_addr = INVALID_ADDR;
			break;
		default:
			ret = -EINVAL;
			break;
	}
	return ret;
}

int pubsub_read_requested(struct i2c_target_config *cfg, uint8_t *val)
{
	struct i2c_pubsub_target *data = CONTAINER_OF(cfg, struct i2c_pubsub_target, config);

	LOG_DBG("read requested, reg %x", data->xfer_reg_addr);

	if (data->xfer_reg_addr == MSG_OUT_BYTE_CNTL_ADDR) {
		data->msg = rtio_spsc_consume(&msg_pool);
	}

	return pubsub_read_register(data, val);
}

int pubsub_read_processed(struct i2c_target_config *cfg, uint8_t *val)
{
	struct i2c_pubsub_target *data = CONTAINER_OF(cfg, struct i2c_pubsub_target, config);

	LOG_DBG("read processed, reg %x", data->xfer_reg_addr);

	return pubsub_read_register(data, val);
}

int pubsub_stop(struct i2c_target_config *cfg)
{
	struct i2c_pubsub_target *data = CONTAINER_OF(cfg, struct i2c_pubsub_target, config);

	LOG_DBG("stop received, reg %x", data->xfer_reg_addr);

	data->xfer_reg_addr = UNSET_ADDR;
	if (data->out_idx != 0) {
		data->out_idx = 0;
		data->msg = NULL;
		rtio_spsc_release(&msg_pool);
	}

	return 0;
}

static const struct i2c_target_callbacks pubsub_callbacks = {
	.write_requested = pubsub_write_requested,
	.write_received = pubsub_write_received,
	.read_requested = pubsub_read_requested,
	.read_processed = pubsub_read_processed,
	.stop = pubsub_stop,
};

static struct i2c_pubsub_target pubsub_i2c = {
	.config =
		{
			.address = I2C_ADDR,
			.callbacks = &pubsub_callbacks,
		},
};

static PubSub message;
static pb_ostream_t stream;

int write_sample(float x)
{
	int ret;

	struct pub_msg *msg = rtio_spsc_acquire(&msg_pool);

	if (msg == NULL) {
		LOG_WRN("No more buffers to send messages");
		return -ENOMEM;
	}

	stream = (pb_ostream_t)pb_ostream_from_buffer(msg->buf, sizeof(msg->buf));
	message = (PubSub)PubSub_init_zero;

	message.publisher_id = 1;
	message.which_topic_msg = PubSub_sensor_data_tag;
	message.topic_msg.sensor_data = (SensorData){
		.timestamp = k_cycle_get_64(),
		.has_channel = true,
		.channel =
			{
				.idx = 0,
				.type = SensorChannelType_SENSOR_TEMP,
			},
		.value = x,
	};

	ret = pb_encode(&stream, PubSub_fields, &message);
	if (!ret) {
		LOG_ERR("encoding failed: %s", PB_GET_ERROR(&stream));
		rtio_spsc_drop_all(&msg_pool);
		return -EINVAL;
	}

	msg->len = stream.bytes_written;
	LOG_INF("bytes written %d", msg->len);
	LOG_HEXDUMP_INF(msg->buf, msg->len, "out fifo");
	rtio_spsc_produce(&msg_pool);

	return 0;
}

int main(void)
{
	LOG_INF("i2c peripheral");

	k_sleep(K_SECONDS(1));

	int ret = i2c_target_register(i2c_bus, &pubsub_i2c.config);

	if (ret != 0) {
		LOG_WRN("Failed to register i2c target device %x", I2C_ADDR);
	}

	int temp = 0;

	while (1) {
		k_sleep(K_SECONDS(1));

		/* Periodically publish a dummy temperature reading message */
		write_sample((float)temp);

		if (temp >= 25) {
			temp = 0;
		} else {
			temp += 1;
		}

	}

	return 0;
}
