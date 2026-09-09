/*
 * FreeRTOS compatibility shim -- implementation on top of the Zephyr kernel.
 *
 * Objects are handed out from fixed-size static pools rather than a heap, so a
 * missing pool entry is a build-time sizing problem instead of a runtime
 * allocation failure.  The pool sizes below cover the whole application; they
 * are asserted at creation time and logged if exhausted.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "freertos_compat.h"

LOG_MODULE_REGISTER(freertos_compat, CONFIG_DJI_REMOTE_LOG_LEVEL);

/* Pool sizes -- see the users in data/, gps/ and logic/ */
#define COMPAT_MAX_SEMAPHORES  20
#define COMPAT_MAX_QUEUES      4
#define COMPAT_MAX_TIMERS      4
#define COMPAT_MAX_TASKS       4

/* Queue backing storage: the largest item the application queues, times the
 * deepest queue it creates. */
#define COMPAT_QUEUE_BUF_SIZE  2048

/* ----------------------------------------------------------------
 *  Timeout conversion
 * ---------------------------------------------------------------- */

static inline k_timeout_t ticks_to_timeout(TickType_t ticks)
{
	if (ticks == portMAX_DELAY) {
		return K_FOREVER;
	}
	if (ticks == 0) {
		return K_NO_WAIT;
	}
	return K_MSEC(ticks);
}

/* ----------------------------------------------------------------
 *  Semaphores and mutexes
 *
 *  Both are backed by a k_sem: the project uses its "mutexes" as plain
 *  non-recursive locks, never relying on priority inheritance or on the
 *  owning-thread check.
 * ---------------------------------------------------------------- */

struct compat_sem {
	struct k_sem sem;
	bool in_use;
};

static struct compat_sem s_sem_pool[COMPAT_MAX_SEMAPHORES];
static K_MUTEX_DEFINE(s_pool_lock);

static SemaphoreHandle_t sem_alloc(unsigned int initial_count)
{
	SemaphoreHandle_t handle = NULL;

	k_mutex_lock(&s_pool_lock, K_FOREVER);
	for (int i = 0; i < COMPAT_MAX_SEMAPHORES; i++) {
		if (!s_sem_pool[i].in_use) {
			s_sem_pool[i].in_use = true;
			handle = &s_sem_pool[i];
			break;
		}
	}
	k_mutex_unlock(&s_pool_lock);

	if (handle == NULL) {
		LOG_ERR("semaphore pool exhausted (%d entries)", COMPAT_MAX_SEMAPHORES);
		return NULL;
	}

	k_sem_init(&handle->sem, initial_count, 1);
	return handle;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
	/* FreeRTOS binary semaphores start empty */
	return sem_alloc(0);
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
	/* FreeRTOS mutexes start available */
	return sem_alloc(1);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
	if (xSemaphore == NULL) {
		return pdFALSE;
	}
	return k_sem_take(&xSemaphore->sem, ticks_to_timeout(xTicksToWait)) == 0
		       ? pdTRUE
		       : pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
	if (xSemaphore == NULL) {
		return pdFALSE;
	}
	k_sem_give(&xSemaphore->sem);
	return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
	if (xSemaphore == NULL) {
		return;
	}
	k_mutex_lock(&s_pool_lock, K_FOREVER);
	xSemaphore->in_use = false;
	k_mutex_unlock(&s_pool_lock);
}

/* ----------------------------------------------------------------
 *  Queues
 * ---------------------------------------------------------------- */

struct compat_queue {
	struct k_msgq msgq;
	char *buf;
	bool in_use;
};

