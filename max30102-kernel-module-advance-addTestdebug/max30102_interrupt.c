#include <linux/workqueue.h>
#include <linux/ratelimit.h>
#include <linux/tracepoint.h>
#include "max30102.h"

// Define tracepoints
DEFINE_TRACE(max30102_fifo_read,
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

DEFINE_TRACE(max30102_interrupt,
             TPARGS(data, status1, status2),
             TPSTRUCT__entry(
                 __field(void *, data)
                 __field(uint8_t, status1)
                 __field(uint8_t, status2)
             ),
             TPFAST_assign(
                 __entry->data = data;
                 __entry->status1 = status1;
                 __entry->status2 = status2;
             ),
             TP_printk("data=%p status1=0x%02x status2=0x%02x", __entry->data, __entry->status1, __entry->status2)
);

/**
 * max30102_work_handler - Workqueue handler for interrupt processing
 * @work: Work structure
 */
void max30102_work_handler(struct work_struct *work)
{
    struct max30102_data *data = container_of(work, struct max30102_data, work);
    uint8_t status1, status2, write_ptr, read_ptr;
    uint8_t len;
    uint8_t *fifo_data;
    int ret;
    DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL, DEFAULT_RATELIMIT_BURST);

    mutex_lock(&data->lock);

    ret = max30102_read_reg(data, MAX30102_REG_INTERRUPT_STATUS_1, &status1, 1) ||
          max30102_read_reg(data, MAX30102_REG_INTERRUPT_STATUS_2, &status2, 1);
    if (ret) {
        if (printk_ratelimit(&rs))
            dev_err(&data->client->dev, "Failed to read interrupt status: %d\n", ret);
        goto unlock;
    }

    trace_max30102_interrupt(data, status1, status2);

    // Clear status by reading (as per datasheet, status clears on read)

    if (status1 & (1 << MAX30102_INT_FIFO_FULL)) {
        ret = max30102_read_reg(data, MAX30102_REG_FIFO_WRITE_POINTER, &write_ptr, 1) ||
              max30102_read_reg(data, MAX30102_REG_FIFO_READ_POINTER, &read_ptr, 1);
        if (ret) {
            if (printk_ratelimit(&rs))
                dev_err(&data->client->dev, "Failed to read FIFO pointers: %d\n", ret);
            goto unlock;
        }

        len = (write_ptr - read_ptr + 32) % 32;  // Improved calculation from datasheet
        if (len == 0 || len > 32) {
            if (printk_ratelimit(&rs))
                dev_err(&data->client->dev, "Invalid FIFO length: %d\n", len);
            goto unlock;
        }

        fifo_data = kmalloc(len * 6, GFP_KERNEL);
        if (!fifo_data) {
            if (printk_ratelimit(&rs))
                dev_err(&data->client->dev, "Failed to allocate FIFO buffer\n");
            goto unlock;
        }

        ret = max30102_read_reg(data, MAX30102_REG_FIFO_DATA, fifo_data, len * 6);
        if (ret) {
            if (printk_ratelimit(&rs))
                dev_err(&data->client->dev, "Failed to read FIFO data: %d\n", ret);
            kfree(fifo_data);
            goto unlock;
        }

        for (int i = 0; i < len; i++) {
            data->red_data[i] = (fifo_data[i*6] << 10) | (fifo_data[i*6+1] << 2) | (fifo_data[i*6+2] >> 6);
            data->ir_data[i] = (fifo_data[i*6+3] << 10) | (fifo_data[i*6+4] << 2) | (fifo_data[i*6+5] >> 6);
        }
        data->data_len = len;
        data->fifo_full = true;
        trace_max30102_fifo_read(data, len);
        wake_up_interruptible(&data->wait_data_ready);  // Wake blocking read
        kfree(fifo_data);
        if (printk_ratelimit(&rs))
            dev_info(&data->client->dev, "FIFO full: %d samples read\n", len);
    }

    if (status1 & (1 << MAX30102_INT_PPG_RDY))
        if (printk_ratelimit(&rs))
            dev_info(&data->client->dev, "PPG ready interrupt\n");
    if (status1 & (1 << MAX30102_INT_ALC_OVF))
        if (printk_ratelimit(&rs))
            dev_warn(&data->client->dev, "ALC overflow interrupt - adjust LED current\n");
    if (status1 & (1 << MAX30102_INT_PWR_RDY))
        if (printk_ratelimit(&rs))
            dev_info(&data->client->dev, "Power ready interrupt\n");
    if (status2 & (1 << MAX30102_INT_DIE_TEMP_RDY))
        if (printk_ratelimit(&rs))
            dev_info(&data->client->dev, "Die temperature ready interrupt\n");

unlock:
    mutex_unlock(&data->lock);
}

/**
 * max30102_irq_handler - IRQ handler for MAX30102 interrupts
 * @irq: IRQ number
 * @dev_id: Device ID (max30102_data)
 * Returns: IRQ_HANDLED
 */
irqreturn_t max30102_irq_handler(int irq, void *dev_id)
{
    struct max30102_data *data = dev_id;
    schedule_work(&data->work);
    return IRQ_HANDLED;
}