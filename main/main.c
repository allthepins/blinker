#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "iot_button.h"
#include "button_gpio.h"

static const char *TAG = "ATP_BLINKER";

/**
 * GPIO for led
 */
#define BLINK_GPIO GPIO_NUM_2                       // GPIO 2 for esp32 devkit V1 built in LED
#define BUTTON_GPIO GPIO_NUM_0                      // GPIO 0 for esp32 devkit V1 BOOT button

/**
 * Wi-Fi config
 */
#define BLINKER_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define BLINKER_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define BLINKER_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_STATION_BLINKER_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define BLINKER_H2E_IDENTIFIER ""
#elif CONFIG_ESP_STATION_BLINKER_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define BLINKER_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_BLINKER_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define BLINKER_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

esp_timer_handle_t led_periodic_timer;              // timer that toggles LED
const uint64_t LED_PERIODIC_TIMER_PERIOD = 1000000; // timer period of 1 second

/* event group to signal wifi connection state */
static EventGroupHandle_t s_wifi_event_group;

/**
 * Bits to indicate
 * - connected to WiFi access point with an IP
 * - failed to connect to access point after maximum number of retries
 */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* Count wifi connection retries */
static int s_retry_num = 0;

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

/**
 * WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    if (s_retry_num < BLINKER_ESP_MAXIMUM_RETRY)
    {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "re-attempting to connect to WiFi access point. Attempt %d of %d", s_retry_num, BLINKER_ESP_MAXIMUM_RETRY);
    }
    else
    {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "failed to connect to the WiFi access point");
  }else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

/**
 * Init WiFi
 */
void wifi_init_sta(void)
{
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

  wifi_config_t wifi_config = {
    .sta = {
      .ssid = BLINKER_ESP_WIFI_SSID,
      .password = BLINKER_ESP_WIFI_PASS,
      .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
      .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
      .sae_h2e_identifier = BLINKER_H2E_IDENTIFIER,
    },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_sta finished");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
  * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
          pdFALSE,
          pdFALSE,
          portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
    * happened. */
  if (bits & WIFI_CONNECTED_BIT)
  {
    ESP_LOGI(TAG, "Connected to SSID: %s", BLINKER_ESP_WIFI_SSID);
  } else if (bits & WIFI_FAIL_BIT)
  {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s", BLINKER_ESP_WIFI_SSID);
  } else
  {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }
}

void app_main(void)
{ 
  /**
   * Blinker
   */

  /* reset LED pin to default state */
  gpio_reset_pin(BLINK_GPIO);

  /* set LED GPIO direction */
  gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

  /* init button and led timer */
  button_init(BUTTON_GPIO);
  led_periodic_timer_init();


  /**
   * Wi-Fi
   */

  //Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL)
  {
    esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
  }

  wifi_init_sta();
}