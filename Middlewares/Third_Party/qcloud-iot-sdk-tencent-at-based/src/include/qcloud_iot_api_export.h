/**
 ******************************************************************************
 * @file			:  at_api_export.h
 * @brief			:  all api export to application
 ******************************************************************************
 *
 * History:      <author>          <time>        <version>
 *               yougaliu          2019-3-20        1.0
 * Desc:          ORG.
 * Copyright (c) 2019 Tencent. 
 * All rights reserved.
 ******************************************************************************
 */
#ifndef _QCLOUD_API_EXPORT_H_
#define _QCLOUD_API_EXPORT_H_

/* IoT C-SDK version info */
#define QCLOUD_IOT_AT_SDK_VERSION       "1.0.0"
//#define MQTT_SHADOW_ENABLE
//#define EVENT_POST_ENABLED
#define MODULE_TYPE_WIFI


#define AT_CMD_MAX_LEN                 1024
#define RING_BUFF_LEN         		   AT_CMD_MAX_LEN	 //uart ring buffer len

#define MAX_PAYLOAD_LEN_PUB			   200			//AT+TCMQTTPUB �֧�ֵ����ݳ��ȣ��������������Ҫ����AT+TCMQTTPUBL




#define TRANSFER_LABEL_NEED			1		// ˫����/�����Ƿ���Ҫת�塣��ͬģ�鴦����ЩС���,Ĭ�ϲ���Ҫ����������˫���źͶ��Ŷ�����Ҫת��
#ifdef  TRANSFER_LABEL_NEED	 
#define T_	"\\" 							//��Ҫת�������£������Ƿ���Ҫת�塣Ŀǰֻ��ESP8266������Ҫת��
#else
#define T_	
#endif

#include "at_log.h"
#include "hal_export.h"
#include "dev_config.h"
#include "module_api_inf.h"
#include "at_sanity_check.h"
#include "qcloud_iot_export_mqtt.h"
#include "qcloud_iot_export_shadow.h"
#include "qcloud_iot_export_data_template.h"
#include "qcloud_iot_export_error.h"
#include "qcloud_iot_export_method.h"




#ifdef OS_USED
#include "cmsis_os.h"
#endif

#endif
