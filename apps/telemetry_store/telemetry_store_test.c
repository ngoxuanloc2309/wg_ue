#include "telemetry_store.h"
#include "FreeRTOS.h"
#include "task.h"
#include "logger.h"

static const char *TAG = "TelemetryTest";
static telemetry_id_t s_test_id;
static volatile uint32_t s_torn_reads = 0;
static volatile uint32_t s_successful_reads = 0;
static volatile bool s_writer_done = false;

static void test_writer_task(void *params) {
    (void)params;
    uint32_t i = 1;
    while (i <= 500000) {
        telemetry_value_t val;
        val.type = TELEMETRY_TYPE_UINT32;
        val.value.u32 = i;
        telemetry_store_update(s_test_id, &val, TELEMETRY_VALUE_GOOD, i);
        i++;
        if (i % 5000 == 0) vTaskDelay(1); /* Let watchdog and other tasks breathe */
    }
    s_writer_done = true;
    vTaskDelete(NULL);
}

static void test_reader_task(void *params) {
    (void)params;
    telemetry_point_snapshot_t snap;
    while (!s_writer_done) {
        if (telemetry_store_get(s_test_id, &snap) == 0) {
            if (snap.timestamp > 0 && snap.value.value.u32 != snap.timestamp) {
                s_torn_reads++;
            }
            s_successful_reads++;
        }
        if (s_successful_reads % 5000 == 0) vTaskDelay(1);
    }
    vTaskDelete(NULL);
}

void telemetry_store_run_test(void) {
    LOGI(TAG, "Starting Telemetry Store SeqLock test...");
    telemetry_store_init();
    
    if (telemetry_store_register_point("test.point", TELEMETRY_TYPE_UINT32, &s_test_id) != 0) {
        LOGE(TAG, "Failed to register point");
        return;
    }
    
    s_torn_reads = 0;
    s_successful_reads = 0;
    s_writer_done = false;
    
    /* Use lower priority so we don't starve system tasks like Watchdog or NetManager */
    TaskHandle_t tw, tr;
    xTaskCreate(test_writer_task, "TS_WR", 1024, NULL, tskIDLE_PRIORITY + 2, &tw);
    xTaskCreate(test_reader_task, "TS_RD", 1024, NULL, tskIDLE_PRIORITY + 2, &tr);
    
    /* Wait for writer to finish 500,000 iterations */
    while (!s_writer_done) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    vTaskDelay(pdMS_TO_TICKS(50)); // let reader exit
    
    LOGI(TAG, "Telemetry Store Test Completed!");
    LOGI(TAG, "Successful Reads: %lu", s_successful_reads);
    LOGI(TAG, "Torn Reads: %lu", s_torn_reads);
    if (s_torn_reads == 0 && s_successful_reads > 100) {
        LOGI(TAG, "[SUCCESS] SeqLock perfectly prevented torn reads!");
    } else {
        LOGE(TAG, "[FAILED] SeqLock test failed.");
    }
}