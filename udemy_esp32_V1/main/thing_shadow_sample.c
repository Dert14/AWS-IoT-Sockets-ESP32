/*
 * Copyright 2010-2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * Additions Copyright 2016 Espressif Systems (Shanghai) PTE LTD
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
/**
 * @file thing_shadow_sample.c
 * @brief A simple connected window example demonstrating the use of Thing Shadow
 *
 * See example README for more details.
 */
#include "aws_connection.h"


//event handler sluzacy do
//sprawdzania jaki to event glowny a potem konkretnie po ID

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
	//jesli to WIFI_EVENT oraz WIFI_EVENT_STA_START to esp laczymy z wifi
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    //je�li to IP_EVENT oraz IP_EVENT_STA_GOT_IP to laczymy sie z IP ??????????????????????????????????????????????????
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        /* Signal main application to continue execution */

    //uznajemy ze sie polaczylismy i ustawiamy bity
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        //jesli to WIFI_EVENT oraz WIFI_EVENT_STA_DISCONNECTED to probujemy ponownie sie polaczyc
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
        esp_wifi_connect();
     //uznajemy ze sie rozlaczylismy i resetujemy bity
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    }
}


void ShadowUpdateStatusCallback(const char *pThingName, ShadowActions_t action, Shadow_Ack_Status_t status,
                                const char *pReceivedJsonDocument, void *pContextData) {

	//raczej zeby nie wywalalo bledow przy braku uzycia
    IOT_UNUSED(pThingName);
    IOT_UNUSED(action);
    IOT_UNUSED(pReceivedJsonDocument);
    IOT_UNUSED(pContextData);

    shadowUpdateInProgress = false;

    //sprawdzamy czy update zostal zaakceptowany
    if(SHADOW_ACK_TIMEOUT == status) {
        ESP_LOGE(TAG, "Update timed out");
    } else if(SHADOW_ACK_REJECTED == status) {
        ESP_LOGE(TAG, "Update rejected");
    } else if(SHADOW_ACK_ACCEPTED == status) {
        ESP_LOGI(TAG, "Update accepted");
    }
}

//callback ktory zapisuje w logach jezli pContext != NULL
void socketActuate_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext) {
    IOT_UNUSED(pJsonString);
    IOT_UNUSED(JsonStringDataLen);

    if(pContext != NULL) {
    	//ESP_LOGI(TAG, "Delta changed");
        ESP_LOGI(TAG, "Delta - Socket state changed to %d", *(bool *) (pContext->pData));
    }
}

