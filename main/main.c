#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "ATP_BLINKER";

/**
 * GPIO for led
 */
#define BLINK_GPIO GPIO_NUM_2 // GPIO 2 for esp32 devkit V1 built in LED

void app_main(void)
{
  /* reset pins to default state */
  gpio_reset_pin(BLINK_GPIO);

  /* set LED GPIO to output only */
  gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

  while (1)
  {
    /* LED on */
    gpio_set_level(BLINK_GPIO, 1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // wait for 1000 ms
    ESP_LOGI(TAG, "LED on");

    /* LED off */
    gpio_set_level(BLINK_GPIO, 0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "LED off");
  }
}