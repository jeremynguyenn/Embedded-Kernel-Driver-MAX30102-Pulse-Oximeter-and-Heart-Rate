#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/of_device.h>
#include <linux/pm.h>
#include <linux/fs.h>  // For file ops
#include <linux/uaccess.h>  // For copy
#include <linux/debugfs.h>  // Added
#include "max30102.h"

/**
 * max30102_probe - Probe function for MAX30102 I2C device
 * @client: I2C client structure
 * @id: I2C device ID
 * Returns: 0 on success, negative error code on failure
 */
static int max30102_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct max30102_data *data;
    uint8_t part_id;
    int ret;

    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    i2c_set_clientdata(client, data);
    mutex_init(&data->lock);
    INIT_WORK(&data->work, max30102_work_handler);

    /* Verify device ID */
    ret = max30102_read_reg(data, MAX30102_REG_PART_ID, &part_id, 1);
    if (ret || part_id != 0x15) {
        dev_err(&client->dev, "Unsupported device ID: 0x%02x, error=%d\n", part_id, ret);
        return -ENODEV;
    }

    data->miscdev.minor = MISC_DYNAMIC_MINOR;
    data->miscdev.name = devm_kasprintf(&client->dev, GFP_KERNEL, "max30102-%d", client->addr);
    data->miscdev.fops = &max30102_fops;
    ret = misc_register(&data->miscdev);
    if (ret) {
        dev_err(&client->dev, "Failed to register misc device: %d\n", ret);
        return ret;
    }

    data->irq_gpio = devm_gpiod_get(&client->dev, "int", GPIOD_IN);
    if (IS_ERR(data->irq_gpio)) {
        ret = PTR_ERR(data->irq_gpio);
        dev_err(&client->dev, "Failed to get IRQ GPIO: %d\n", ret);
        misc_deregister(&data->miscdev);
        return ret;
    }

    ret = gpiod_to_irq(data->irq_gpio);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to get IRQ number: %d\n", ret);
        misc_deregister(&data->miscdev);
        return ret;
    }

    ret = devm_request_irq(&client->dev, ret, max30102_irq_handler, IRQF_TRIGGER_FALLING, "max30102_irq", data);
    if (ret) {
        dev_err(&client->dev, "Failed to request IRQ: %d\n", ret);
        misc_deregister(&data->miscdev);
        return ret;
    }

    ret = sysfs_create_group(&client->dev.kobj, &max30102_attr_group);
    if (ret) {
        dev_err(&client->dev, "Failed to create sysfs group: %d\n", ret);
        misc_deregister(&data->miscdev);
        return ret;
    }

    // Added debugfs
    data->debug_dir = debugfs_create_dir("max30102", NULL);
    if (data->debug_dir) {
        debugfs_create_u8("status1", 0444, data->debug_dir, (u8 *)data);  // Example
    }

    ret = max30102_init_sensor(data);
    if (ret) {
        dev_err(&client->dev, "Failed to initialize sensor: %d\n", ret);
        sysfs_remove_group(&client->dev.kobj, &max30102_attr_group);
        misc_deregister(&data->miscdev);
        debugfs_remove_recursive(data->debug_dir);
        return ret;
    }

    dev_info(&client->dev, "MAX30102 driver probed successfully, part ID: 0x%02x\n", part_id);
    return 0;
}

/**
 * max30102_remove - Remove function for MAX30102 I2C device
 * @client: I2C client structure
 */
static void max30102_remove(struct i2c_client *client)
{
    struct max30102_data *data = i2c_get_clientdata(client);
    sysfs_remove_group(&client->dev.kobj, &max30102_attr_group);
    misc_deregister(&data->miscdev);
    debugfs_remove_recursive(data->debug_dir);
    mutex_destroy(&data->lock);
}

/**
 * max30102_suspend - Suspend function for power management
 * @dev: Device structure
 * Returns: 0 on success, negative error code on failure
 */
static int max30102_suspend(struct device *dev)
{
    struct max30102_data *data = i2c_get_clientdata(to_i2c_client(dev));
    uint8_t value = 0x80;  // SHDN = 1 (improved from datasheet)
    int ret = max30102_write_reg(data, MAX30102_REG_MODE_CONFIG, &value, 1);
    if (ret)
        dev_err(dev, "Failed to suspend device: %d\n", ret);
    return ret;
}

/**
 * max30102_resume - Resume function for power management
 * @dev: Device structure
 * Returns: 0 on success, negative error code on failure
 */
static int max30102_resume(struct device *dev)
{
    struct max30102_data *data = i2c_get_clientdata(to_i2c_client(dev));
    int ret = max30102_init_sensor(data);
    if (ret)
        dev_err(dev, "Failed to resume device: %d\n", ret);
    return ret;
}

