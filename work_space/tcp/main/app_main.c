/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <time.h>
#include "driver/gpio.h"
#include "led_strip.h"

#define BUTTON_GPIO GPIO_NUM_3
#define BLINK_GPIO CONFIG_BLINK_GPIO


static const char *LED_TAG = "LED";
static led_strip_t *pStrip_a;
static const char *TAG = "mqtt_example";
static QueueHandle_t xQueueHandle = NULL;
static QueueHandle_t gpio_evt_queue = NULL;

esp_mqtt_client_handle_t client;
TaskHandle_t xHandle = NULL;


int mqtt_status = 0; //0 = "off"
int flags = 0;


static void blink_red_led(void * pvParameters)
{
    while(1){
        /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
        pStrip_a->set_pixel(pStrip_a, 0, 50, 0, 0);
        /* Refresh the strip to send data */
        pStrip_a->refresh(pStrip_a, 100);
        vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
    }
}

static void blink_green_led(void * pvParameters)
{
    while(1){
        /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
        pStrip_a->set_pixel(pStrip_a, 0, 0, 50, 0);
        /* Refresh the strip to send data */
        pStrip_a->refresh(pStrip_a, 100);
        vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
    }
}

static void configure_led(void)
{
    ESP_LOGI(LED_TAG, "Example configured to blink addressable LED!");
    /* LED strip initialization with the GPIO and pixels number*/
    pStrip_a = led_strip_init(CONFIG_BLINK_LED_RMT_CHANNEL, BLINK_GPIO, 1);
    /* Set all LED off to clear all pixels */
    pStrip_a->clear(pStrip_a, 50);
}




static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{

    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    //TaskHandle_t xHandle = NULL;
    //configure_led();






    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "status", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:

        //ここが呼び出されるたびに，message をサブスクライブし，スイッチの状態を確認する
        msg_id = esp_mqtt_client_subscribe(client, "status", 0);
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        printf("%d\n\n\n", event->data_len);
        
        int b = event->data_len;
        
        char *event_data_str = (char *)malloc(b+ 1);
        if (event_data_str == NULL) {
            printf("Memory allocation failed\n");
        }

        strncpy(event_data_str, event->data, event->data_len);

        event_data_str[event->data_len] = '\0';

        int a = strncmp(event_data_str, "{\"On\":false}",b);
        printf("a = %d\n", a);


        if (strcmp(event_data_str, "{\"On\":false}") == 0){
            mqtt_status = 0;
            printf("OFF\n\n");
        }
        else{
            mqtt_status = 1;
            printf("ON\n\n");
        } 

        if (mqtt_status == 0)
        {
            if (flags == 1)
            {
                vTaskDelete(xHandle);
                //vTaskDelay(pdMS_TO_TICKS(400));
                printf("called\n");

            }
            printf("RED\n\n");
            xTaskCreate(blink_red_led, "blink_red_led", 2048, NULL, 10, &xHandle);
            flags = 1;
        }
        else{
            if (flags == 1)
            {
                vTaskDelete(xHandle);
                //vTaskDelay(pdMS_TO_TICKS(400));
                printf("called\n");

            }
            printf("GREEN\n\n");
            xTaskCreate(blink_green_led, "blink_green_led", 2048, NULL, 10, &xHandle);
            flags = 1;
        }   
        free(event_data_str);

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}



static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
    };
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    /* コンフィグの情報に則り，MQTT クライアントを作成  */
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    
    /*作成した MQTT client を起動する */
    esp_mqtt_client_start(client);

    
    int num = 0;
    int mes = 0;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&io_conf);

    gpio_install_isr_service(0);

    gpio_isr_handler_add(BUTTON_GPIO, gpio_isr_handler, (void*) BUTTON_GPIO);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    //xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL);



    printf("start: \n\n");

    

    
    while(1){

        num = gpio_get_level(BUTTON_GPIO);

        if(num != 1){
            if (mqtt_status == 0)
            {
                esp_mqtt_client_publish(client, "test", "OFF",0,1,0);
                mqtt_status = 1;
            }
            else{
                esp_mqtt_client_publish(client, "test", "ON", 0, 1, 0);
                mqtt_status = 0;
            }
            
        }
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    
    
}





void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    configure_led();

    mqtt_app_start();
}
