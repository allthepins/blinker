#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "iot_button.h"
#include "button_gpio.h"

static const char *TAG = "ATP_BLINKER";

bool blinker_on = false;  // blinker state

/**
 * GPIO for led
 */
#define BLINK_GPIO GPIO_NUM_2   // GPIO 2 for esp32 devkit V1 built in LED
#define BUTTON_GPIO GPIO_NUM_0  // GPIO 0 for esp32 devkit V1 BOOT button

QueueHandle_t gpio_evt_queue;   // queue where interrupt events are sent

/**
 * Button component event callback function
 */
static void button_event_cb(void *arg, void *data)
{
  iot_button_print_event((button_handle_t)arg);
  uint32_t gpio_num = (uint32_t) data;
  xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

/**
 * Toggle LED state on reception of event in gpio_evt_queue
 */
void blinker_toggle_task(void *arg)
{
  uint32_t io_num;
  while (true)
  {
    if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
    {
      ESP_LOGI(TAG, "Button (GPIO [%"PRIu32"]) was pressed", io_num);
      blinker_on = !blinker_on; // toggle state
    }
  }
}

/**
 * Setup button component
 */
void button_init(uint32_t button_num)
{
  button_config_t btn_cfg = {
    .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
    .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
  };
  button_gpio_config_t gpio_cfg = {
    .gpio_num = button_num,
    .active_level = 0,
  };

  button_handle_t btn;
  esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn);
  assert(ret == ESP_OK);
  
  ret = iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, button_event_cb, (void *)button_num);

  ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
  /* reset LED pin to default state */
  gpio_reset_pin(BLINK_GPIO);

  /* set LED GPIO direction */
  gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

  /* create a queue to handle button events */
  gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
  /* start blinker state toggle task */
  xTaskCreate(blinker_toggle_task, "Blinker control task", 2048, NULL, 1, NULL);

  button_init(BUTTON_GPIO);

  while (1)
  {
    if ( blinker_on ) // blink LED
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
}