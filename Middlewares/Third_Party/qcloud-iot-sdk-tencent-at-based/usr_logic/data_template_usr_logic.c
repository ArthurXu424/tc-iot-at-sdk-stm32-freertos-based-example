/*
 * Tencent is pleased to support the open source community by making IoT Hub available.
 * Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.

 * Licensed under the MIT License (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT

 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include "qcloud_iot_api_export.h"
#include "lite-utils.h"
#include "at_client.h"
#include "string.h"
#include "data_config.c"


static bool sg_control_msg_arrived = false;
static bool sg_dev_report_new_data = false;


static char sg_data_report_buffer[AT_CMD_MAX_LEN];
static size_t sg_data_report_buffersize = sizeof(sg_data_report_buffer) / sizeof(sg_data_report_buffer[0]);

#ifdef EVENT_POST_ENABLED

#include "events_config.c"

#ifdef	EVENT_TIMESTAMP_USED
static void update_events_timestamp(sEvent *pEvents, int count)
{
	int i;
	
	for(i = 0; i < count; i++){
        if (NULL == (&pEvents[i])) { 
	        Log_e("null event pointer"); 
	        return; 
        }
		pEvents[i].timestamp = HAL_GetTimeSeconds();
	}
}
#endif 

static void event_post_cb(char *msg, void *context)
{
	Log_d("eventReply:%s", msg);
	clearEventFlag(context, FLAG_EVENT0|FLAG_EVENT1|FLAG_EVENT2);
}

#endif



/*如果有自定义的字符串或者json，需要在这里解析*/
static int update_self_define_value(const char *pJsonDoc, DeviceProperty *pProperty) 
{
    int rc = AT_ERR_SUCCESS;
		
	if((NULL == pJsonDoc)||(NULL == pProperty)){
		return AT_ERR_NULL;
	}
	
	/*convert const char* to char * */
	char *pTemJsonDoc =HAL_Malloc(strlen(pJsonDoc));
	strcpy(pTemJsonDoc, pJsonDoc);

	char* property_data = LITE_json_value_of(pProperty->key, pTemJsonDoc);	
	
    if(property_data != NULL){
		if(pProperty->type == TYPE_TEMPLATE_STRING){
			/*如果多个字符串属性,根据pProperty->key值匹配，处理字符串*/			
			Log_d("string type wait to be deal,%s", property_data);
		}else if(pProperty->type == TYPE_TEMPLATE_JOBJECT){
			Log_d("Json type wait to be deal,%s",property_data);	
		}
		
		HAL_Free(property_data);
    }else{
		
		rc = AT_ERR_FAILURE;
		Log_d("Property:%s no matched",pProperty->key);	
	}
	
	HAL_Free(pTemJsonDoc);
		
    return rc;
}

static void OnControlMsgCallback(void *pClient, const char *pJsonValueBuffer, uint32_t valueLength, DeviceProperty *pProperty) 
{
    int i = 0;

    for (i = 0; i < TOTAL_PROPERTY_COUNT; i++) {
		/*其他数据类型已经在_handle_delta流程统一处理了，字符串和json串需要在这里处理，因为只有产品自己才知道string/json的自定义解析*/
        if (strcmp(sg_DataTemplate[i].data_property.key, pProperty->key) == 0) {
            sg_DataTemplate[i].state = eCHANGED;
			if((sg_DataTemplate[i].data_property.type == TYPE_TEMPLATE_STRING)
				||(sg_DataTemplate[i].data_property.type == TYPE_TEMPLATE_JOBJECT)){

				update_self_define_value(pJsonValueBuffer, &(sg_DataTemplate[i].data_property));
			}
		
            Log_i("Property=%s changed", pProperty->key);
            sg_control_msg_arrived = true;
            return;
        }
    }

    Log_e("Property=%s changed no match", pProperty->key);
}

/**
 * 注册数据模板属性
 */
