#include "aws_connection.h"
const int CONNECTED_BIT = BIT0;

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    }
}

void ShadowUpdateStatusCallback(const char *pThingName, ShadowActions_t action, Shadow_Ack_Status_t status,
                                const char *pReceivedJsonDocument, void *pContextData)
{
    IOT_UNUSED(pThingName);
    IOT_UNUSED(action);
    IOT_UNUSED(pReceivedJsonDocument);
    IOT_UNUSED(pContextData);

    shadowUpdateInProgress = false;

    if(SHADOW_ACK_TIMEOUT == status)
    {
        ESP_LOGE(TAG, "Update timed out");
    }
    else if(SHADOW_ACK_REJECTED == status)
    {
        ESP_LOGE(TAG, "Update rejected");
    }
    else if(SHADOW_ACK_ACCEPTED == status)
    {
        ESP_LOGI(TAG, "Update accepted");
    }
}

void socketActuate_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext)
{
    IOT_UNUSED(pJsonString);
    IOT_UNUSED(JsonStringDataLen);
    if(pContext != NULL)
    {
        ESP_LOGI(TAG, "Delta - Socket state changed to %d", *(bool *) (pContext->pData));
    }
}

void aws_iot_task(void *param)
{
	IoT_Error_t rc = FAILURE;
	char JsonDocumentBuffer[MAX_LENGTH_OF_UPDATE_JSON_BUFFER];
	size_t sizeOfJsonDocumentBuffer = sizeof(JsonDocumentBuffer) / sizeof(JsonDocumentBuffer[0]);
	int output_pin[6] = {12, 13, 14, 25, 26, 27};
	bool socket_on_recived[6] = {0, 0, 0, 0, 0, 0};
	bool socket_on_send[6] = {1, 1, 1, 1, 1, 1};
	bool update_needed = false;

	for (int i = 0; i < 6; i++)
	{
		gpio_pad_select_gpio(output_pin[i]);
		gpio_set_direction(output_pin[i], GPIO_MODE_OUTPUT);
	}
	while (1)
	{
		jsonStruct_t socket_actuator[6] = {
				{
				.cb = socketActuate_Callback,
				.pData = &socket_on_recived[0],
				.pKey = "soc1",
				.type = SHADOW_JSON_BOOL,
				.dataLength = sizeof(bool)
				},
				{
				.cb = socketActuate_Callback,
				.pData = &socket_on_recived[1],
				.pKey = "soc2",
				.type = SHADOW_JSON_BOOL,
				.dataLength = sizeof(bool)
				},
				{
				.cb = socketActuate_Callback,
				.pData = &socket_on_recived[2],
				.pKey = "soc3",
				.type = SHADOW_JSON_BOOL,
				.dataLength = sizeof(bool)
				},
				{
				.cb = socketActuate_Callback,
				.pData = &socket_on_recived[3],
				.pKey = "soc4",
				.type = SHADOW_JSON_BOOL,
				.dataLength = sizeof(bool)
				},
				{
				.cb = socketActuate_Callback,
				.pData = &socket_on_recived[4],
				.pKey = "soc5",
				.type = SHADOW_JSON_BOOL,
				.dataLength = sizeof(bool)
				},
				{
				.cb = socketActuate_Callback,
				.pData = &socket_on_recived[5],
				.pKey = "soc6",
				.type = SHADOW_JSON_BOOL,
				.dataLength = sizeof(bool)
				}
		};
		AWS_IoT_Client mqttClient;
		ShadowInitParameters_t sp = ShadowInitParametersDefault;
		sp.pHost = AWS_IOT_MQTT_HOST;
		sp.port = AWS_IOT_MQTT_PORT;
		sp.pClientCRT = (const char *)certificate_pem_crt_start;
		sp.pClientKey = (const char *)private_pem_key_start;
		sp.pRootCA = (const char *)aws_root_ca_pem_start;
		sp.enableAutoReconnect = false;
		sp.disconnectHandler = NULL;
		xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
							false, true, portMAX_DELAY);
		ESP_LOGI(TAG, "Shadow Init");
		rc = aws_iot_shadow_init(&mqttClient, &sp);
		if(SUCCESS != rc)
		{
			ESP_LOGE(TAG, "aws_iot_shadow_init returned error %d, aborting...", rc);
			abort();
		}
		ShadowConnectParameters_t scp = ShadowConnectParametersDefault;
		scp.pMyThingName = CONFIG_AWS_EXAMPLE_THING_NAME;
		scp.pMqttClientId = CONFIG_AWS_EXAMPLE_CLIENT_ID;
		scp.mqttClientIdLen = (uint16_t) strlen(CONFIG_AWS_EXAMPLE_CLIENT_ID);
		ESP_LOGI(TAG, "Shadow Connect");
		rc = aws_iot_shadow_connect(&mqttClient, &scp);
		if(SUCCESS != rc)
		{
			ESP_LOGE(TAG, "aws_iot_shadow_connect returned error %d, aborting...", rc);
			abort();
		}
		rc = aws_iot_shadow_set_autoreconnect_status(&mqttClient, true);
		if(SUCCESS != rc)
		{
			ESP_LOGE(TAG, "Unable to set Auto Reconnect to true - %d, aborting...", rc);
			abort();
		}
		for (int i = 0; i < 6 ; i++)
		{
			rc = aws_iot_shadow_register_delta(&mqttClient, &socket_actuator[i]);
		}
		if(SUCCESS != rc)
		{
			ESP_LOGE(TAG, "Shadow Register Delta Error");
		}
		while(NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc)
		{
			rc = aws_iot_shadow_yield(&mqttClient, 200);
			for (int i = 0; i < 6 ; i++)
				{
					gpio_set_level(output_pin[i], !socket_on_recived[i]);
				}
			if(NETWORK_ATTEMPTING_RECONNECT == rc || shadowUpdateInProgress)
			{
				rc = aws_iot_shadow_yield(&mqttClient, 1000);
				continue;
			}
			update_needed = false;
			for (int i = 0; i < 6; i++)
			{
				if(socket_on_send[i] != socket_on_recived[i])
				{
					update_needed = true;
				}
			}
			rc = aws_iot_shadow_init_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
			if(SUCCESS == rc)
			{
				rc = aws_iot_shadow_add_reported(JsonDocumentBuffer, sizeOfJsonDocumentBuffer, 6, &socket_actuator[0], &socket_actuator[1], &socket_actuator[2], &socket_actuator[3], &socket_actuator[4], &socket_actuator[5]);
				if(SUCCESS == rc)
				{
					rc = aws_iot_finalize_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
					if(SUCCESS == rc && update_needed)
					{
						ESP_LOGI(TAG, "Update Shadow: %s", JsonDocumentBuffer);
						rc = aws_iot_shadow_update(&mqttClient, CONFIG_AWS_EXAMPLE_THING_NAME, JsonDocumentBuffer,
												   ShadowUpdateStatusCallback, NULL, 4, true);
						for (int i = 0; i < 6; i++)
						{
							socket_on_send[i] = socket_on_recived[i];
						}
						shadowUpdateInProgress = true;
					}
				}
			}
			ESP_LOGI(TAG, "*****************************************************************************************");
			ESP_LOGI(TAG, "Stack remaining for task '%s' is %d bytes", pcTaskGetName(NULL), uxTaskGetStackHighWaterMark(NULL));
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}
		if(SUCCESS != rc)
		{
			ESP_LOGE(TAG, "An error occurred in the loop %d", rc);
		}
		ESP_LOGI(TAG, "Disconnecting");
		rc = aws_iot_shadow_disconnect(&mqttClient);
		if(SUCCESS != rc)
		{
			ESP_LOGE(TAG, "Disconnect error %d", rc);
		}
		vTaskDelete(NULL);
	}
}

static void initialise_wifi(void)
{
    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
#ifdef ESP_NETIF_SUPPORTED
    esp_netif_create_default_wifi_sta();
#endif
    wifi_event_group = xEventGroupCreate();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

void init_outputs()
{
	int output_pin[6] = {12, 13, 14, 25, 26, 27};
	for (int i = 0; i < 6; i++)
	{
		gpio_pad_select_gpio(output_pin[i]);
		gpio_set_direction(output_pin[i], GPIO_MODE_OUTPUT);
		gpio_set_level(output_pin[i], 1);
	}
}

void aws_connect()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
    initialise_wifi();
    xTaskCreatePinnedToCore(&aws_iot_task, "aws_iot_task", 9216, NULL, 5, NULL, 1);
}
