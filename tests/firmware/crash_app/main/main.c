#include "esp_log.h"
#include <stdlib.h>

void app_main(void) {
    ESP_LOGE("crashtest", "BOOT crashtest: aborting before app_switch_mark_healthy()");
    abort();
}