static const struct dev_pm_ops max30102_pm_ops = {
    .suspend = max30102_suspend,
    .resume = max30102_resume,
};

static const struct i2c_device_id max30102_id[] = {
    { "max30102", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, max30102_id);

static const struct of_device_id max30102_of_match[] = {
    { .compatible = "maxim,max30102" },
    { }
};
MODULE_DEVICE_TABLE(of, max30102_of_match);

static struct i2c_driver max30102_driver = {
    .driver = {
        .name = "max30102",
        .of_match_table = max30102_of_match,
        .pm = &max30102_pm_ops,
    },
    .probe = max30102_probe,
    .remove = max30102_remove,
    .id_table = max30102_id,
};

// Added file operations
static ssize_t max30102_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct max30102_data *data = file->private_data;
    struct max30102_fifo_data fifo_data;
    int ret;

    if (file->f_flags & O_NONBLOCK) {
        if (!data->fifo_full) return -EAGAIN;  // Non-blocking
    } else {
        wait_event_interruptible(data->wait_data_ready, data->fifo_full);  // Blocking
    }

    ret = max30102_read_fifo(data, fifo_data.red, fifo_data.ir, &fifo_data.len);
    if (ret) return ret;

    if (count < sizeof(fifo_data)) return -EINVAL;
    if (copy_to_user(buf, &fifo_data, sizeof(fifo_data))) return -EFAULT;

    return sizeof(fifo_data);
}

static ssize_t max30102_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    struct max30102_data *data = file->private_data;
    uint8_t config;
    if (count != sizeof(uint8_t)) return -EINVAL;
    if (copy_from_user(&config, buf, sizeof(uint8_t))) return -EFAULT;
    return max30102_set_mode(data, config);  // Example write for mode
}

static loff_t max30102_llseek(struct file *file, loff_t offset, int whence)
{
    // Simple lseek demo, not really needed but added
    return fixed_size_llseek(file, offset, whence, sizeof(struct max30102_fifo_data));
}

const struct file_operations max30102_fops = {
    .owner = THIS_MODULE,
    .open = max30102_open,
    .unlocked_ioctl = max30102_ioctl,
    .read = max30102_read,
    .write = max30102_write,
    .llseek = max30102_llseek,
};

static ssize_t temperature_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct max30102_data *data = i2c_get_clientdata(to_i2c_client(dev));
    float temp;
    int ret = max30102_read_temperature(data, &temp);
    if (ret)
        return ret;
    return sprintf(buf, "%.4f\n", temp);
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct max30102_data *data = i2c_get_clientdata(to_i2c_client(dev));
    uint8_t status1, status2;
    int ret = max30102_read_reg(data, MAX30102_REG_INTERRUPT_STATUS_1, &status1, 1) ||
              max30102_read_reg(data, MAX30102_REG_INTERRUPT_STATUS_2, &status2, 1);
    if (ret)
        return ret;
    return sprintf(buf, "Status1: 0x%02x, Status2: 0x%02x\n", status1, status2);
}

// Added new sysfs: LED current
static ssize_t led_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct max30102_data *data = i2c_get_clientdata(to_i2c_client(dev));
    uint8_t led1, led2;
    max30102_read_reg(data, MAX30102_REG_LED_PULSE_1, &led1, 1);
    max30102_read_reg(data, MAX30102_REG_LED_PULSE_2, &led2, 1);
    return sprintf(buf, "LED1: 0x%02x, LED2: 0x%02x\n", led1, led2);
}

static ssize_t led_current_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct max30102_data *data = i2c_get_clientdata(to_i2c_client(dev));
    uint8_t value;
    sscanf(buf, "%hhx", &value);
    max30102_write_reg(data, MAX30102_REG_LED_PULSE_1, &value, 1);
    max30102_write_reg(data, MAX30102_REG_LED_PULSE_2, &value, 1);
    return count;
}

static DEVICE_ATTR_RO(temperature);
static DEVICE_ATTR_RO(status);
static DEVICE_ATTR_RW(led_current);  // New

static struct attribute *max30102_attrs[] = {
    &dev_attr_temperature.attr,
    &dev_attr_status.attr,
    &dev_attr_led_current.attr,
    NULL
};

struct attribute_group max30102_attr_group = {
    .attrs = max30102_attrs,
};

module_i2c_driver(max30102_driver);

MODULE_AUTHOR("Nguyen Nhan");
MODULE_DESCRIPTION("MAX30102 Sensor Kernel Module with Enhanced Features");
MODULE_LICENSE("GPL");
