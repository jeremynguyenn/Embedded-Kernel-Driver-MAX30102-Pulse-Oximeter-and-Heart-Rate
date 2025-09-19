

#include "gpio.h"
#include <gpiod.h>
#include <pthread.h>

/**
 * @brief gpio device name definition
 */
#define GPIO_DEVICE_NAME "/dev/gpiochip0"        /**< gpio device name */

/**
 * @brief gpio device line definition
 */
#define GPIO_DEVICE_LINE 17                      /**< gpio device line */

/**
 * @brief global var definition
 */
static struct gpiod_chip *gs_chip;        /**< gpio chip handle */
static struct gpiod_line *gs_line;        /**< gpio line handle */
static pthread_t gs_pid;                  /**< gpio pthread pid */
extern uint8_t (*g_gpio_irq)(void);       /**< gpio irq */

/**
 * @brief  gpio interrupt pthread
 * @param  *p pointer to an args buffer
 * @return NULL
 * @note   none
 */
static void *a_gpio_interrupt_pthread(void *p)
{
    int res;
    struct gpiod_line_event event;
    
    /* enable catching cancel signal */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    /* cancel the pthread at once */
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* loop */
    while (1)
    {
        /* wait for the event */
        res = gpiod_line_event_wait(gs_line, NULL);
        if (res == 1)
        {
            /* read the event */
            if (gpiod_line_event_read(gs_line, &event) != 0)
            {
                continue;
            }

            /* if the falling edge */
            if (event.event_type == GPIOD_LINE_EVENT_FALLING_EDGE)
            {
                /* check the g_gpio_irq */
                if (g_gpio_irq != NULL)
                {
                    /* run the callback */
                    g_gpio_irq();
                }
            }
        }
    }
}

/**
 * @brief  gpio interrupt init
 * @return status code
 *         - 0 success
 *         - 1 init failed
 * @note   none
 */
uint8_t gpio_interrupt_init(void)
{
    uint8_t res;
    
    /* open the gpio group */
    gs_chip = gpiod_chip_open(GPIO_DEVICE_NAME);
    if (gs_chip == NULL)
    {
        perror("gpio: open failed.\n");

        return 1;
    }
    
    /* get the gpio line */
    gs_line = gpiod_chip_get_line(gs_chip, GPIO_DEVICE_LINE);
    if (gs_line == NULL) 
    {
        perror("gpio: get line failed.\n");
        gpiod_chip_close(gs_chip);

        return 1;
    }

    /* catch the falling edge */
    if (gpiod_line_request_falling_edge_events(gs_line, "gpiointerrupt") < 0)
    {
        perror("gpio: set edge events failed.\n");
        gpiod_chip_close(gs_chip);

        return 1;
    }

    /* creat a gpio interrupt pthread */
    res = pthread_create(&gs_pid, NULL, a_gpio_interrupt_pthread, NULL);
    if (res != 0)
    {
        perror("gpio: creat pthread failed.\n");
        gpiod_chip_close(gs_chip);

        return 1;
    }

    return 0;
}

/**
 * @brief  gpio interrupt deinit
 * @return status code
 *         - 0 success
 *         - 1 deinit failed
 * @note   none
 */
uint8_t gpio_interrupt_deinit(void)
{
    uint8_t res;
    
    /* close the gpio interrupt pthread */
    res = pthread_cancel(gs_pid);
    if (res != 0)
    {
        perror("gpio: delete pthread failed.\n");

        return 1;
    }

    /* close the gpio */
    gpiod_chip_close(gs_chip);
    
    return 0;
}
