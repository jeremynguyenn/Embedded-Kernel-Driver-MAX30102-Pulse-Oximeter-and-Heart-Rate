#include <linux/uaccess.h>
#include "max30102.h"

/**
 * max30102_open - Open function for device file
 * @inode: Inode structure
 * @file: File structure
 * Returns: 0 on success
 */
static int max30102_open(struct inode *inode, struct file *file)
{
    struct miscdevice *miscdev = file->private_data;
    struct max30102_data *data = container_of(miscdev, struct max30102_data, miscdev);
    file->private_data = data;
    init_waitqueue_head(&data->wait_data_ready);  // Init wait queue
    dev_info(&data->client->dev, "Device opened by process %d\n", current->pid);  // Process management
    return 0;
}

/**
 * max30102_ioctl - IOCTL handler for user-space interaction
 * @file: File structure
 * @cmd: IOCTL command
 * @arg: Argument from user space
 * Returns: 0 on success, negative error code on failure
 */
static long max30102_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct max30102_data *data = file->private_data;
    struct max30102_fifo_data fifo_data;
    struct max30102_slot_config slot_config;
    uint8_t mode, config;
    float temp;
    int ret;

    mutex_lock(&data->lock);

    switch (cmd) {
    case MAX30102_IOC_READ_FIFO:
        ret = max30102_read_fifo(data, fifo_data.red, fifo_data.ir, &fifo_data.len);
        if (ret)
            goto unlock;
        if (copy_to_user((void __user *)arg, &fifo_data, sizeof(fifo_data))) {
            dev_err(&data->client->dev, "Failed to copy FIFO data to user\n");
            ret = -EFAULT;
            goto unlock;
        }
        break;

    case MAX30102_IOC_READ_TEMP:
        ret = max30102_read_temperature(data, &temp);
        if (ret)
            goto unlock;
        if (copy_to_user((void __user *)arg, &temp, sizeof(temp))) {
            dev_err(&data->client->dev, "Failed to copy temperature to user\n");
            ret = -EFAULT;
            goto unlock;
        }
        break;

    case MAX30102_IOC_SET_MODE:
        if (copy_from_user(&mode, (void __user *)arg, sizeof(mode))) {
            dev_err(&data->client->dev, "Failed to copy mode from user\n");
            ret = -EFAULT;
            goto unlock;
        }
        ret = max30102_set_mode(data, mode);
        break;

    case MAX30102_IOC_SET_SLOT:
        if (copy_from_user(&slot_config, (void __user *)arg, sizeof(slot_config))) {
            dev_err(&data->client->dev, "Failed to copy slot config from user\n");
            ret = -EFAULT;
            goto unlock;
        }
        if (slot_config.slot < 1 || slot_config.slot > 4 || slot_config.led > 2) {
            dev_err(&data->client->dev, "Invalid slot=%d or led=%d\n", slot_config.slot, slot_config.led);
            ret = -EINVAL;
            goto unlock;
        }
        ret = max30102_set_slot(data, slot_config.slot, slot_config.led);
        break;

    case MAX30102_IOC_SET_FIFO_CONFIG:
        if (copy_from_user(&config, (void __user *)arg, sizeof(config))) {
            dev_err(&data->client->dev, "Failed to copy FIFO config from user\n");
            ret = -EFAULT;
            goto unlock;
        }
        ret = max30102_set_fifo_config(data, config);
        break;

    case MAX30102_IOC_SET_SPO2_CONFIG:
        if (copy_from_user(&config, (void __user *)arg, sizeof(config))) {
            dev_err(&data->client->dev, "Failed to copy SpO2 config from user\n");
            ret = -EFAULT;
            goto unlock;
        }
        ret = max30102_set_spo2_config(data, config);
        break;

    default:
        dev_err(&data->client->dev, "Invalid IOCTL command: 0x%x\n", cmd);
        ret = -ENOTTY;
        goto unlock;
    }

    ret = 0;

unlock:
    mutex_unlock(&data->lock);
    return ret;
}