//Tworzymy glowny task komunikacji IoT
void aws_iot_task(void *param) {

	//definiujemy tablice pinow wyjsciowych, danych odebranych i wyslanych
	int output_pin[6] = {12, 13, 14, 25, 26, 27};
	bool socket_on_recived[6] = {0, 0, 0, 0, 0, 0};
	bool socket_on_send[6] = {1, 1, 1, 1, 1, 1};
	bool update_needed = false;

	//ustawaimy odpowiednie GPIO jako wyjscia
	for (int i = 0; i < 6; i++)
	{
		gpio_pad_select_gpio(output_pin[i]);
		gpio_set_direction(output_pin[i], GPIO_MODE_OUTPUT);
	}
	while (1)
	{

		//tworzymy tablice structow obsugujacych shadowy
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

		ESP_LOGI(TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

		// inicjalizujemy mqtt client
		AWS_IoT_Client mqttClient;

		ShadowInitParameters_t sp = ShadowInitParametersDefault;
		//przypisujemy (hosta?) i port
		sp.pHost = AWS_IOT_MQTT_HOST;
		sp.port = AWS_IOT_MQTT_PORT;

		//przypisujemy nasze binarne cety
		sp.pClientCRT = (const char *)certificate_pem_crt_start;
		sp.pClientKey = (const char *)private_pem_key_start;
		sp.pRootCA = (const char *)aws_root_ca_pem_start;
		//ponowne laczenie sie jest wylaczone
		sp.enableAutoReconnect = false;
		sp.disconnectHandler = NULL;

		//Czekamy az polaczymy sie z WiFi
		xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
							false, true, portMAX_DELAY);

		//inicjalizujemy shadowa i sprawdzamy czy z sukcesem, jezli nie to przerywamy, jezli tak to dalej lecimy
		ESP_LOGI(TAG, "Shadow Init");
		rc = aws_iot_shadow_init(&mqttClient, &sp);
		if(SUCCESS != rc) {
			ESP_LOGE(TAG, "aws_iot_shadow_init returned error %d, aborting...", rc);
			abort();
		}

		//uzywamy naszych credenciali do polczenia z shadowem
		ShadowConnectParameters_t scp = ShadowConnectParametersDefault;
		scp.pMyThingName = CONFIG_AWS_EXAMPLE_THING_NAME;
		scp.pMqttClientId = CONFIG_AWS_EXAMPLE_CLIENT_ID;
		scp.mqttClientIdLen = (uint16_t) strlen(CONFIG_AWS_EXAMPLE_CLIENT_ID);

		//laczymy sie z shadowem i sprawdzamy czy z sukcesem, jezli nie to przerywamy, jezli tak to dalej lecimy
		ESP_LOGI(TAG, "Shadow Connect");
		rc = aws_iot_shadow_connect(&mqttClient, &scp);
		if(SUCCESS != rc) {
			ESP_LOGE(TAG, "aws_iot_shadow_connect returned error %d, aborting...", rc);
			abort();
		}

		/*
		 * Enable Auto Reconnect functionality. Minimum and Maximum time of Exponential backoff are set in aws_iot_config.h
		 *  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
		 *  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
		 */

		rc = aws_iot_shadow_set_autoreconnect_status(&mqttClient, true);
		if(SUCCESS != rc) {
			ESP_LOGE(TAG, "Unable to set Auto Reconnect to true - %d, aborting...", rc);
			abort();
		}

		//W tym miejscu obslugujemy delte shadowa kolejno dla kazdego gniazda
		for (int i = 0; i < 6 ; i++)
		{
			rc = aws_iot_shadow_register_delta(&mqttClient, &socket_actuator[i]);
		}

		if(SUCCESS != rc) {
			ESP_LOGE(TAG, "Shadow Register Delta Error");
		}

		// publikujemy zmiany temperatury w loopie
		while(NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc) {
			rc = aws_iot_shadow_yield(&mqttClient, 200);
			//ustawiamy wyjscia na wartosci odebrane
			for (int i = 0; i < 6 ; i++)
				{
					//socket_on_recived_inverted = !socket_on_recived[i];
					gpio_set_level(output_pin[i], !socket_on_recived[i]);
				}
			if(NETWORK_ATTEMPTING_RECONNECT == rc || shadowUpdateInProgress) {
				rc = aws_iot_shadow_yield(&mqttClient, 1000);
				//Jezli klient probuje ponownie sie polaczyc lub juz czeka na shadow update
				//pomijamy reszte petli
				continue;
			}
			//wypisujemy temperature i stan okna
			ESP_LOGI(TAG, "=======================================================================================");
			ESP_LOGI(TAG, "On Device: socket state 1 %s", socket_on_recived[0] ? "true" : "false");
			ESP_LOGI(TAG, "On Device: socket state 2 %s", socket_on_recived[1] ? "true" : "false");
			ESP_LOGI(TAG, "On Device: socket state 3 %s", socket_on_recived[2] ? "true" : "false");
			ESP_LOGI(TAG, "On Device: socket state 4 %s", socket_on_recived[3] ? "true" : "false");
			ESP_LOGI(TAG, "On Device: socket state 5 %s", socket_on_recived[4] ? "true" : "false");
			ESP_LOGI(TAG, "On Device: socket state 6 %s", socket_on_recived[5] ? "true" : "false");

			//Tu sprawdzamy czy jakis update
			update_needed = false;
			for (int i = 0; i < 6; i++)
			{
				if(socket_on_send[i] != socket_on_recived[i])
				{
					update_needed = true;
				}
			}

			//Tworzymy w tym momencie Jsona ktorego wysylamy, raport stanu naszego urzadzenia (tylko reported)
			rc = aws_iot_shadow_init_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
			if(SUCCESS == rc) {
				//dodajemy reported
				rc = aws_iot_shadow_add_reported(JsonDocumentBuffer, sizeOfJsonDocumentBuffer, 6, &socket_actuator[0], &socket_actuator[1], &socket_actuator[2], &socket_actuator[3], &socket_actuator[4], &socket_actuator[5]);
				//jezli sie udalo to finalizujemy naszego Jsona
				if(SUCCESS == rc) {
					rc = aws_iot_finalize_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
					//jezli sie udalo to raportujemy i wysylamy naszego jsona
					if(SUCCESS == rc && update_needed) {
						ESP_LOGI(TAG, "Update Shadow: %s", JsonDocumentBuffer);
						rc = aws_iot_shadow_update(&mqttClient, CONFIG_AWS_EXAMPLE_THING_NAME, JsonDocumentBuffer,
												   ShadowUpdateStatusCallback, NULL, 4, true);
						//Przypisujemy wartoscia wyslanym wartosci odebrane
						for (int i = 0; i < 6; i++)
						{
							socket_on_send[i] = socket_on_recived[i];
						}
						shadowUpdateInProgress = true;
					}
				}
			}

			//ile zostalo stacka na taska?
			ESP_LOGI(TAG, "*****************************************************************************************");
			ESP_LOGI(TAG, "Stack remaining for task '%s' is %d bytes", pcTaskGetName(NULL), uxTaskGetStackHighWaterMark(NULL));

			//Wykonujemy taska co 1s
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}

		//Jezli cos poszlo nie tak to wychodzuimy z loopa i raporcik
		if(SUCCESS != rc) {
			ESP_LOGE(TAG, "An error occurred in the loop %d", rc);
		}

		//rozlaczmy sie z shadowem
		ESP_LOGI(TAG, "Disconnecting");
		rc = aws_iot_shadow_disconnect(&mqttClient);

		//jezli error przy rozlaczaniu
		if(SUCCESS != rc) {
			ESP_LOGE(TAG, "Disconnect error %d", rc);
		}

		vTaskDelete(NULL);
}
}

//Inicjalizacja WiFi
static void initialise_wifi(void)
{
    esp_netif_init();
    //sprawdzamy errory podczas gdy
    //tworzymy event_loop i tworzymy handlery dla WIFI_EVENT i IP_EVENT
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    /* Initialize Wi-Fi including netif with default config */
#ifdef ESP_NETIF_SUPPORTED
    esp_netif_create_default_wifi_sta();
#endif
    //dynamicznie alokujemy pamiec dzieki freeRtos?
    wifi_event_group = xEventGroupCreate();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );

    //Tutaj przypisujemy nasze SSID WiFi oraz haslo
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };

    //jezeli pasy sa ok to sie polaczymy
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

void app_main()
{
	//tworzymy zmienna na errory w pamieci nie ulotnej
    esp_err_t err = nvs_flash_init();

    //nie czaje raczej
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    initialise_wifi();
    /* Temporarily pin task to core, due to FPU uncertainty */
    xTaskCreatePinnedToCore(&aws_iot_task, "aws_iot_task", 9216, NULL, 5, NULL, 1);
}
