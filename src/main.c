#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <stdatomic.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define SENSOR_PERIOD_MS       10U
#define PROCESSING_QUEUE_LEN   32U
#define LOG_QUEUE_LEN          8U
#define SIMULATION_TIME_MS     10000U
#define STACK_SIZE             2048U

#define PRIORITY_PROTECTION    4U
#define PRIORITY_SENSOR        3U
#define PRIORITY_PROCESSING    2U
#define PRIORITY_LOGGER        1U
#define PRIORITY_REPORT        5U

typedef struct {
    uint32_t sequence;
    TickType_t tick;
    float vibration;
    float temperature;
    float current;
} SensorSample;

typedef struct {
    uint32_t sequence;
    TickType_t tick;
} LogRecord;

static QueueHandle_t sensor_queue;
static QueueHandle_t alarm_queue;
static QueueHandle_t log_queue;

static atomic_uint samples_processed;
static atomic_uint sensor_drops;
static atomic_uint log_drops;
static atomic_uint deadline_misses;
static atomic_uint alarm_count;
static atomic_uint max_alarm_latency_ms;

static void busy_work_ms(uint32_t duration_ms)
{
    struct timespec start, now;
    uint64_t elapsed_ns;

    clock_gettime(CLOCK_MONOTONIC, &start);

    do {
        clock_gettime(CLOCK_MONOTONIC, &now);

        elapsed_ns =
            (uint64_t)(now.tv_sec - start.tv_sec) * 1000000000ULL;

        if (now.tv_nsec >= start.tv_nsec) {
            elapsed_ns += (uint64_t)(now.tv_nsec - start.tv_nsec);
        } else {
            elapsed_ns -= (uint64_t)(start.tv_nsec - now.tv_nsec);
        }
    } while (elapsed_ns < (uint64_t)duration_ms * 1000000ULL);
}

static void update_max_latency(uint32_t latency_ms)
{
    unsigned int previous =
        atomic_load_explicit(&max_alarm_latency_ms, memory_order_relaxed);

    while (latency_ms > previous &&
           !atomic_compare_exchange_weak_explicit(
               &max_alarm_latency_ms, &previous, latency_ms,
               memory_order_relaxed, memory_order_relaxed)) {
    }
}

static void sensor_task(void *argument)
{
    (void)argument;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(SENSOR_PERIOD_MS);
    uint32_t sequence = 0U;
    int alarm_sent = 0;

    for (;;) {
        vTaskDelayUntil(&last_wake, period);

        TickType_t now = xTaskGetTickCount();

        if ((TickType_t)(now - last_wake) > pdMS_TO_TICKS(2U)) {
            atomic_fetch_add(&deadline_misses, 1U);
        }

        SensorSample sample = {
            .sequence = ++sequence,
            .tick = now,
            .vibration = 1.5F,
            .temperature = 55.0F,
            .current = 5.0F
        };

        /* Simulated dangerous motor condition begins at 2 seconds. */
        if (sequence >= 200U && sequence < 260U) {
            sample.vibration = 8.5F;
            sample.temperature = 92.0F;
            sample.current = 18.0F;
        }

        if (xQueueSend(sensor_queue, &sample, 0U) != pdPASS) {
            atomic_fetch_add(&sensor_drops, 1U);
        }

        if (sequence % 10U == 0U) {
            LogRecord record = {
                .sequence = sequence,
                .tick = now
            };

            if (xQueueSend(log_queue, &record, 0U) != pdPASS) {
                atomic_fetch_add(&log_drops, 1U);
            }
        }

        if (!alarm_sent &&
            (sample.vibration > 7.0F ||
             sample.temperature > 80.0F ||
             sample.current > 15.0F)) {

            if (xQueueSend(alarm_queue, &sample, 0U) == pdPASS) {
                alarm_sent = 1;
            }
        }
    }
}

