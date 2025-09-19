

#ifndef DRIVER_MAX30102_REGISTER_TEST_H
#define DRIVER_MAX30102_REGISTER_TEST_H

#include "driver_max30102_interface.h"

#ifdef __cplusplus
extern "C"{
#endif

/**
 * @defgroup max30102_test_driver max30102 test driver function
 * @brief    max30102 test driver modules
 * @ingroup  max30102_driver
 * @{
 */

/**
 * @brief  register test
 * @return status code
 *         - 0 success
 *         - 1 test failed
 * @note   none
 */
uint8_t max30102_register_test(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif
