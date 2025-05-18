#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "iot_button.h"
#include "button_gpio.h"

static const char *TAG = "ATP_BLINKER";

/**
 * GPIO for led
 */
#define BLINK_GPIO GPIO_NUM_2                       // GPIO 2 for esp32 devkit V1 built in LED
#define BUTTON_GPIO GPIO_NUM_0                      // GPIO 0 for esp32 devkit V1 BOOT button

esp_timer_handle_t led_periodic_timer;              // timer that toggles LED
const uint64_t LED_PERIODIC_TIMER_PERIOD = 1000000; // timer period of 1 second

/**
 * Button component event callback function
 */
static void button_event_cb(void *arg, void *data)
{
  iot_button_print_event((button_handle_t)arg);
  if (esp_timer_is_active(led_periodic_timer))
  {
    ESP_ERROR_CHECK(esp_timer_stop(led_periodic_timer));
    gpio_set_level(BLINK_GPIO, 0);
  }
  else
  {
    gpio_set_level(BLINK_GPIO, 1);
    ESP_ERROR_CHECK(esp_timer_start_periodic(led_periodic_timer, LED_PERIODIC_TIMER_PERIOD));
  }
}

/**
 * LED periodic timer callback function
 */
static void led_periodic_timer_callback(void *arg)
{
  ESP_LOGI(TAG, "LED Periodic timer called");

  static bool ON;
  ESP_LOGI(TAG, "LED ON is: %d", ON);
  ON = !ON;

  gpio_set_level(BLINK_GPIO, ON);
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

/**
 * Setup LED periodic timer
 */
void led_periodic_timer_init(void){
  const esp_timer_create_args_t periodic_timer_args = {
    .callback = &led_periodic_timer_callback,
    .name = "led_periodic_timer",
  };

  ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &led_periodic_timer));
}

void app_main(void)
{
  /* reset LED pin to default state */
  gpio_reset_pin(BLINK_GPIO);

  /* set LED GPIO direction */
  gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

  /* init button and led timer */
  button_init(BUTTON_GPIO);
  led_periodic_timer_init();
}