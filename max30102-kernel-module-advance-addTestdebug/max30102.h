#ifndef MAX30102_H
#define MAX30102_H

#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/gpio/consumer.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/debugfs.h>

/* MAX30102 Register Definitions */
#define MAX30102_ADDRESS                0x57
#define MAX30102_REG_INTERRUPT_STATUS_1 0x00
#define MAX30102_REG_INTERRUPT_STATUS_2 0x01
#define MAX30102_REG_INTERRUPT_ENABLE_1 0x02
#define MAX30102_REG_INTERRUPT_ENABLE_2 0x03
#define MAX30102_REG_FIFO_WRITE_POINTER 0x04
#define MAX30102_REG_OVERFLOW_COUNTER   0x05
#define MAX30102_REG_FIFO_READ_POINTER  0x06
#define MAX30102_REG_FIFO_DATA          0x07
#define MAX30102_REG_FIFO_CONFIG        0x08
#define MAX30102_REG_MODE_CONFIG        0x09
#define MAX30102_REG_SPO2_CONFIG        0x0A
#define MAX30102_REG_LED_PULSE_1        0x0C
#define MAX30102_REG_LED_PULSE_2        0x0D
#define MAX30102_REG_MULTI_LED_MODE_1   0x11
#define MAX30102_REG_MULTI_LED_MODE_2   0x12
#define MAX30102_REG_DIE_TEMP_INTEGER   0x1F
#define MAX30102_REG_DIE_TEMP_FRACTION  0x20
#define MAX30102_REG_DIE_TEMP_CONFIG    0x21
#define MAX30102_REG_REVISION_ID        0xFE
#define MAX30102_REG_PART_ID            0xFF

/* Interrupt Status Types */
enum max30102_interrupt_status {
    MAX30102_INT_FIFO_FULL    = 7,
    MAX30102_INT_PPG_RDY      = 6,
    MAX30102_INT_ALC_OVF      = 5,
    MAX30102_INT_PWR_RDY      = 0,
    MAX30102_INT_DIE_TEMP_RDY = 1,
};

/* Added from datasheet: Sample Averaging Options */
enum max30102_smp_ave {
    SMP_AVE_1 = 0,
    SMP_AVE_2 = 1,
    SMP_AVE_4 = 2,
    SMP_AVE_8 = 3,
    SMP_AVE_16 = 4,
    SMP_AVE_32 = 5,
};

/* IOCTL Definitions */
#define MAX30102_IOC_MAGIC 'k'
#define MAX30102_IOC_READ_FIFO      _IOR(MAX30102_IOC_MAGIC, 0, struct max30102_fifo_data)
#define MAX30102_IOC_READ_TEMP      _IOR(MAX30102_IOC_MAGIC, 1, float)
#define MAX30102_IOC_SET_MODE       _IOW(MAX30102_IOC_MAGIC, 2, uint8_t)
#define MAX30102_IOC_SET_SLOT       _IOW(MAX30102_IOC_MAGIC, 3, struct max30102_slot_config)
#define MAX30102_IOC_SET_FIFO_CONFIG _IOW(MAX30102_IOC_MAGIC, 4, uint8_t)
#define MAX30102_IOC_SET_SPO2_CONFIG _IOW(MAX30102_IOC_MAGIC, 5, uint8_t)

struct max30102_fifo_data {
    uint32_t red[32];
    uint32_t ir[32];
    uint8_t len;
};

struct max30102_slot_config {
    uint8_t slot;
    uint8_t led;
};

struct max30102_data {
    struct i2c_client *client;
    struct mutex lock;
    struct work_struct work;
    struct gpio_desc *irq_gpio;
    struct miscdevice miscdev;
    uint32_t red_data[32];
    uint32_t ir_data[32];
    uint8_t data_len;
    bool fifo_full;
    wait_queue_head_t wait_data_ready;
    struct dentry *debug_dir;
};

extern const struct file_operations max30102_fops;
extern void max30102_work_handler(struct work_struct *work);
extern irqreturn_t max30102_irq_handler(int irq, void *dev_id);
extern int max30102_write_reg(struct max30102_data *data, uint8_t reg, uint8_t *buf, uint16_t len);
extern int max30102_read_reg(struct max30102_data *data, uint8_t reg, uint8_t *buf, uint16_t len);
extern int max30102_init_sensor(struct max30102_data *data);
extern int max30102_set_mode(struct max30102_data *data, uint8_t mode);
extern int max30102_set_slot(struct max30102_data *data, uint8_t slot, uint8_t led);
extern int max30102_set_interrupt(struct max30102_data *data, uint8_t interrupt, bool enable);
extern int max30102_read_fifo(struct max30102_data *data, uint32_t *red, uint32_t *ir, uint8_t *len);
extern int max30102_read_temperature(struct max30102_data *data, float *temp);
extern int max30102_set_fifo_config(struct max30102_data *data, uint8_t config);
extern int max30102_set_spo2_config(struct max30102_data *data, uint8_t config);
extern int max30102_debug_init(struct max30102_data *data);
extern void max30102_debug_cleanup(struct max30102_data *data);

/* Sysfs Attributes */
extern struct attribute_group max30102_attr_group;

/* Tracepoints */
DECLARE_TRACE(max30102_fifo_read,
              TPARGS(data, len),
              TPSTRUCT__entry(
                  __field(void *, data)
                  __field(uint8_t, len)
              ));

DECLARE_TRACE(max30102_interrupt,
              TPARGS(data, status1, status2),
              TPSTRUCT__entry(
                  __field(void *, data)
                  __field(uint8_t, status1)
                  __field(uint8_t, status2)
              ));

DECLARE_TRACE(max30102_fifo_access,
              TPARGS(data, len),
              TPSTRUCT__entry(
                  __field(void *, data)
                  __field(uint8_t, len)
              ));

DECLARE_TRACE(max30102_temp_read,
              TPARGS(data, temp),
              TPSTRUCT__entry(
                  __field(void *, data)
                  __field(float, temp)
              ));

#endif