/******************************************************************************
 ** File Name:      paho_mqtt_example.h                                       *
 ** Author:         chenwenxiu                                                *
 ** DATE:           2022-4-07                                                 *
 ** Copyright:      Zhong Yun Xin an(Shenzhen) science and Technology Co.,Ltd.*
 **                 All Rights Reserved.                                      *
 ** Description:    This file contains paho MQTT example                      *
 ******************************************************************************
 *******************************************************************************
 **                        Edit History                                        *
 ** ---------------------------------------------------------------------------*
 ** DATE           NAME                   Description:
 **----------------------------------------------------------------------------*
 ** 2022-4-07     chenwenxiu               Create.
 ******************************************************************************/
/*******************************************************************************
 ** Header Files
 ******************************************************************************/
#include <stdio.h>
#include "string.h"
#include "osi_api.h"
#include "at_api.h"
#include "mqttclient.h"

/*******************************************************************************
 ** MACROS
 ******************************************************************************/
/*Configure according to your own MQTT server*/
#define DEMO_MQTT_HOST			"post-cn-tl32n58nj0d.mqtt.aliyuncs.com"
#define DEMO_MQTT_PORT			"1883"

#define DEMO_MQTT_PRODUCT_KEY 	"GID_YZ05@@@001"
#define DEMO_USER_NAME   		"Signature|LTAI5tRc8gLHpyP3MHpvnsWb|post-cn-tl32n58nj0d"
#define DEMO_USER_PWD 			"KHvMOc4X+2i5BIesJeXIMB3aWB8="

#define DEMO_SUB_TOPI			"YZ05_Test/demo/001/"
#define DEMO_PUB_DATA			"hello world!!"


static int vatResName(const char *rsp, const char *name)
{
    if(rsp == NULL || name == NULL){
        return -1;
    }

    // int ret = strncmp(rsp, name, strlen(name));
    if(strncmp(rsp, name, strlen(name)) == 0)
    {
        return strlen(name);
    }
    return -1;
}

static bool prvUpdateCREG2(void)
{
    char resp[128] = {0};
    char *cmd = "AT+CREG?\r\n";
    int n;
    int stat;
    char lac[5] = {0};
    char ci[9] = {0};
    int act;
	int ret;

	memset(resp, 0, sizeof(resp));
    ret = atCmdSendWaitResp(cmd, 200, "+CREG", 1, "ERROR", resp, sizeof(resp));
	RTI_LOG("resp: %s", resp);
    if(ret == 0)
	{
        int pos = vatResName(resp, "+CREG");
        if(pos >= 0)
        {
            ret = sscanf(resp + pos, ": %d,%d,\"%4s\",\"%8s\",%d\r\n", &n, &stat, lac, ci, &act);
            if(ret == 5)
            {
                RTI_LOG("n:%d state:%d lac:%s,ci:%s",n,stat,lac,ci);
                if((stat == 1) || (stat == 5) || (stat == 6))
				{
                    atCmdSendWaitResp("AT+CGACT=1\r\n", 1000, NULL, 1, "ERROR", NULL, 0);
                    return true;
                }
				else
				{
                    return false;
                }
            }
			else
			{
				return false;
			}
        }

        return false;
    }
	else
	{
        RTI_LOG("AT CREG Error");
    }
    return false;
}

static void activeNetwork(void)
{
	bool ret = true;
    atCmdSendWaitResp("AT+CREG=2\r\n", 1000, NULL, 1, NULL, NULL, 0);
    osiThreadSleep(10);
    atCmdSendWaitResp("AT*DIALMODE=0\r\n", 1000, NULL, 1, NULL, NULL, 0);
    osiThreadSleep(10);

    while (1)
    {
    	ret = prvUpdateCREG2();
		if(true == ret)
		{
			 break;
		}
		else
		{
            osiThreadSleep(200*1);
		}
    }
}