static int _register_data_template_property(void *ptemplate_client)
{
	int i,rc;
	
    for (i = 0; i < TOTAL_PROPERTY_COUNT; i++) {
	    rc = IOT_Template_Register_Property(ptemplate_client, &sg_DataTemplate[i].data_property, OnControlMsgCallback);
	    if (rc != AT_ERR_SUCCESS) {
	        rc = IOT_Template_Destroy(ptemplate_client);
	        Log_e("register device data template property failed, err: %d", rc);
	        return rc;
	    } else {
	        Log_i("data template property=%s registered.", sg_DataTemplate[i].data_property.key);
	    }
    }

	return AT_ERR_SUCCESS;
}

static void OnReportReplyCallback(void *pClient, Method method, ReplyAck replyAck, const char *pJsonDocument, void *pUserdata) {
	Log_i("recv report_reply(ack=%d): %s", replyAck,pJsonDocument);
}


/*用户需要实现的下行数据的业务逻辑,待用户实现*/
static void deal_down_stream_user_logic(void *pClient, ProductDataDefine   * pData)
{
	Log_d("someting about your own product logic wait to be done");
}

/*用户需要实现的上行数据的业务逻辑,此处仅供示例*/
static int deal_up_stream_user_logic(void *pClient, DeviceProperty *pReportDataList[], int *pCount)
{
	int i, j;
	
     for (i = 0, j = 0; i < TOTAL_PROPERTY_COUNT; i++) {       
        if(eCHANGED == sg_DataTemplate[i].state) {
            pReportDataList[j++] = &(sg_DataTemplate[i].data_property);
			sg_DataTemplate[i].state = eNOCHANGE;
        }
    }
	*pCount = j;

	return (*pCount > 0)?AT_ERR_SUCCESS:AT_ERR_FAILURE;
}

static eAtResault net_prepare(void)
{
	eAtResault Ret;
	osThreadId threadId;
	DeviceInfo sDevInfo;
	at_client_t pclient = at_client_get();	

	memset((char *)&sDevInfo, '\0', sizeof(DeviceInfo));
	Ret = (eAtResault)HAL_GetDevInfo(&sDevInfo);
	if(AT_ERR_SUCCESS != Ret){
		Log_e("Get device info err");
		return AT_ERR_FAILURE;
	}
	
	if(AT_ERR_SUCCESS != module_init(eMODULE_WIFI)) 
	{
		Log_e("module init failed");
		goto exit;
	}
	else
	{
		Log_d("module init success");	
	}
	
	//	Parser Func should run in a separate thread
	if((NULL != pclient)&&(NULL != pclient->parser))
	{
		hal_thread_create(&threadId, PARSE_THREAD_STACK_SIZE, osPriorityNormal, pclient->parser, pclient);
	}

	while(AT_STATUS_INITIALIZED != pclient->status)
	{	
		HAL_SleepMs(1000);
	}
	
	Log_d("Start shakehands with module...");
	Ret = module_handshake(CMD_TIMEOUT_MS);
	if(AT_ERR_SUCCESS != Ret)
	{
		Log_e("module connect fail,Ret:%d", Ret);
		goto exit;
	}
	else
	{
		Log_d("module connect success");
	}
	
	Ret = iot_device_info_init(sDevInfo.product_id, sDevInfo.device_name, sDevInfo.devSerc);
	if(AT_ERR_SUCCESS != Ret)
	{
		Log_e("dev info init fail,Ret:%d", Ret);
		goto exit;
	}

	Ret = module_info_set(iot_device_info_get(), ePSK_TLS);
	if(AT_ERR_SUCCESS != Ret)
	{
		Log_e("module info set fail,Ret:%d", Ret);
	}

exit:

	return Ret;
}

