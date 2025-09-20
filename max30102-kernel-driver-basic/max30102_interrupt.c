#include <linux/workqueue.h>
#include <linux/slab.h>
#include "max30102.h"

/**
 * max30102_work_handler - Workqueue handler for interrupt processing
 * @work: Work structure
 */
void max30102_work_handler(struct work_struct *work)
{
    struct max30102_data *data = container_of(work, struct max30102_data, work);
    uint8_t status1 = 0, status2 = 0, write_ptr = 0, read_ptr = 0;
    uint8_t len;
    uint8_t *fifo_data = NULL;
    int ret;

    mutex_lock(&data->lock);

    ret = max30102_read_reg(data, MAX30102_REG_INTERRUPT_STATUS_1, &status1, 1) |
          max30102_read_reg(data, MAX30102_REG_INTERRUPT_STATUS_2, &status2, 1);
    if (ret) {
        dev_err(&data->client->dev, "Failed to read interrupt status: %d\n", ret);
        goto unlock;
    }

    if (status1 & (1 << MAX30102_INT_FIFO_FULL)) {
        ret = max30102_read_reg(data, MAX30102_REG_FIFO_WRITE_POINTER, &write_ptr, 1) |
              max30102_read_reg(data, MAX30102_REG_FIFO_READ_POINTER, &read_ptr, 1);
        if (ret) {
            dev_err(&data->client->dev, "Failed to read FIFO pointers: %d\n", ret);
            goto unlock;
        }

        len = (write_ptr - read_ptr) & 0x1F; /* Calculate number of samples */
        if (len == 0 || len > 32) {
            dev_err(&data->client->dev, "Invalid FIFO length: %d\n", len);
            goto unlock;
        }

        fifo_data = kmalloc(len * 6, GFP_KERNEL);
        if (!fifo_data) {
            dev_err(&data->client->dev, "Failed to allocate FIFO buffer\n");
            goto unlock;
        }

        ret = max30102_read_reg(data, MAX30102_REG_FIFO_DATA, fifo_data, len * 6);
        if (ret) {
            dev_err(&data->client->dev, "Failed to read FIFO data: %d\n", ret);
            kfree(fifo_data);
            goto unlock;
        }

        for (int i = 0; i < len; i++) {
            data->red_data[i] = (fifo_data[i*6] << 16) | (fifo_data[i*6+1] << 8) | fifo_data[i*6+2];
            data->ir_data[i] = (fifo_data[i*6+3] << 16) | (fifo_data[i*6+4] << 8) | fifo_data[i*6+5];
        }
        data->data_len = len;
        data->fifo_full = true;
        kfree(fifo_data);
        dev_info(&data->client->dev, "FIFO full: %d samples read\n", len);
    }

    if (status1 & (1 << MAX30102_INT_PPG_RDY))
        dev_info(&data->client->dev, "PPG ready interrupt\n");
    if (status1 & (1 << MAX30102_INT_ALC_OVF))
        dev_info(&data->client->dev, "ALC overflow interrupt\n");
    if (status1 & (1 << MAX30102_INT_PWR_RDY))
        dev_info(&data->client->dev, "Power ready interrupt\n");
    if (status2 & (1 << MAX30102_INT_DIE_TEMP_RDY))
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