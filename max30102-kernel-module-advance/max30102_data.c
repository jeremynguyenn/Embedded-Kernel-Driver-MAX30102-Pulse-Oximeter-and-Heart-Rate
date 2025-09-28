#include <linux/delay.h>
#include <linux/spinlock.h>
#include "max30102.h"

/* Spinlock for atomic FIFO access to prevent race conditions */
static DEFINE_SPINLOCK(fifo_spinlock);

/**
 * max30102_clear_fifo - Clear FIFO pointers
 * @data: MAX30102 device data
 * Returns: 0 on success, negative error code on failure
 */
static int max30102_clear_fifo(struct max30102_data *data)
{
    uint8_t value = 0x00;
    int ret;

    ret = max30102_write_reg(data, MAX30102_REG_FIFO_WRITE_POINTER, &value, 1);
    if (ret) {
        dev_err(&data->client->dev, "Failed to clear FIFO write pointer: %d\n", ret);
        return ret;
    }
    ret = max30102_write_reg(data, MAX30102_REG_FIFO_READ_POINTER, &value, 1);
    if (ret) {
        dev_err(&data->client->dev, "Failed to clear FIFO read pointer: %d\n", ret);
        return ret;
    }
    ret = max30102_write_reg(data, MAX30102_REG_OVERFLOW_COUNTER, &value, 1);
    if (ret) {
        dev_err(&data->client->dev, "Failed to clear FIFO overflow counter: %d\n", ret);
        return ret;
    }
    return 0;
}

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
    unsigned long flags;
    uint8_t ovf_counter;
    int ret;

    if (!data->fifo_full) {
        dev_dbg(&data->client->dev, "No FIFO data available\n");
        return -ENODATA;
    }

    // Check overflow counter as per datasheet
    ret = max30102_read_reg(data, MAX30102_REG_OVERFLOW_COUNTER, &ovf_counter, 1);
    if (ret) {
        dev_err(&data->client->dev, "Failed to read overflow counter: %d\n", ret);
        return ret;
    }
    if (ovf_counter > 0) {
        dev_warn(&data->client->dev, "FIFO overflow: %d samples lost\n", ovf_counter);
    }

    mutex_lock(&data->lock);
    spin_lock_irqsave(&fifo_spinlock, flags);  // Atomic protection
    memcpy(red, data->red_data, sizeof(data->red_data));
    memcpy(ir, data->ir_data, sizeof(data->ir_data));
    *len = data->data_len;
    data->fifo_full = false;
    spin_unlock_irqrestore(&fifo_spinlock, flags);

    // Auto-clear FIFO after reading
    ret = max30102_clear_fifo(data);
    if (ret) {
        dev_err(&data->client->dev, "Failed to auto-clear FIFO: %d\n", ret);
        mutex_unlock(&data->lock);
        return ret;
    }

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
    uint8_t temp_int, temp_frac, status;
    int ret, timeout = 10;  // Poll instead of fixed sleep, as per datasheet (~29ms)

    ret = max30102_write_reg(data, MAX30102_REG_DIE_TEMP_CONFIG, &(uint8_t){0x01}, 1);
    if (ret) {
        dev_err(&data->client->dev, "Failed to start temperature measurement: %d\n", ret);
        return ret;
    }

    // Poll DIE_TEMP_RDY (best practice from datasheet)
    do {
        msleep(10);
        ret = max30102_read_reg(data, MAX30102_REG_INTERRUPT_STATUS_2, &status, 1);
        if (ret) return ret;
        timeout--;
    } while (!(status & (1 << MAX30102_INT_DIE_TEMP_RDY)) && timeout > 0);

    if (timeout <= 0) {
        dev_err(&data->client->dev, "Temperature measurement timeout\n");
        return -ETIMEDOUT;
    }

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

    *temp = (int8_t)temp_int + (temp_frac * 0.0625);  // Handle signed integer as per datasheet
    return 0;
}