static void eventPostCheck(void *client)
{
#ifdef EVENT_POST_ENABLED	
	int rc;
	int i;
	uint32_t eflag;
	sEvent *pEventList[EVENT_COUNTS];
	uint8_t event_count;
		
	//事件上报
	setEventFlag(client, FLAG_EVENT0|FLAG_EVENT1|FLAG_EVENT2);
	eflag = getEventFlag(client);
	if((EVENT_COUNTS > 0 )&& (eflag > 0))
	{	
		event_count = 0;
		for(i = 0; i < EVENT_COUNTS; i++)
		{
		
			if((eflag&(1<<i))&ALL_EVENTS_MASK)
			{
				 pEventList[event_count++] = &(g_events[i]);				 
				 clearEventFlag(client, 1<<i);
#ifdef	EVENT_TIMESTAMP_USED				 
				 update_events_timestamp(&g_events[i], 1);
#endif
			}			
		}	

		rc = qcloud_iot_post_event(client, sg_data_report_buffer, sg_data_report_buffersize, \
											event_count, pEventList, event_post_cb);
		if(rc < 0)
		{
			Log_e("events post failed: %d", rc);
		}
	}
#endif

}

/*用户需要实现设备信息获取,此处示例实现*/
static int _get_sys_info(void *handle, char *pJsonDoc, size_t sizeOfBuffer)
{
	/*平台处理字段，必选字段至少有一个*/
    DeviceProperty plat_info[] = {
     	{.key = "module_hardinfo", .type = TYPE_TEMPLATE_STRING, .data = "ESP8266"},
     	{.key = "module_softinfo", .type = TYPE_TEMPLATE_STRING, .data = "V1.0"},
     	{.key = "fw_ver", 		   .type = TYPE_TEMPLATE_STRING, .data = "V3.0.1"},
     	{.key = "imei", 		   .type = TYPE_TEMPLATE_STRING, .data = "11-22-33-44"},
     	{.key = "lat", 			   .type = TYPE_TEMPLATE_STRING, .data = "22.546015"},
     	{.key = "lon", 			   .type = TYPE_TEMPLATE_STRING, .data = "113.941125"},
        {NULL, NULL, JINT32}  //结束
	};
		
	/*自定义附加信息*/
	DeviceProperty self_info[] = {
        {.key = "append_info", .type = TYPE_TEMPLATE_STRING, .data = "your self define ifno"},
        {NULL, NULL, JINT32}  //结束
	};

	return IOT_Template_JSON_ConstructSysInfo(handle, pJsonDoc, sizeOfBuffer, plat_info, self_info); 	
}


