/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "protocol_examples_common.h"
#include <string.h>
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>


#define PORT                        CONFIG_EXAMPLE_PORT
#define KEEPALIVE_IDLE              CONFIG_EXAMPLE_KEEPALIVE_IDLE
#define KEEPALIVE_INTERVAL          CONFIG_EXAMPLE_KEEPALIVE_INTERVAL
#define KEEPALIVE_COUNT             CONFIG_EXAMPLE_KEEPALIVE_COUNT
#define IRCOUNT 8
#define IR_1 GPIO_NUM_32
#define IR_2 GPIO_NUM_33
#define IR_3 GPIO_NUM_25
#define IR_4 GPIO_NUM_26
#define IR_5 GPIO_NUM_27
#define IR_6 GPIO_NUM_14
#define IR_7 GPIO_NUM_12
#define IR_8 GPIO_NUM_13
#define SPILL GPIO_NUM_35
static const char *TAG = "example";
static const char *testMessage = "test message from server";
static const char irArr[IRCOUNT] = {IR_1,IR_2,IR_3,IR_4,IR_5,IR_6,IR_7,IR_8};
static char irLevel[IRCOUNT]= {0};
static char spillLevel = 0;
// function to read ir sensors
static void read_ir_sensors(void *pvParameters)
{
    while(1)
    {
        
        for(int index = 0; index < IRCOUNT; index++)
        {
            irLevel[index] = gpio_get_level(irArr[index]);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

vTaskDelete(NULL);
}

static void read_spill_sensor(void *pvParameters)
{
    while(1)
    { 
        spillLevel = gpio_get_level(SPILL);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

vTaskDelete(NULL);
}



static void do_retransmit(const int sock)
{
    int len = 0;
    int msgLen = 0;
    int msgSpillLen = 0;
    char rx_buffer[128];
    
   do {
         vTaskDelay(pdMS_TO_TICKS(500));
         char msgIR[20] = "01";
         char msgSpill[10] = "021";
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
            ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

            // send() can return less bytes than supplied length.
            // Walk-around for robust implementation.
            //len = 25;
            //int to_write = len;
            for(int index = 1; index <= IRCOUNT;index++)
            {
                char tempBuf[8];
                if(irLevel[index])
                {
                    sprintf(tempBuf, "%d",index);
                    strcat(msgIR,tempBuf);
                }
            }

            msgLen = strlen(msgIR);
            int to_write = msgLen;
            while (to_write > 0) {
                int written = send(sock, msgIR + (msgLen - to_write), to_write, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    // Failed to retransmit, giving up
                    return;
                }
                to_write -= written;
            }
            if(spillLevel)
            {
                msgSpillLen = strlen(msgSpill);
                int to_write_spill = msgSpillLen;
                while (to_write_spill > 0) {
                    int written = send(sock, msgSpill + (msgSpillLen - to_write_spill), to_write_spill, 0);
                    if (written < 0) {
                        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                        // Failed to retransmit, giving up
                        return;
                    }
                    to_write_spill -= written;
                }
            }
        }
    } while (len > 0);
}

static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

#ifdef CONFIG_EXAMPLE_IPV4
    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    }
#endif
#ifdef CONFIG_EXAMPLE_IPV6
    if (addr_family == AF_INET6) {
        struct sockaddr_in6 *dest_addr_ip6 = (struct sockaddr_in6 *)&dest_addr;
        bzero(&dest_addr_ip6->sin6_addr.un, sizeof(dest_addr_ip6->sin6_addr.un));
        dest_addr_ip6->sin6_family = AF_INET6;
        dest_addr_ip6->sin6_port = htons(PORT);
        ip_protocol = IPPROTO_IPV6;
    }
#endif

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
#ifdef CONFIG_EXAMPLE_IPV4
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
#endif
#ifdef CONFIG_EXAMPLE_IPV6
        if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
#endif
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        do_retransmit(sock);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}
void configPorts(void)
{
    gpio_set_direction(IR_1, GPIO_MODE_INPUT);
    gpio_set_direction(IR_2, GPIO_MODE_INPUT);
    gpio_set_direction(IR_3, GPIO_MODE_INPUT);
    gpio_set_direction(IR_4, GPIO_MODE_INPUT);
    gpio_set_direction(IR_5, GPIO_MODE_INPUT);
    gpio_set_direction(IR_6, GPIO_MODE_INPUT);
    gpio_set_direction(IR_7, GPIO_MODE_INPUT);
    gpio_set_direction(IR_8, GPIO_MODE_INPUT);
    gpio_set_direction(SPILL, GPIO_MODE_INPUT);
    gpio_set_pull_mode(IR_1, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(IR_2, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(IR_3, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(IR_4, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(IR_5, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(IR_6, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(IR_7, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(IR_8, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(SPILL, GPIO_PULLDOWN_ONLY);

}
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    configPorts();
    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

#ifdef CONFIG_EXAMPLE_IPV4
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
#endif
#ifdef CONFIG_EXAMPLE_IPV6
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET6, 5, NULL);
#endif
    xTaskCreate(read_ir_sensors, "read_ir", 4096, NULL, 5, NULL);
     xTaskCreate(read_spill_sensor, "read_spill", 4096, NULL, 5, NULL);
}
