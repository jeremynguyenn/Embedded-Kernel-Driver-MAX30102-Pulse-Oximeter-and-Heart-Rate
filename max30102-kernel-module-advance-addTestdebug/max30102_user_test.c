#include <gtest/gtest.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <pthread.h>
#include <mqueue.h>
#include <string.h>
#include "max30102.h"

static int fd = -1;
static volatile sig_atomic_t running = 1;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static mqd_t mq;

struct max30102_fifo_data fifo_data;
float temp;

// Mock IOCTL for testing
int mock_ioctl(int fd, unsigned int cmd, void *arg) {
    if (cmd == MAX30102_IOC_READ_FIFO) {
        fifo_data.len = 2;
        fifo_data.red[0] = 0x123456;
        fifo_data.ir[0] = 0x789ABC;
        memcpy(arg, &fifo_data, sizeof(fifo_data));
        return 0;
    } else if (cmd == MAX30102_IOC_READ_TEMP) {
        temp = 25.0625;
        memcpy(arg, &temp, sizeof(temp));
        return 0;
    }
    return -1;
}

// Signal handler
static void signal_handler(int sig) {
    running = 0;
}

// Thread function for FIFO
void *fifo_thread(void *arg) {
    char buf[256];
    while (running) {
        pthread_mutex_lock(&mutex);
        if (mock_ioctl(fd, MAX30102_IOC_READ_FIFO, &fifo_data) < 0) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        snprintf(buf, sizeof(buf), "FIFO: %d samples", fifo_data.len);
        mq_send(mq, buf, strlen(buf) + 1, 0);
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
        usleep(100000); // Simulate delay
    }
    return NULL;
}

// Thread function for temperature
void *temp_thread(void *arg) {
    while (running) {
        pthread_mutex_lock(&mutex);
        if (mock_ioctl(fd, MAX30102_IOC_READ_TEMP, &temp) < 0) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        pthread_mutex_unlock(&mutex);
        usleep(500000); // Simulate delay
    }
    return NULL;
}

TEST(Max30102Test, OpenDevice) {
    fd = open("/dev/null", O_RDWR); // Mock device
    ASSERT_GE(fd, 0);
}

TEST(Max30102Test, SignalHandling) {
    signal(SIGUSR1, signal_handler);
    running = 1;
    raise(SIGUSR1);
    ASSERT_EQ(running, 0);
}

TEST(Max30102Test, FifoThread) {
    struct mq_attr attr = { .mq_maxmsg = 10, .mq_msgsize = 256 };
    mq = mq_open("/max30102_test_mq", O_CREAT | O_RDWR, 0666, &attr);
    ASSERT_NE(mq, (mqd_t)-1);

    pthread_t fifo_tid;
    running = 1;
    pthread_create(&fifo_tid, NULL, fifo_thread, NULL);
    usleep(200000); // Let thread run
    running = 0;
    pthread_join(fifo_tid, NULL);

    char buf[256];
    ssize_t ret = mq_receive(mq, buf, 256, NULL);
    ASSERT_GT(ret, 0);
    ASSERT_STREQ(buf, "FIFO: 2 samples");

    mq_close(mq);
    mq_unlink("/max30102_test_mq");
}

TEST(Max30102Test, TempThread) {
    pthread_t temp_tid;
    running = 1;
    pthread_create(&temp_tid, NULL, temp_thread, NULL);
    usleep(200000); // Let thread run
    running = 0;
    pthread_join(temp_tid, NULL);
    ASSERT_NEAR(temp, 25.0625, 0.0001);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}