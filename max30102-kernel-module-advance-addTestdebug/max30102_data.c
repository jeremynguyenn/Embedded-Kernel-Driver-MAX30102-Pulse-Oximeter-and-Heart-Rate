#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/ratelimit.h>
#include <linux/tracepoint.h>
#include "max30102.h"

/* Added spinlock for atomic FIFO access to prevent race conditions */
static DEFINE_SPINLOCK(fifo_spinlock);

// Define tracepoints
DEFINE_TRACE(max30102_fifo_access,
             TPARGS(data, len),
             TPSTRUCT__entry(
                 __field(void *, data)
                 __field(uint8_t, len)
             ),
             TPFAST_assign(
                 __entry->data = data;
                 __entry->len = len;
             ),
             TP_printk("data=%p len=%u", __entry->data, __entry->len)
);

DEFINE_TRACE(max30102_temp_read,
             TPARGS(data, temp),
             TPSTRUCT__entry(
                 __field(void *, data)
                 __field(float, temp)
             ),
             TPFAST_assign(
                 __entry->data = data;
                 __entry->temp = temp;
             ),
             TP_printk("data=%p temp=%f", __entry->data, __entry->temp)
);

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
    DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL, DEFAULT_RATELIMIT_BURST);

    if (!data->fifo_full) {
        if (printk_ratelimit(&rs))
            dev_dbg(&data->client->dev, "No FIFO data available\n");
        return -ENODATA;
    }

    // Check overflow counter as per datasheet
    max30102_read_reg(data, MAX30102_REG_OVERFLOW_COUNTER, &ovf_counter, 1);
    if (ovf_counter > 0) {
        if (printk_ratelimit(&rs))
            dev_warn(&data->client->dev, "FIFO overflow: %d samples lost\n", ovf_counter);
    }

    mutex_lock(&data->lock);
    spin_lock_irqsave(&fifo_spinlock, flags);  // Atomic protection
    memcpy(red, data->red_data, sizeof(data->red_data));
    memcpy(ir, data->ir_data, sizeof(data->ir_data));
    *len = data->data_len;
    data->fifo_full = false;
    trace_max30102_fifo_access(data, *len);
    spin_unlock_irqrestore(&fifo_spinlock, flags);
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
    int ret, timeout = 10;  // Improved: Poll instead of fixed sleep, as per datasheet (~29ms)
    DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL, DEFAULT_RATELIMIT_BURST);

    ret = max30102_write_reg(data, MAX30102_REG_DIE_TEMP_CONFIG, &(uint8_t){0x01}, 1);
    if (ret) {
        if (printk_ratelimit(&rs))
            dev_err(&data->client->dev, "Failed to start temperature measurement: %d\n", ret);
        return ret;
    }

    // Poll DIE_TEMP_RDY (best practice from datasheet)
    do {
        msleep(10);
        ret = max30102_read_reg(data, MAX30102_REG_INTERRUPT_STATUS_2, &status, 1);
        if (ret) {
            if (printk_ratelimit(&rs))
                dev_err(&data->client->dev, "Failed to read interrupt status: %d\n", ret);
            return ret;
        }
        timeout--;
    } while (!(status & (1 << MAX30102_INT_DIE_TEMP_RDY)) && timeout > 0);

    if (timeout <= 0) {
        if (printk_ratelimit(&rs))
            dev_err(&data->client->dev, "Temperature measurement timeout\n");
        return -ETIMEDOUT;
    }

    ret = max30102_read_reg(data, MAX30102_REG_DIE_TEMP_INTEGER, &temp_int, 1);
    if (ret) {
        if (printk_ratelimit(&rs))
            dev_err(&data->client->dev, "Failed to read temperature integer: %d\n", ret);
        return ret;
    }

    ret = max30102_read_reg(data, MAX30102_REG_DIE_TEMP_FRACTION, &temp_frac, 1);
    if (ret) {
        if (printk_ratelimit(&rs))
            dev_err(&data->client->dev, "Failed to read temperature fraction: %d\n", ret);
        return ret;
    }

    *temp = (int8_t)temp_int + (temp_frac * 0.0625);  // Handle signed integer as per datasheet
    trace_max30102_temp_read(data, *temp);
    return 0;
}