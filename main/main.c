#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "ATP_BLINKER";

bool blinker_on = false;  // blinker state

/**
 * GPIO for led
 */
#define BLINK_GPIO GPIO_NUM_2   // GPIO 2 for esp32 devkit V1 built in LED
#define BUTTON_GPIO GPIO_NUM_0  // GPIO 0 for esp32 devkit V1 BOOT button

QueueHandle_t gpio_evt_queue;   // queue where interrupt events are sent

static void IRAM_ATTR gpio_interrupt_handler(void *arg)
{
  uint32_t gpio_num = (uint32_t) arg;
  xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

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

void app_main(void)
{
  /* reset pins to default state */
  gpio_reset_pin(BLINK_GPIO);
  gpio_reset_pin(BUTTON_GPIO);

  /* set GPIO directions */
  gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);

  /* enable pulldown for input pin */
  gpio_pulldown_en(BUTTON_GPIO);
  /* disable pullup for input pin */
  gpio_pullup_dis(BUTTON_GPIO);
  /* set rising edge interrupt type */
  gpio_set_intr_type(BUTTON_GPIO, GPIO_INTR_POSEDGE);

  /* create a queue to handle gpio events from isr */
  gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
  /* start blinker state toggle task */
  xTaskCreate(blinker_toggle_task, "Blinker control task", 2048, NULL, 1, NULL);

  /* install isr service */
  gpio_install_isr_service(0);
  /* hook isr handler up to button gpio */
  gpio_isr_handler_add(BUTTON_GPIO, gpio_interrupt_handler, (void *)BUTTON_GPIO);
  

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