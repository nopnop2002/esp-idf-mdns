/* BSD Socket API Example

	 This example code is in the Public Domain (or CC0 licensed, at your option.)

	 Unless required by applicable law or agreed to in writing, this
	 software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	 CONDITIONS OF ANY KIND, either express or implied.
*/


#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"

#include "parameter.h"

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "MAIN";

static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base,
								int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

esp_err_t wifi_init_sta()
{
	esp_err_t ret_value = ESP_OK;
	s_wifi_event_group = xEventGroupCreate();

	ESP_LOGI(TAG,"ESP-IDF Ver%d.%d", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR);
	ESP_LOGI(TAG,"ESP_IDF_VERSION %d", ESP_IDF_VERSION);

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = CONFIG_ESP_WIFI_SSID,
			.password = CONFIG_ESP_WIFI_PASSWORD
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
		WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
		pdFALSE,			// xClearOnExit
		pdFALSE,			// xWaitForAllBits
		portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
			 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGE(TAG, "Failed to connect to SSID:%s, password:%s",
			 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
		ret_value = ESP_FAIL;
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
		ret_value = ESP_ERR_INVALID_STATE;
	}

	ESP_LOGI(TAG, "wifi_init_sta finished.");
	ESP_LOGI(TAG, "connect to ap SSID:%s", CONFIG_ESP_WIFI_SSID);
	vEventGroupDelete(s_wifi_event_group);
	return ret_value;
}

MessageBufferHandle_t xMessageBufferTrans;
MessageBufferHandle_t xMessageBufferRecv;
TimerHandle_t timerHandle;

// The total number of bytes (not messages) the message buffer will be able to hold at any one time.
size_t xBufferSizeBytes = 1024;
// The size, in bytes, required to hold each item in the message,
size_t xItemSize = 64;


// Timer call back
static void timer_cb(TimerHandle_t xTimer)
{
	char buffer[xItemSize];
	ESP_LOGD(TAG, "timer_cb");
	TickType_t nowTick = xTaskGetTickCount();
	sprintf(buffer,"Hello World!! %"PRIu32, nowTick);
	//BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	//xMessageBufferSendFromISR(xMessageBufferTrans, &buffer, strlen(buffer), &xHigherPriorityTaskWoken);
	xMessageBufferSendFromISR(xMessageBufferTrans, &buffer, strlen(buffer), NULL);
}


static esp_err_t query_mdns_host(const char * host_name, struct esp_ip4_addr *addr)
{
	ESP_LOGI(TAG, "Query A: %s", host_name);

	addr->addr = 0;

	esp_err_t err = mdns_query_a(host_name, 10000,	addr);
	if(err){
		if(err == ESP_ERR_NOT_FOUND){
			ESP_LOGE(TAG, "%s: Host was not found!", host_name);
			return err;
		}
		ESP_LOGE(TAG, "Query Failed: %s", esp_err_to_name(err));
		return err;
	}

	ESP_LOGI(TAG, "Query A: %s.local resolved to: " IPSTR, host_name, IP2STR(addr));
	ESP_LOGI(TAG, "Query A: addr->addr=0x%"PRIx32, addr->addr);
	return ESP_OK;
}

void tcp_client_task(void *pvParameters);

void keyin(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(NULL), "Start");
	char buffer[xItemSize];
	strcpy(buffer, "EXIT");

	uint16_t c;
	while (1) {
		c = fgetc(stdin);
		if (c == 0xffff) {
			vTaskDelay(10);
			continue;
		}
		//ESP_LOGI(pcTaskGetName(NULL), "c=%x", c);
		if (c == 0x0a) {
			ESP_LOGI(pcTaskGetName(NULL), "Push Enter");
			xMessageBufferSendFromISR(xMessageBufferTrans, &buffer, strlen(buffer), NULL);
		}
	}

	/* Never reach here */
	vTaskDelete( NULL );
}

void app_main(void)
{
	//Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	// Start WiFi
	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	wifi_init_sta();

	// Resolved ip address using mDNS
	ESP_ERROR_CHECK( mdns_init() );
	struct esp_ip4_addr addr;
	//ret = query_mdns_host("esp32", &addr);
	ret = query_mdns_host(CONFIG_MDNS_HOSTNAME, &addr);
	if (ret != ESP_OK) {
		while(1) { vTaskDelay(1); }
	}
	ESP_LOGD(TAG, "Query A: %s.local resolved to: " IPSTR, CONFIG_MDNS_HOSTNAME, IP2STR(&addr));
	ESP_LOGD(TAG, IPSTR, IP2STR(&addr));

	// Create MessageBuffer
	xMessageBufferTrans = xMessageBufferCreate(xBufferSizeBytes);
	configASSERT( xMessageBufferTrans );
	xMessageBufferRecv = xMessageBufferCreate(xBufferSizeBytes);
	configASSERT( xMessageBufferRecv );

	// Create Timer
	timerHandle = xTimerCreate("Trigger", 5000/portTICK_PERIOD_MS, pdTRUE, NULL, timer_cb);
	configASSERT( timerHandle );

	// Start client task
	PARAMETER_t param;
	param.port = CONFIG_TCP_PORT;
	sprintf(param.ipv4, IPSTR, IP2STR(&addr));
	param.taskHandle = xTaskGetCurrentTaskHandle();
	xTaskCreate(tcp_client_task, "tcp_client", 1024*4, (void *)&param, 5, NULL);

	// Start keyboard task
	xTaskCreate(keyin, "KEYIN", 1024*2, NULL, 5, NULL);

	ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
	ESP_LOGI(TAG, "ulTaskNotifyTake");

	// Start Timer
	if (xTimerStart(timerHandle, 0) != pdPASS) {
		ESP_LOGE(TAG, "Unable to start Timer");
		while(1) { vTaskDelay(1); }
	} else {
		ESP_LOGI(TAG, "Success to start Timer");
	}

	while(1) {
		vTaskDelay(1);
	}
}
