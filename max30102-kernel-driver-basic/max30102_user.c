#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

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

int main(void)
{
    int fd = open("/dev/max30102", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    struct max30102_fifo_data fifo_data;
    float temp;
    uint8_t mode = 0x03; /* SpO2 mode */
    struct max30102_slot_config slot_config = { .slot = 1, .led = 2 }; /* Slot 1: IR LED */
    uint8_t fifo_config = 0x40; /* Sample averaging = 4, rollover enabled */
    uint8_t spo2_config = 0x43; /* ADC range 16384, 50Hz, 18-bit */

    /* Set FIFO configuration */
    if (ioctl(fd, MAX30102_IOC_SET_FIFO_CONFIG, &fifo_config) < 0) {
        perror("Failed to set FIFO config");
        close(fd);
        return 1;
    }
    printf("Set FIFO config: 0x%02x\n", fifo_config);

    /* Set SpO2 configuration */
    if (ioctl(fd, MAX30102_IOC_SET_SPO2_CONFIG, &spo2_config) < 0) {
        perror("Failed to set SpO2 config");
        close(fd);
        return 1;
    }
    printf("Set SpO2 config: 0x%02x\n", spo2_config);

    /* Set mode */
    if (ioctl(fd, MAX30102_IOC_SET_MODE, &mode) < 0) {
        perror("Failed to set mode");
        close(fd);
        return 1;
    }
    printf("Set mode to SpO2\n");

    /* Set slot */
    if (ioctl(fd, MAX30102_IOC_SET_SLOT, &slot_config) < 0) {
        perror("Failed to set slot");
        close(fd);
        return 1;
    }
    printf("Set slot %d to IR LED\n", slot_config.slot);

    /* Read FIFO data */
    if (ioctl(fd, MAX30102_IOC_READ_FIFO, &fifo_data) < 0) {
        perror("Failed to read FIFO data");
        close(fd);
        return 1;
    }

    printf("FIFO Data: %d samples\n", fifo_data.len);
    for (int i = 0; i < fifo_data.len; i++) {
        printf("Sample %d: Red=%u, IR=%u\n", i, fifo_data.red[i], fifo_data.ir[i]);
    }

    /* Read temperature */
    if (ioctl(fd, MAX30102_IOC_READ_TEMP, &temp) < 0) {
        perror("Failed to read temperature");
        close(fd);
        return 1;
    }
    printf("Temperature: %.4f°C\n", temp);

    if (close(fd) < 0) {
        perror("Failed to close device");
        return 1;
    }
    return 0;
}