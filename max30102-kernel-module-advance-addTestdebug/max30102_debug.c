#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include "max30102.h"

/**
 * max30102_debug_dump_registers - Dump all MAX30102 registers to seq_file
 * @data: MAX30102 device data
 * @seq: Sequence file for output
 * Returns: 0 on success
 */
static int max30102_debug_dump_registers(struct max30102_data *data, struct seq_file *seq)
{
    uint8_t reg_values[0xFF + 1];
    int ret, i;

    mutex_lock(&data->lock);
    for (i = 0; i <= 0xFF; i++) {
        if (i == 0x08 || (i >= 0x0C && i <= 0x12) || i == 0x1F || i == 0x20 || i == 0x21 || i == 0xFE || i == 0xFF) {
            ret = max30102_read_reg(data, i, &reg_values[i], 1);
            if (ret) {
                seq_printf(seq, "Failed to read register 0x%02x: %d\n", i, ret);
                mutex_unlock(&data->lock);
                return ret;
            }
        } else {
            reg_values[i] = 0xFF; // Mark unused registers
        }
    }
    mutex_unlock(&data->lock);

    seq_printf(seq, "MAX30102 Register Dump:\n");
    seq_printf(seq, "Interrupt Status 1 (0x00): 0x%02x\n", reg_values[0x00]);
    seq_printf(seq, "Interrupt Status 2 (0x01): 0x%02x\n", reg_values[0x01]);
    seq_printf(seq, "Interrupt Enable 1 (0x02): 0x%02x\n", reg_values[0x02]);
    seq_printf(seq, "Interrupt Enable 2 (0x03): 0x%02x\n", reg_values[0x03]);
    seq_printf(seq, "FIFO Write Pointer (0x04): 0x%02x\n", reg_values[0x04]);
    seq_printf(seq, "Overflow Counter (0x05): 0x%02x\n", reg_values[0x05]);
    seq_printf(seq, "FIFO Read Pointer (0x06): 0x%02x\n", reg_values[0x06]);
    seq_printf(seq, "FIFO Config (0x08): 0x%02x\n", reg_values[0x08]);
    seq_printf(seq, "Mode Config (0x09): 0x%02x\n", reg_values[0x09]);
    seq_printf(seq, "SpO2 Config (0x0A): 0x%02x\n", reg_values[0x0A]);
    seq_printf(seq, "LED Pulse 1 (0x0C): 0x%02x\n", reg_values[0x0C]);
    seq_printf(seq, "LED Pulse 2 (0x0D): 0x%02x\n", reg_values[0x0D]);
    seq_printf(seq, "Multi-LED Mode 1 (0x11): 0x%02x\n", reg_values[0x11]);
    seq_printf(seq, "Multi-LED Mode 2 (0x12): 0x%02x\n", reg_values[0x12]);
    seq_printf(seq, "Die Temp Integer (0x1F): 0x%02x\n", reg_values[0x1F]);
    seq_printf(seq, "Die Temp Fraction (0x20): 0x%02x\n", reg_values[0x20]);
    seq_printf(seq, "Die Temp Config (0x21): 0x%02x\n", reg_values[0x21]);
    seq_printf(seq, "Revision ID (0xFE): 0x%02x\n", reg_values[0xFE]);
    seq_printf(seq, "Part ID (0xFF): 0x%02x\n", reg_values[0xFF]);
    return 0;
}

/**
 * max30102_debug_dump_fifo - Dump FIFO data to seq_file
 * @data: MAX30102 device data
 * @seq: Sequence file for output
 * Returns: 0 on success
 */
static int max30102_debug_dump_fifo(struct max30102_data *data, struct seq_file *seq)
{
    uint32_t red[32], ir[32];
    uint8_t len;
    int ret;

    mutex_lock(&data->lock);
    ret = max30102_read_fifo(data, red, ir, &len);
    if (ret) {
        seq_printf(seq, "Failed to read FIFO data: %d\n", ret);
        mutex_unlock(&data->lock);
        return ret;
    }
    mutex_unlock(&data->lock);

    seq_printf(seq, "FIFO Data (%d samples):\n", len);
    for (int i = 0; i < len; i++) {
        seq_printf(seq, "Sample %d: Red=0x%08x, IR=0x%08x\n", i, red[i], ir[i]);
    }
    return 0;
}

/**
 * max30102_debug_reg_show - Debugfs show function for registers
 */
static int max30102_debug_reg_show(struct seq_file *seq, void *v)
{
    struct max30102_data *data = seq->private;
    return max30102_debug_dump_registers(data, seq);
}

/**
 * max30102_debug_fifo_show - Debugfs show function for FIFO
 */
static int max30102_debug_fifo_show(struct seq_file *seq, void *v)
{
    struct max30102_data *data = seq->private;
    return max30102_debug_dump_fifo(data, seq);
}

/**
 * max30102_debug_open - Debugfs open function
 */
static int max30102_debug_open(struct inode *inode, struct file *file)
{
    return single_open(file, inode->i_private == (void *)1 ?
                      max30102_debug_reg_show : max30102_debug_fifo_show,
                      inode->i_private);
}

static const struct file_operations max30102_debug_fops = {
    .owner = THIS_MODULE,
    .open = max30102_debug_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

/**
 * max30102_debug_init - Initialize debugfs entries
 * @data: MAX30102 device data
 * Returns: 0 on success
 */
int max30102_debug_init(struct max30102_data *data)
{
    data->debug_dir = debugfs_create_dir("max30102", NULL);
    if (!data->debug_dir) {
        dev_err(&data->client->dev, "Failed to create debugfs directory\n");
        return -ENOMEM;
    }

    if (!debugfs_create_file("registers", 0444, data->debug_dir, (void *)1,
                             &max30102_debug_fops)) {
        dev_err(&data->client->dev, "Failed to create debugfs registers file\n");
        debugfs_remove_recursive(data->debug_dir);
        return -ENOMEM;
    }

    if (!debugfs_create_file("fifo", 0444, data->debug_dir, (void *)2,
                             &max30102_debug_fops)) {
        dev_err(&data->client->dev, "Failed to create debugfs fifo file\n");
        debugfs_remove_recursive(data->debug_dir);
        return -ENOMEM;
    }

    return 0;
}

/**
 * max30102_debug_cleanup - Cleanup debugfs entries
 * @data: MAX30102 device data
 */
void max30102_debug_cleanup(struct max30102_data *data)
{
    debugfs_remove_recursive(data->debug_dir);
}