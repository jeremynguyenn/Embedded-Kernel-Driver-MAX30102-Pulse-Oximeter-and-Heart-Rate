#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <pthread.h>
#include <mqueue.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/mman.h>
#include <semaphore.h>

#define MAX30102_IOC_MAGIC 'k'
#define MAX30102_IOC_READ_FIFO      _IOR(MAX30102_IOC_MAGIC, 0, struct max30102_fifo_data)
#define MAX30102_IOC_READ_TEMP      _IOR(MAX30102_IOC_MAGIC, 1, float)
#define MAX30102_IOC_SET_MODE       _IOW(MAX30102_IOC_MAGIC, 2, uint8_t)
#define MAX30102_IOC_SET_SLOT       _IOW(MAX30102_IOC_MAGIC, 3, struct max30102_slot_config)
#define MAX30102_IOC_SET_FIFO_CONFIG _IOW(MAX30102_IOC_MAGIC, 4, uint8_t)
#define MAX30102_IOC_SET_SPO2_CONFIG _IOW(MAX30102_IOC_MAGIC, 5, uint8_t)

struct max30102_fifo_data {
    unsigned int red[32];
    unsigned int ir[32];
    unsigned char len;
};

struct max30102_slot_config {
    unsigned char slot;
    unsigned char led;
};

struct shared_data {
    float temp;
    int valid;
};

static int fd = -1;
static volatile sig_atomic_t running = 1;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
mqd_t mq;
sem_t *sem;
void *shm_ptr;

static void signal_handler(int sig) {
    running = 0;
    printf("Signal %d received, stopping...\n", sig);
}

void *fifo_thread(void *arg) {
    struct max30102_fifo_data fifo_data;
    char buf[256];

    while (running) {
        pthread_mutex_lock(&mutex);
        if (ioctl(fd, MAX30102_IOC_READ_FIFO, &fifo_data) < 0) {
            perror("Failed to read FIFO data");
            pthread_mutex_unlock(&mutex);
            break;
        }
        snprintf(buf, sizeof(buf), "FIFO: %d samples", fifo_data.len);
        mq_send(mq, buf, strlen(buf) + 1, 0);
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
        sleep(1);
    }
    return NULL;
}

void *temp_thread(void *arg) {
    float temp;
    static int static_var = 0;
    int auto_var = 0;
    struct shared_data *shm_data = (struct shared_data *)shm_ptr;

    while (running) {
        pthread_mutex_lock(&mutex);
        if (ioctl(fd, MAX30102_IOC_READ_TEMP, &temp) < 0) {
            perror("Failed to read temperature");
            pthread_mutex_unlock(&mutex);
            break;
        }
        auto_var++;
        static_var++;
        sem_wait(sem);
        shm_data->temp = temp;
        shm_data->valid = 1;
        sem_post(sem);
        printf("Temp: %.4f°C, Auto: %d, Static: %d\n", temp, auto_var, static_var);
        pthread_mutex_unlock(&mutex);
        sleep(5);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        printf("Arg: %s\n", argv[1]);
    }

    fd = open("/dev/max30102", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    struct mq_attr attr = { .mq_maxmsg = 10, .mq_msgsize = 256 };
    mq = mq_open("/max30102_mq", O_CREAT | O_RDWR, 0666, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        close(fd);
        return 1;
    }

    // Initialize shared memory
    int shm_fd = shm_open("/max30102_shm", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        mq_close(mq);
        close(fd);
        return 1;
    }
    ftruncate(shm_fd, sizeof(struct shared_data));
    shm_ptr = mmap(NULL, sizeof(struct shared_data), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap");
        shm_unlink("/max30102_shm");
        close(shm_fd);
        mq_close(mq);
        close(fd);
        return 1;
    }

    // Initialize semaphore
    sem = sem_open("/max30102_sem", O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        munmap(shm_ptr, sizeof(struct shared_data));
        shm_unlink("/max30102_shm");
        close(shm_fd);
        mq_close(mq);
        close(fd);
        return 1;
    }

    uint8_t mode = 0x03;
    struct max30102_slot_config slot_config = { .slot = 1, .led = 2 };
    uint8_t fifo_config = 0x40;
    uint8_t spo2_config = 0x43;

    ioctl(fd, MAX30102_IOC_SET_FIFO_CONFIG, &fifo_config);
    ioctl(fd, MAX30102_IOC_SET_SPO2_CONFIG, &spo2_config);
    ioctl(fd, MAX30102_IOC_SET_MODE, &mode);
    ioctl(fd, MAX30102_IOC_SET_SLOT, &slot_config);

    pthread_t fifo_tid, temp_tid;
    pthread_create(&fifo_tid, NULL, fifo_thread, NULL);
    pthread_create(&temp_tid, NULL, temp_thread, NULL);

    pid_t pid = fork();
    if (pid == 0) {
        printf("Child PID: %d\n", getpid());
        char *args[] = {"echo", "Hello from exec", NULL};
        execvp("echo", args);
        exit(0);
    } else if (pid > 0) {
        printf("Parent waiting for child %d\n", pid);
        wait(NULL);
    }

    char buf[256];
    struct shared_data *shm_data = (struct shared_data *)shm_ptr;
    while (running) {
        pthread_mutex_lock(&mutex);
        pthread_cond_wait(&cond, &mutex);
        mq_receive(mq, buf, 256, NULL);
        printf("Received from queue: %s\n", buf);
        sem_wait(sem);
        if (shm_data->valid) {
            printf("Shared memory temp: %.4f°C\n", shm_data->temp);
            shm_data->valid = 0;
        }
        sem_post(sem);
        pthread_mutex_unlock(&mutex);
    }

    pthread_join(fifo_tid, NULL);
    pthread_join(temp_tid, NULL);
    sem_close(sem);
    sem_unlink("/max30102_sem");
    munmap(shm_ptr, sizeof(struct shared_data));
    shm_unlink("/max30102_shm");
    close(shm_fd);
    mq_close(mq);
    mq_unlink("/max30102_mq");
    close(fd);
    return 0;
}