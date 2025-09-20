#include <linux/delay.h>
#include "max30102.h"

/**
 * max30102_read_fifo - Read FIFO data (Red and IR samples)
 * @data: MAX30102 device data
 * @red: Buffer for Red LED samples
 * @ir: Buffer for IR LED samples
 * @len: Pointer to store number of samples read
 * Returns: 0 on success, negative error code on failure
 */
int max30102_read_fifo(struct max30102_data *data, uint32_t *red, uint32_t *ir, uint8_t *len)
{
    if (!data->fifo_full) {
        dev_dbg(&data->client->dev, "No FIFO data available\n");
        return -ENODATA;
    }

    mutex_lock(&data->lock);
    memcpy(red, data->red_data, sizeof(data->red_data));
    memcpy(ir, data->ir_data, sizeof(data->ir_data));
    *len = data->data_len;
    data->fifo_full = false;
    mutex_unlock(&data->lock);

    return 0;
}

/**
 * max30102_read_temperature - Read die temperature
 * @data: MAX30102 device data
 * @temp: Pointer to store temperature value
 * Returns: 0 on success, negative error code on failure
 */
int max30102_read_temperature(struct max30102_data *data, float *temp)
{
    uint8_t temp_int = 0, temp_frac = 0;
    int ret;

    ret = max30102_write_reg(data, MAX30102_REG_DIE_TEMP_CONFIG, &(uint8_t){0x01}, 1);
    if (ret) {
        dev_err(&data->client->dev, "Failed to start temperature measurement: %d\n", ret);
        return ret;
    }

    msleep(100); /* Wait for temperature conversion (per datasheet) */

    ret = max30102_read_reg(data, MAX30102_REG_DIE_TEMP_INTEGER, &temp_int, 1);
    if (ret) {
        dev_err(&data->client->dev, "Failed to read temperature integer: %d\n", ret);
        return ret;
    }

    ret = max30102_read_reg(data, MAX30102_REG_DIE_TEMP_FRACTION, &temp_frac, 1);
    if (ret) {
        dev_err(&data->client->dev, "Failed to read temperature fraction: %d\n", ret);
        return ret;
    }

    *temp = temp_int + (temp_frac * 0.0625);
    return 0;
}