static struct compat_queue s_queue_pool[COMPAT_MAX_QUEUES];
static char s_queue_buf[COMPAT_QUEUE_BUF_SIZE];
static size_t s_queue_buf_used;

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)
{
	QueueHandle_t handle = NULL;
	size_t needed = (size_t)uxQueueLength * (size_t)uxItemSize;

	k_mutex_lock(&s_pool_lock, K_FOREVER);

	if (s_queue_buf_used + needed > sizeof(s_queue_buf)) {
		k_mutex_unlock(&s_pool_lock);
		LOG_ERR("queue buffer exhausted (need %zu, %zu free)", needed,
			sizeof(s_queue_buf) - s_queue_buf_used);
		return NULL;
	}

	for (int i = 0; i < COMPAT_MAX_QUEUES; i++) {
		if (!s_queue_pool[i].in_use) {
			s_queue_pool[i].in_use = true;
			handle = &s_queue_pool[i];
			break;
		}
	}

	if (handle == NULL) {
		k_mutex_unlock(&s_pool_lock);
		LOG_ERR("queue pool exhausted (%d entries)", COMPAT_MAX_QUEUES);
		return NULL;
	}

	handle->buf = &s_queue_buf[s_queue_buf_used];
	s_queue_buf_used += needed;

	k_mutex_unlock(&s_pool_lock);

	k_msgq_init(&handle->msgq, handle->buf, uxItemSize, uxQueueLength);
	return handle;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue,
		      TickType_t xTicksToWait)
{
	if (xQueue == NULL) {
		return pdFALSE;
	}
	return k_msgq_put(&xQueue->msgq, pvItemToQueue,
			  ticks_to_timeout(xTicksToWait)) == 0
		       ? pdTRUE
		       : pdFALSE;
}

BaseType_t xQueueSendFromISR(QueueHandle_t xQueue, const void *pvItemToQueue,
			     BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	if (xQueue == NULL) {
		return pdFALSE;
	}
	return k_msgq_put(&xQueue->msgq, pvItemToQueue, K_NO_WAIT) == 0 ? pdTRUE
								       : pdFALSE;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer,
			 TickType_t xTicksToWait)
{
	if (xQueue == NULL) {
		return pdFALSE;
	}
	return k_msgq_get(&xQueue->msgq, pvBuffer,
			  ticks_to_timeout(xTicksToWait)) == 0
		       ? pdTRUE
		       : pdFALSE;
}

void vQueueDelete(QueueHandle_t xQueue)
{
	if (xQueue == NULL) {
		return;
	}
	k_msgq_purge(&xQueue->msgq);
	k_mutex_lock(&s_pool_lock, K_FOREVER);
	xQueue->in_use = false;
	k_mutex_unlock(&s_pool_lock);
}

/* ----------------------------------------------------------------
 *  Software timers
 *
 *  Backed by delayed work so the callbacks run in thread context, like
 *  FreeRTOS' timer service task.
 * ---------------------------------------------------------------- */

struct compat_timer {
	struct k_work_delayable work;
	TimerCallbackFunction_t cb;
	void *id;
	TickType_t period;
	bool auto_reload;
	bool in_use;
};

static struct compat_timer s_timer_pool[COMPAT_MAX_TIMERS];

static void compat_timer_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct compat_timer *timer = CONTAINER_OF(dwork, struct compat_timer, work);

	if (timer->cb != NULL) {
		timer->cb(timer);
	}

	if (timer->auto_reload) {
		k_work_reschedule(&timer->work, K_MSEC(timer->period));
	}
}

TimerHandle_t xTimerCreate(const char *pcTimerName, TickType_t xTimerPeriod,
			   BaseType_t uxAutoReload, void *pvTimerID,
			   TimerCallbackFunction_t pxCallbackFunction)
{
	ARG_UNUSED(pcTimerName);

	TimerHandle_t handle = NULL;

	k_mutex_lock(&s_pool_lock, K_FOREVER);
	for (int i = 0; i < COMPAT_MAX_TIMERS; i++) {
		if (!s_timer_pool[i].in_use) {
			s_timer_pool[i].in_use = true;
			handle = &s_timer_pool[i];
			break;
		}
	}
	k_mutex_unlock(&s_pool_lock);

	if (handle == NULL) {
		LOG_ERR("timer pool exhausted (%d entries)", COMPAT_MAX_TIMERS);
		return NULL;
	}

	handle->cb = pxCallbackFunction;
	handle->id = pvTimerID;
	handle->period = xTimerPeriod;
	handle->auto_reload = (uxAutoReload != pdFALSE);
	k_work_init_delayable(&handle->work, compat_timer_handler);

	return handle;
}

BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
	ARG_UNUSED(xTicksToWait);

	if (xTimer == NULL) {
		return pdFALSE;
	}
	k_work_reschedule(&xTimer->work, K_MSEC(xTimer->period));
	return pdPASS;
}

BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
	ARG_UNUSED(xTicksToWait);

	if (xTimer == NULL) {
		return pdFALSE;
	}
	k_work_cancel_delayable(&xTimer->work);
	return pdPASS;
}

void *pvTimerGetTimerID(TimerHandle_t xTimer)
{
	return xTimer != NULL ? xTimer->id : NULL;
}

/* ----------------------------------------------------------------
 *  Tasks
 * ---------------------------------------------------------------- */

struct compat_task {
	struct k_thread thread;
	k_thread_stack_t *stack;
	void (*entry)(void *);
	void *arg;
	bool in_use;
};

static struct compat_task s_task_pool[COMPAT_MAX_TASKS];

static void compat_task_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct compat_task *task = p1;

	task->entry(task->arg);
}

/*
 * Map a FreeRTOS priority (higher number = more urgent) onto a Zephyr
 * preemptible priority (lower number = more urgent).  The application only
 * uses priorities 3..6, which land in the middle of Zephyr's range.
 */
static int map_priority(UBaseType_t uxPriority)
{
	int prio = CONFIG_NUM_PREEMPT_PRIORITIES - 1 - (int)uxPriority;

	if (prio < 0) {
		prio = 0;
	}
	if (prio > CONFIG_NUM_PREEMPT_PRIORITIES - 1) {
		prio = CONFIG_NUM_PREEMPT_PRIORITIES - 1;
	}
	return prio;
}

BaseType_t xTaskCreate(void (*pvTaskCode)(void *), const char *pcName,
		       uint32_t usStackDepth, void *pvParameters,
		       UBaseType_t uxPriority, TaskHandle_t *pxCreatedTask)
{
	struct compat_task *task = NULL;

	k_mutex_lock(&s_pool_lock, K_FOREVER);
	for (int i = 0; i < COMPAT_MAX_TASKS; i++) {
		if (!s_task_pool[i].in_use) {
			s_task_pool[i].in_use = true;
			task = &s_task_pool[i];
			break;
		}
	}
	k_mutex_unlock(&s_pool_lock);

	if (task == NULL) {
		LOG_ERR("task pool exhausted (%d entries)", COMPAT_MAX_TASKS);
		return pdFAIL;
	}

	/* usStackDepth is in bytes, following ESP-IDF's xTaskCreate() */
	task->stack = k_thread_stack_alloc(usStackDepth, 0);
	if (task->stack == NULL) {
		LOG_ERR("no memory for the '%s' task stack (%u bytes)", pcName,
			usStackDepth);
		task->in_use = false;
		return pdFAIL;
	}

	task->entry = pvTaskCode;
	task->arg = pvParameters;

	k_thread_create(&task->thread, task->stack, usStackDepth,
			compat_task_entry, task, NULL, NULL,
			map_priority(uxPriority), 0, K_NO_WAIT);
	k_thread_name_set(&task->thread, pcName);

	if (pxCreatedTask != NULL) {
		*pxCreatedTask = task;
	}
	return pdPASS;
}

void vTaskDelete(TaskHandle_t xTask)
{
	if (xTask == NULL) {
		/* FreeRTOS: NULL means "delete the calling task" */
		k_thread_abort(k_current_get());
		return;
	}

	k_thread_abort(&xTask->thread);
	k_mutex_lock(&s_pool_lock, K_FOREVER);
	xTask->in_use = false;
	k_mutex_unlock(&s_pool_lock);
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask)
{
	struct k_thread *thread = (xTask == NULL) ? k_current_get() : &xTask->thread;
	size_t unused = 0;

	if (k_thread_stack_space_get(thread, &unused) != 0) {
		return 0;
	}
	return (UBaseType_t)unused;
}