void data_template_demo_task(void *arg)
{
	eAtResault Ret;
	int rc;
	int ReportCont;
	void *client = NULL;
	at_client_t pclient = at_client_get();	
	DeviceProperty *pReportDataList[TOTAL_PROPERTY_COUNT];

	Log_d("shadow_demo_task Entry...");

	do  
	{
		Ret = net_prepare();
		if(AT_ERR_SUCCESS != Ret)
		{
			Log_e("net prepare fail,Ret:%d", Ret);
			break;
		}

		/*
		 *注意：module_register_network 联网需要根据所选模组适配修改实现
		*/
		Ret = module_register_network(eMODULE_ESP8266);
		if(AT_ERR_SUCCESS != Ret)
		{			
			Log_e("network connect fail,Ret:%d", Ret);
			break;
		}

		
		MQTTInitParams init_params = DEFAULT_MQTTINIT_PARAMS;
		Ret = module_mqtt_conn(init_params);
		if(AT_ERR_SUCCESS != Ret)
		{
			Log_e("module mqtt conn fail,Ret:%d", Ret);
			break;
		}
		else
		{
			Log_d("module mqtt conn success");
		}

		
		if(!IOT_MQTT_IsConnected())
		{
			Log_e("mqtt connect fail");
			break;
		}


		Ret = (eAtResault)IOT_Template_Construct(&client);
		if(AT_ERR_SUCCESS != Ret)
		{
			Log_e("data template construct fail,Ret:%d", Ret);
			break;
		}
		else
		{
			Log_d("data template construct success");
		}

		//init data template
		_init_data_template();

				
		//register data template propertys here
		rc = _register_data_template_property(client);
		if (rc == AT_ERR_SUCCESS) 
		{
			Log_i("Register data template propertys Success");
		} 
		else 
		{
			Log_e("Register data template propertys Failed: %d", rc);
			break;
		}
			
		//上报设备信息,平台根据这个信息提供产品层面的数据分析,譬如位置服务等
		rc = _get_sys_info(client, sg_data_report_buffer, sg_data_report_buffersize);
		if(AT_ERR_SUCCESS == rc)
		{
			rc = IOT_Template_Report_SysInfo_Sync(client, sg_data_report_buffer, sg_data_report_buffersize, QCLOUD_IOT_MQTT_COMMAND_TIMEOUT);	
			if (rc != AT_ERR_SUCCESS) 
			{
				Log_e("Report system info fail, err: %d", rc);
				break;
			}
		}
		else
		{
			Log_e("Get system info fail, err: %d", rc);
			break;
		}

		//获取离线期间数据
		rc = IOT_Template_GetStatus_sync(client, QCLOUD_IOT_MQTT_COMMAND_TIMEOUT);
		if (rc != AT_ERR_SUCCESS) 
		{
			Log_e("Get data status fail, err: %d", rc);
			break;
		}
		else
		{
			Log_d("Get data status success");
		}


		while(1)
		{
			HAL_SleepMs(1000);
			IOT_Template_Yield(client, 2000);
			
			/*服务端下行消息，业务处理逻辑1入口*/
			if (sg_control_msg_arrived) {	
				
				deal_down_stream_user_logic(client, &sg_ProductData);				
				//业务逻辑处理完后需要通知服务端control msg 已收到，请服务端删除control msg，否则服务端会保留control msg(通过Get status命令可以得到未删除的Control数据)
				sControlReplyPara replyPara;
				memset((char *)&replyPara, 0, sizeof(sControlReplyPara));
				replyPara.code = eDEAL_SUCCESS;
				replyPara.timeout_ms = QCLOUD_IOT_MQTT_COMMAND_TIMEOUT;						
				replyPara.status_msg[0] = '\0';			//可以通过 replyPara.status_msg 添加附加消息，一般在失败情况下才添加
				
				rc = IOT_Template_ControlReply(client, sg_data_report_buffer, sg_data_report_buffersize, &replyPara);
	            if (rc == AT_ERR_SUCCESS) {
					Log_d("Contol msg reply success");
					sg_control_msg_arrived = false;   
	            } else {
	                Log_e("Contol msg reply failed, err: %d", rc);
					break;
	            }

				sg_dev_report_new_data = true; //用户需要根据业务情况修改上报flag的赋值位置,此处仅为示例
			}	else{
				//Log_d("No delta msg received...");
			}

			/*设备上行消息,业务逻辑2入口*/
			if(sg_dev_report_new_data){
				
				/*上报属性的最新状态，应用侧可以通过report的状态确认control的命令是否执行成功*/
				if(AT_ERR_SUCCESS == deal_up_stream_user_logic(client, pReportDataList, &ReportCont)){
					
					rc = IOT_Template_JSON_ConstructReportArray(client, sg_data_report_buffer, sg_data_report_buffersize, ReportCont, pReportDataList);
					if (rc == AT_ERR_SUCCESS) {
						rc = IOT_Template_Report(client, sg_data_report_buffer, sg_data_report_buffersize, 
													OnReportReplyCallback, NULL, QCLOUD_IOT_MQTT_COMMAND_TIMEOUT);
						if (rc == AT_ERR_SUCCESS) {
							sg_dev_report_new_data = false;
							Log_i("data template reporte success");
						} else {
							Log_e("data template reporte failed, err: %d", rc);
							break;
						}
					} else {
						Log_e("construct reporte data failed, err: %d", rc);
						break;
					}
			
				}else{
					 Log_d("no data need to be reported or someting goes wrong");
				}
			}	else{			
				//Log_d("No device data need to be reported...");
			}

			
			eventPostCheck(client);
		}				
	}while (0);
	
	hal_thread_destroy(NULL);
	Log_e("Task teminated,Something goes wrong!!!");
}

void data_template_sample(void)
{
	osThreadId demo_threadId;
	
#ifdef OS_USED
	hal_thread_create(&demo_threadId, 512, osPriorityNormal, data_template_demo_task, NULL);
	hal_thread_destroy(NULL);
#else
	#error os should be used just now
#endif
}