static void topic_handler(void* client, message_data_t* msg)
{
	char *rsp = NULL;

	rsp = (char *)osiMalloc(msg->message->payloadlen+100);
	if(NULL == rsp)
	{
		RTI_LOG("malloc no memory!");
	}

	memset(rsp, 0, msg->message->payloadlen+100);
	sprintf(rsp, "QMTRECV: %d,\"%s\",%d,\"%s\"\r\n", msg->message->id,
			msg->topic_name, msg->message->payloadlen, (char*)msg->message->payload);
	RTI_LOG("%s", rsp);

	if(NULL != rsp)
	{
		osiFree(rsp);
	}
}

static mqtt_error_t example_mqtt_publish(mqtt_client_t *client)
{
	mqtt_error_t ret = KAWAII_MQTT_SUCCESS_ERROR;
    mqtt_message_t msg;

	memset(&msg, 0, sizeof(msg));
	msg.qos = QOS2;
	msg.id = 1;
	msg.retained = 1;
	msg.payload = (void *)DEMO_PUB_DATA;
	msg.payloadlen = strlen(DEMO_PUB_DATA);

	ret = mqtt_publish(client, DEMO_SUB_TOPI, &msg);
	if(KAWAII_MQTT_SUCCESS_ERROR != ret)
	{
		RTI_LOG("paho mqtt unsubscribe error 0x%X", ret);
	}
	else
	{
		RTI_LOG("publish success");
	}

	return ret;
}

static void prvPahoMqttExampleThread(void *param)
{
	mqtt_client_t *client = NULL;
	mqtt_error_t ret = KAWAII_MQTT_SUCCESS_ERROR;

	/*Network ready*/
    activeNetwork();

	/*paho mqtt client create*/
	client = mqtt_lease();
	if(NULL == client)
	{
		RTI_LOG("MQTT: create client feil!");
		goto fail;
	}

	/*set mqtt param*/
	mqtt_set_host(client, DEMO_MQTT_HOST);
	mqtt_set_port(client, DEMO_MQTT_PORT);
	mqtt_set_client_id(client, DEMO_MQTT_PRODUCT_KEY);
	mqtt_set_user_name(client, DEMO_USER_NAME);
	mqtt_set_password(client, DEMO_USER_PWD);
	mqtt_set_keep_alive_interval(client, 60);
	mqtt_set_version(client, 4);

	/*mqtt connect*/
	ret = mqtt_open_with_results(client); // open socket
	if(KAWAII_MQTT_SUCCESS_ERROR != ret)
	{
		RTI_LOG("socket open fail!");
		goto fail;
	}
	ret = mqtt_conn_with_results(client); // connect mqtt server
	if(KAWAII_MQTT_SUCCESS_ERROR != ret)
	{
		RTI_LOG("connect server fail!");
		goto fail;
	}

	/*mqtt subscribe topic*/
	ret = mqtt_subscribe(client, DEMO_SUB_TOPI, QOS1, topic_handler);
	if(KAWAII_MQTT_SUCCESS_ERROR != ret)
	{
		RTI_LOG("mqtt subscribe fail!");
		goto fail;
	}
	else
	{
		//wait SUBACK
		osiThreadSleep(200*6);
	}

	/*mqtt publish topic*/
	mqtt_list_subscribe_topic(client);
	example_mqtt_publish(client); //will receive the publish information
	osiThreadSleep(200*3);
	example_mqtt_publish(client); //will receive the publish information
	osiThreadSleep(200*3);

	/*mqtt unsubscribe topic*/
	mqtt_unsubscribe(client, DEMO_SUB_TOPI);
	osiThreadSleep(200*3); //wait UNSUBACK

	example_mqtt_publish(client); //can't receive the publish information
	osiThreadSleep(200*3);

	/*mqtt disconnect*/
	mqtt_disconnect(client);
	osiThreadSleep(200*3);

	/*mqtt release*/
	mqtt_release(client);

fail:
	/*exit thread*/
    osiThreadExit();
}

int appimg_enter(void *param)
{
    RTI_LOG("application image enter, param 0x%x", param);
    osiThreadCreate("tcp_example", prvPahoMqttExampleThread, NULL, 210, 10*1024);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}
