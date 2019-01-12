#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "u8g2_esp32_hal.h"

#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "config.h"


#define PIN_SDA 5
#define PIN_SCL 4

static u8g2_t u8g2;

// Event group
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

static void print_screen(char *s1, char *s2, char *s3);

// HTTP request
static const char *REQUEST = "GET "CONFIG_PATH" HTTP/1.1\n"
	"Host: "CONFIG_URL"\n"
	"User-Agent: ESP32\n"
	"\n";

static void print_screen(char *s1, char *s2, char *s3)
{
	u8g2_ClearBuffer(&u8g2);
	u8g2_DrawStr(&u8g2, 2,17,s1);
	u8g2_DrawStr(&u8g2, 2,37,s2);
	u8g2_DrawStr(&u8g2, 2,57,s3);
	u8g2_SendBuffer(&u8g2);
	vTaskDelay(300 / portTICK_RATE_MS);
}

// Wifi event handler
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
	switch(event->event_id) {
	case SYSTEM_EVENT_STA_START:
		esp_wifi_connect();
		break;
	case SYSTEM_EVENT_STA_GOT_IP:
		xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
		break;
	default:
		break;
	}
	return ESP_OK;
}

// Main task
void main_task(void *pvParameter)
{
	// wait for connection
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
	printf("connected!\n");
	print_screen("wifi connected", "", "");
	printf("\n");
	
	// print the local IP address
	tcpip_adapter_ip_info_t ip_info;
	ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
	printf("IP Address:  %s\n", ip4addr_ntoa(&ip_info.ip));
	printf("Subnet mask: %s\n", ip4addr_ntoa(&ip_info.netmask));
	printf("Gateway:     %s\n", ip4addr_ntoa(&ip_info.gw));
	printf("\n");
	print_screen("IP Address:", ip4addr_ntoa(&ip_info.ip), "");
	
	// define connection parameters
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	
	// address info struct and receive buffer
	struct addrinfo *res;
	char recv_buf[100];
	
	// resolve the IP of the target website
	int result = getaddrinfo(CONFIG_URL, CONFIG_PORT, &hints, &res);
	if((result != 0) || (res == NULL)) {
		printf("Unable to resolve IP for target website %s\n", CONFIG_URL);
		while(1) vTaskDelay(1000 / portTICK_RATE_MS);
	}
	printf("Target website's IP resolved\n");
	
	// create a new socket
	int s = socket(res->ai_family, res->ai_socktype, 0);
	if(s < 0) {
		printf("Unable to allocate a new socket\n");
		while(1) vTaskDelay(1000 / portTICK_RATE_MS);
	}
	printf("Socket allocated, id=%d\n", s);
	
	// connect to the specified server
	result = connect(s, res->ai_addr, res->ai_addrlen);
	if(result != 0) {
		printf("Unable to connect to the target website\n");
		close(s);
		while(1) vTaskDelay(1000 / portTICK_RATE_MS);
	}
	printf("Connected to the target website\n");
	print_screen("Connected to the", "target website","");
	
	// send the request
	result = write(s, REQUEST, strlen(REQUEST));
		if(result < 0) {
		printf("Unable to send the HTTP request\n");
		close(s);
		while(1) vTaskDelay(1000 / portTICK_RATE_MS);
	}
	printf("HTTP request sent\n");
	print_screen("http request", "", "");
	
	// print the response
	printf("HTTP response:\n");
	printf("--------------------------------------------------------------------------------\n");
	int r;
	do {
		bzero(recv_buf, sizeof(recv_buf));
		r = read(s, recv_buf, sizeof(recv_buf) - 1);
		for(int i = 0; i < r; i++) {
			putchar(recv_buf[i]);
		}
	} while(r > 0);	
	printf("--------------------------------------------------------------------------------\n");

	close(s);
	printf("Socket closed\n");
	
	while(1) {
		vTaskDelay(5000 / portTICK_RATE_MS);
	}
}

void app_main() {

	// initialize the u8g2 hal
	u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
	u8g2_esp32_hal.sda = PIN_SDA;
	u8g2_esp32_hal.scl = PIN_SCL;
	u8g2_esp32_hal_init(u8g2_esp32_hal);

	// initialize the u8g2 library
	u8g2_Setup_ssd1306_i2c_128x64_noname_f(
		&u8g2,
		U8G2_R0,
		u8g2_esp32_i2c_byte_cb,
		u8g2_esp32_gpio_and_delay_cb);
	
	// set the display address
	u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);
	
	// initialize the display
	u8g2_InitDisplay(&u8g2);
	
	// wake up the display
	u8g2_SetPowerSave(&u8g2, 0);

	// set font
	u8g2_SetFont(&u8g2, u8g2_font_timR14_tf);

	print_screen("screen ready", "", "");

	// initialize NVS
	ESP_ERROR_CHECK(nvs_flash_init());

	// -- WIFI --
	// disable the default wifi logging
	esp_log_level_set("wifi", ESP_LOG_NONE);

	// create the event group to handle wifi events
	wifi_event_group = xEventGroupCreate();
		
	// initialize the tcp stack
	tcpip_adapter_init();

	// initialize the wifi event handler
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
	
	// initialize the wifi stack in STAtion mode with config in RAM
	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	// configure the wifi connection and start the interface
	wifi_config_t wifi_config = {
		.sta = {
		    .ssid = CONFIG_WIFI_SSID,
		    .password = CONFIG_WIFI_PASS,
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	/* char buffer[50]; */
	/* snprintf(buffer, sizeof(buffer), "Connecting to\n%s... ", CONFIG_WIFI_SSID); */
	/* print_screen(buffer, "", ""); */
	print_screen("Connectiong to:", CONFIG_WIFI_SSID, "");

	// start the main task
	xTaskCreate(&main_task, "main_task", 2048, NULL, 5, NULL);
}
