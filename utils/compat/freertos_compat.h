/*
 * FreeRTOS compatibility shim.
 *
 * The original DJI-Remote firmware uses the FreeRTOS API that ESP-IDF exposes:
 * binary semaphores and mutexes, software timers, a queue, task delays and the
 * tick counter.  Re-implementing that small subset on top of the Zephyr kernel
 * keeps logic/, data/ and gps/ line-for-line comparable with the original
 * instead of rewriting them around k_sem / k_msgq / k_work.
 *
 * Only the subset the project actually uses is implemented.  New code should
 * use the native Zephyr APIs directly.
 *
 * Notable semantics:
 * - One tick is one millisecond.  The original sets CONFIG_FREERTOS_HZ=1000,
 *   so pdMS_TO_TICKS() is the identity and portTICK_PERIOD_MS is 1.
 * - Software timer callbacks run on the Zephyr system workqueue, i.e. in
 *   thread context.  (A raw k_timer would run them in interrupt context, where
 *   the original callbacks -- which log and take mutexes -- are not allowed.)
 * - FreeRTOS priorities count upwards, Zephyr's count downwards; xTaskCreate()
 *   inverts them.
 * - Task stack sizes are in bytes, following ESP-IDF's deviation from vanilla
 *   FreeRTOS (where they are in words).
 */

#ifndef __FREERTOS_COMPAT_H__
#define __FREERTOS_COMPAT_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

/* ----------------------------------------------------------------
 *  Types and constants
 * ---------------------------------------------------------------- */

typedef uint32_t TickType_t;
typedef int      BaseType_t;
typedef unsigned int UBaseType_t;

#define pdTRUE   ((BaseType_t)1)
#define pdFALSE  ((BaseType_t)0)
#define pdPASS   pdTRUE
#define pdFAIL   pdFALSE

#define portTICK_PERIOD_MS  ((TickType_t)1)
#define portMAX_DELAY       ((TickType_t)UINT32_MAX)

#define pdMS_TO_TICKS(ms)   ((TickType_t)(ms))

/* ----------------------------------------------------------------
 *  Tasks
 * ---------------------------------------------------------------- */

typedef struct compat_task *TaskHandle_t;

#define vTaskDelay(ticks)     k_msleep((int32_t)(ticks))
#define xTaskGetTickCount()   ((TickType_t)k_uptime_get_32())

/*
 * usStackDepth is in bytes (ESP-IDF convention), uxPriority counts upwards
 * like FreeRTOS.  pxCreatedTask may be NULL.
 */
BaseType_t xTaskCreate(void (*pvTaskCode)(void *), const char *pcName,
		       uint32_t usStackDepth, void *pvParameters,
		       UBaseType_t uxPriority, TaskHandle_t *pxCreatedTask);

void vTaskDelete(TaskHandle_t xTask);

/* Zephyr reschedules on its own when leaving an ISR */
#define portYIELD_FROM_ISR(...)  do { } while (0)

/* ----------------------------------------------------------------
 *  Semaphores and mutexes
 * ---------------------------------------------------------------- */

typedef struct compat_sem *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);
void vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

/* ----------------------------------------------------------------
 *  Queues
 * ---------------------------------------------------------------- */

typedef struct compat_queue *QueueHandle_t;

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue,
		      TickType_t xTicksToWait);
BaseType_t xQueueSendFromISR(QueueHandle_t xQueue, const void *pvItemToQueue,
			     BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer,
			 TickType_t xTicksToWait);
void vQueueDelete(QueueHandle_t xQueue);

/* ----------------------------------------------------------------
 *  Software timers
 * ---------------------------------------------------------------- */

typedef struct compat_timer *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t xTimer);

TimerHandle_t xTimerCreate(const char *pcTimerName, TickType_t xTimerPeriod,
			   BaseType_t uxAutoReload, void *pvTimerID,
			   TimerCallbackFunction_t pxCallbackFunction);
BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait);
BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait);
void *pvTimerGetTimerID(TimerHandle_t xTimer);

#endif /* __FREERTOS_COMPAT_H__ */
