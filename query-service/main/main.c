/* MDNS-SD Query and advertise Example

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
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_mac.h" // esp_read_mac
#include "mdns.h"

static const char *TAG = "MAIN";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
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

esp_err_t wifi_init_sta(void)
{
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
		ESP_EVENT_ANY_ID,
		&event_handler,
		NULL,
		&instance_any_id));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
		IP_EVENT_STA_GOT_IP,
		&event_handler,
		NULL,
		&instance_got_ip));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = CONFIG_ESP_WIFI_SSID,
			.password = CONFIG_ESP_WIFI_PASSWORD,
			/* Setting a password implies station will connect to all security modes including WEP/WPA.
			 * However these modes are deprecated and not advisable to be used. Incase your Access point
			 * doesn't support WPA2, these mode can be enabled by commenting below line */
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,

			.pmf_cfg = {
				.capable = true,
				.required = false
			},
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	//ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
	ESP_ERROR_CHECK(esp_wifi_start() );

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	esp_err_t ret_value = ESP_OK;
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
		WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
		pdFALSE,
		pdFALSE,
		portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
		ret_value = ESP_FAIL;
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
		ret_value = ESP_FAIL;
	}

	/* The event will not be processed after unregister */
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
	vEventGroupDelete(s_wifi_event_group);
	return ret_value;
}


/** Generate host name based on sdkconfig, optionally adding a portion of MAC address to it.
 *	@return host name string allocated from the heap
 */
static char* generate_hostname(void)
{
	uint8_t mac[6];
	char   *hostname;
	esp_read_mac(mac, ESP_MAC_WIFI_STA);
	if (-1 == asprintf(&hostname, "%s-%02X%02X%02X", CONFIG_MDNS_HOSTNAME, mac[3], mac[4], mac[5])) {
		abort();
	}
	return hostname;
}

static void initialise_mdns(void)
{
	char * hostname = generate_hostname();

	//initialize mDNS
	ESP_ERROR_CHECK( mdns_init() );
	//set mDNS hostname (required if you want to advertise services)
	ESP_ERROR_CHECK( mdns_hostname_set(hostname) );
	ESP_LOGI(__FUNCTION__, "mdns hostname set to: [%s]", hostname);
	free(hostname);

	//set default mDNS instance name
	ESP_ERROR_CHECK( mdns_instance_name_set(CONFIG_MDNS_INSTANCE) );
	ESP_LOGI(__FUNCTION__, "mdns instance name set to: [%s]", CONFIG_MDNS_INSTANCE);

#if CONFIG_MDNS_TXT
	//structure with TXT records
	mdns_txt_item_t serviceTxtData[3] = {
		{"board", "esp32"},
		{"u", "user"},
		{"p", "password"}
	};
#endif

	//initialize service
	char service_type[64];
	sprintf(service_type, "_service_%d", CONFIG_UDP_PORT); //prepended with underscore
	ESP_LOGI(__FUNCTION__, "service_type=[%s]", service_type);
#if CONFIG_MDNS_TXT
	ESP_ERROR_CHECK( mdns_service_add(NULL, service_type, "_udp", CONFIG_UDP_PORT, serviceTxtData, 3) );
#else
	ESP_ERROR_CHECK( mdns_service_add(NULL, service_type, "_udp", CONFIG_UDP_PORT, NULL, 0) );
#endif
}

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
/* these strings match tcpip_adapter_if_t enumeration */
static const char * if_str[] = {"STA", "AP", "ETH", "MAX"};
#endif

/* these strings match mdns_ip_protocol_t enumeration */
static const char * ip_protocol_str[] = {"V4", "V6", "MAX"};

static void mdns_print_results(mdns_result_t *results, char * hostname, char * ip, uint16_t *port)
{
	mdns_result_t *r = results;
	mdns_ip_addr_t *a = NULL;
	int i = 1, t;
	while (r) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
		esp_netif_get_desc(r->esp_netif);
		printf("%d: Interface: %s, Type: %s, TTL: %"PRIu32"\n",
			i++, esp_netif_get_desc(r->esp_netif), ip_protocol_str[r->ip_protocol], r->ttl);

#else
		printf("%d: Interface: %s, Type: %s, TTL: %u\n", 
			i++, if_str[r->tcpip_if], ip_protocol_str[r->ip_protocol], r->ttl);
#endif
		if (r->instance_name) {
			printf("  PTR : %s.%s.%s\n", r->instance_name, r->service_type, r->proto);
		}
		if (r->hostname) {
			printf("  SRV : %s.local:%u\n", r->hostname, r->port);
			strcpy(hostname, r->hostname);
			*port = r->port;
		}
		if (r->txt_count) {
			printf("  TXT : [%zu] ", r->txt_count);
			for (t = 0; t < r->txt_count; t++) {
				printf("%s=%s(%d); ", r->txt[t].key, r->txt[t].value ? r->txt[t].value : "NULL", r->txt_value_len[t]);
			}
			printf("\n");
		}
		a = r->addr;
		while (a) {
			if (a->addr.type == ESP_IPADDR_TYPE_V6) {
				printf("  AAAA: " IPV6STR "\n", IPV62STR(a->addr.u_addr.ip6));
			} else {
				printf("  A   : " IPSTR "\n", IP2STR(&(a->addr.u_addr.ip4)));
				sprintf(ip, IPSTR, IP2STR(&(a->addr.u_addr.ip4)));
			}
			a = a->next;
		}
		r = r->next;
	}
}

static void query_mdns_service(const char * service_name, const char * proto)
{
	ESP_LOGI(__FUNCTION__, "Query PTR: %s.%s.local", service_name, proto);

	mdns_result_t * results = NULL;
	esp_err_t err = mdns_query_ptr(service_name, proto, 3000, 20,  &results);
	if(err){
		ESP_LOGE(__FUNCTION__, "Query Failed: %s", esp_err_to_name(err));
		return;
	}
	if(!results){
		ESP_LOGW(__FUNCTION__, "No results found!");
		return;
	}

	char hostname[64];
	char ip[16];
	uint16_t port;
	mdns_print_results(results, hostname, ip, &port);
	ESP_LOGD(__FUNCTION__, "hostname=[%s] ip=[%s] port=[%d]", hostname, ip, port);
	mdns_query_results_free(results);
}

#if 0
static void query_mdns_host(const char * host_name)
{
	ESP_LOGI(__FUNCTION__, "Query A: %s.local", host_name);

	struct esp_ip4_addr addr;
	addr.addr = 0;

	esp_err_t err = mdns_query_a(host_name, 2000,  &addr);
	if(err){
		if(err == ESP_ERR_NOT_FOUND){
			ESP_LOGW(__FUNCTION__, "%s: Host was not found!", esp_err_to_name(err));
			return;
		}
		ESP_LOGE(__FUNCTION__, "Query Failed: %s", esp_err_to_name(err));
		return;
	}

	ESP_LOGI(__FUNCTION__, "Query A: %s.local resolved to: " IPSTR, host_name, IP2STR(&addr));
}
#endif

void app_main(void)
{
	// Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	// Initialize WiFi
	wifi_init_sta();

	// Initialize mDNS
	initialise_mdns();

	char service_type[64];
	sprintf(service_type, "_service_%d", CONFIG_UDP_PORT); //prepended with underscore
	while(1) {
		ESP_LOGI(TAG, "looking for [%s] on mDNS", service_type);
		query_mdns_service(service_type, "_udp");
		vTaskDelay(100);
	}
}
