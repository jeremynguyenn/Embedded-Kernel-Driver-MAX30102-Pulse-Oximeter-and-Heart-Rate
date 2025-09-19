#include <linux/i2c.h>
#include "max30102.h"

/**
 * max30102_write_reg - Write to MAX30102 register via I2C
 * @data: MAX30102 device data
 * @reg: Register address
 * @buf: Buffer containing data to write
 * @len: Length of data to write
 * Returns: 0 on success, negative error code on failure
 */
int max30102_write_reg(struct max30102_data *data, uint8_t reg, uint8_t *buf, uint16_t len)
{
    struct i2c_msg msg;
    uint8_t send_buf[32 + 1];
    int ret;

    if (len > 32) {
        dev_err(&data->client->dev, "Invalid buffer length: %d, max is 32\n", len);
        return -EINVAL;
    }

    send_buf[0] = reg;
    memcpy(&send_buf[1], buf, len);

    msg.addr = data->client->addr;
    msg.flags = 0;
    msg.buf = send_buf;
    msg.len = len + 1;

    ret = i2c_transfer(data->client->adapter, &msg, 1);
    if (ret != 1) {
        dev_err(&data->client->dev, "I2C write failed: reg=0x%02x, len=%d, error=%d\n", reg, len, ret);
        return ret < 0 ? ret : -EIO;
    }

    return 0;
}

/**
 * max30102_read_reg - Read from MAX30102 register via I2C
 * @data: MAX30102 device data
 * @reg: Register address
 * @buf: Buffer to store read data
 * @len: Length of data to read
 * Returns: 0 on success, negative error code on failure
 */
int max30102_read_reg(struct max30102_data *data, uint8_t reg, uint8_t *buf, uint16_t len)
{
    struct i2c_msg msgs[2];
    int ret;

    if (len > 32) {
        dev_err(&data->client->dev, "Invalid read length: %d, max is 32\n", len);
        return -EINVAL;
    }

    msgs[0].addr = data->client->addr;
    msgs[0].flags = 0;
    msgs[0].buf = &reg;
    msgs[0].len = 1;

    msgs[1].addr = data->client->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].buf = buf;
    msgs[1].len = len;

    ret = i2c_transfer(data->client->adapter, msgs, 2);
    if (ret != 2) {
        dev_err(&data->client->dev, "I2C read failed: reg=0x%02x, len=%d, error=%d\n", reg, len, ret);
        return ret < 0 ? ret : -EIO;
    }

    return 0;
}