static void protection_task(void *argument)
{
    (void)argument;
    SensorSample alarm;

    for (;;) {
        if (xQueueReceive(alarm_queue, &alarm, portMAX_DELAY) == pdPASS) {
            TickType_t now = xTaskGetTickCount();
            uint32_t latency_ms =
                (uint32_t)((now - alarm.tick) * 1000U /
                           configTICK_RATE_HZ);

            update_max_latency(latency_ms);
            atomic_fetch_add(&alarm_count, 1U);

            /* Simulated safety output: latch the motor shutdown. */
            printf("\n[PROTECTION] MOTOR SHUTDOWN"
                   " | sample=%lu | response=%lu ms\n",
                   (unsigned long)alarm.sequence,
                   (unsigned long)latency_ms);
            fflush(stdout);
        }
    }
}

static void processing_task(void *argument)
{
    (void)argument;
    SensorSample sample;

    for (;;) {
        if (xQueueReceive(sensor_queue, &sample, portMAX_DELAY) == pdPASS) {
            TickType_t now = xTaskGetTickCount();

            /* Inject overload after 5 seconds: work exceeds sensor period. */
            uint32_t work_ms =
                (now >= pdMS_TO_TICKS(5000U)) ? 15U : 3U;

            busy_work_ms(work_ms);
            atomic_fetch_add(&samples_processed, 1U);
        }
    }
}

static void logger_task(void *argument)
{
    (void)argument;
    LogRecord record;

    for (;;) {
        if (xQueueReceive(log_queue, &record, portMAX_DELAY) == pdPASS) {
            /* Simulate slow storage without blocking other tasks. */
            vTaskDelay(pdMS_TO_TICKS(250U));
            printf("[LOGGER] Stored sample %lu\n",
                   (unsigned long)record.sequence);
        }
    }
}

static void report_task(void *argument)
{
    (void)argument;

    printf("Industrial Sensor Supervisor started.\n");
    printf("Hazard injection: 2 s | Processing overload: 5 s\n\n");
    fflush(stdout);

    vTaskDelay(pdMS_TO_TICKS(SIMULATION_TIME_MS));

    printf("\n========== TEST SUMMARY ==========\n");
    printf("Samples processed : %u\n", atomic_load(&samples_processed));
    printf("Sensor queue drops: %u\n", atomic_load(&sensor_drops));
    printf("Log queue drops   : %u\n", atomic_load(&log_drops));
    printf("Late sensor cycles: %u\n", atomic_load(&deadline_misses));
    printf("Shutdown alarms   : %u\n", atomic_load(&alarm_count));
    printf("Max alarm response: %u ms\n",
           atomic_load(&max_alarm_latency_ms));
    printf("==================================\n");
    fflush(stdout);

    exit(EXIT_SUCCESS);
}



void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    fprintf(stderr, "FATAL: Stack overflow in task %s\n", task_name);
    fflush(stderr);
    abort();
}


int main(void)
{
    sensor_queue = xQueueCreate(PROCESSING_QUEUE_LEN,
                                sizeof(SensorSample));
    alarm_queue = xQueueCreate(4U, sizeof(SensorSample));
    log_queue = xQueueCreate(LOG_QUEUE_LEN, sizeof(LogRecord));

    if (sensor_queue == NULL || alarm_queue == NULL || log_queue == NULL) {
        fprintf(stderr, "Failed to allocate FreeRTOS queues.\n");
        return EXIT_FAILURE;
    }

    if (xTaskCreate(protection_task, "Protection", STACK_SIZE, NULL,
                    PRIORITY_PROTECTION, NULL) != pdPASS ||
        xTaskCreate(sensor_task, "Sensor", STACK_SIZE, NULL,
                    PRIORITY_SENSOR, NULL) != pdPASS ||
        xTaskCreate(processing_task, "Processing", STACK_SIZE, NULL,
                    PRIORITY_PROCESSING, NULL) != pdPASS ||
        xTaskCreate(logger_task, "Logger", STACK_SIZE, NULL,
                    PRIORITY_LOGGER, NULL) != pdPASS ||
        xTaskCreate(report_task, "Report", STACK_SIZE, NULL,
                    PRIORITY_REPORT, NULL) != pdPASS) {
        fprintf(stderr, "Failed to create FreeRTOS tasks.\n");
        return EXIT_FAILURE;
    }

    vTaskStartScheduler();

    fprintf(stderr, "FreeRTOS scheduler returned unexpectedly.\n");
    return EXIT_FAILURE;
}
