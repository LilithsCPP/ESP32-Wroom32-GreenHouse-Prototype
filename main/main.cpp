#include <iostream>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

extern "C" {
    void app_main(void);
}

#define LED_GPIO_PIN 18
static const char *TAG = "LED";

void app_main(void) {
    // Настройка GPIO
    gpio_reset_pin(static_cast<gpio_num_t>(LED_GPIO_PIN));
    gpio_set_direction(static_cast<gpio_num_t>(LED_GPIO_PIN), GPIO_MODE_OUTPUT);
    
    ESP_LOGI(TAG, "LED control started on GPIO %d", LED_GPIO_PIN);
    
    while(1) {
        // Включить светодиод
        gpio_set_level(static_cast<gpio_num_t>(LED_GPIO_PIN), 1);
        ESP_LOGI(TAG, "LED ON");
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 секунда
        
        // Выключить светодиод
        gpio_set_level(static_cast<gpio_num_t>(LED_GPIO_PIN), 0);
        ESP_LOGI(TAG, "LED OFF");
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 секунда
    }
}