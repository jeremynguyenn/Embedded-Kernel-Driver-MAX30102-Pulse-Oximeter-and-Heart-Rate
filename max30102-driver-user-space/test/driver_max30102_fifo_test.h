

#ifndef DRIVER_MAX30102_FIFO_TEST_H
#define DRIVER_MAX30102_FIFO_TEST_H

#include "driver_max30102_interface.h"

#ifdef __cplusplus
extern "C"{
#endif

/**
 * @addtogroup max30102_test_driver
 * @{
 */

/**
 * @brief  fifo test irq handler
 * @return status code
 *         - 0 success
 *         - 1 run failed
 * @note   none
 */
uint8_t max30102_fifo_test_irq_handler(void);

/**
 * @brief     fifo test
 * @param[in] times test times
 * @return    status code
 *            - 0 success
 *            - 1 test failed
 * @note      none
 */
uint8_t max30102_fifo_test(uint32_t times);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif
