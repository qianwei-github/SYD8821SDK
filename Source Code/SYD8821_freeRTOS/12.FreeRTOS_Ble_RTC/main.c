/* BLE */
#include "ARMCM0.h"
#include "config.h"
#include "ble_slave.h"
#include "SYD_ble_service_Profile.h"
#include "ble.h"
#include "ota.h"
#include "ancs.h"

/* freeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"

/*drive*/
#include "delay.h"
#include "debug.h"
#include "gpio.h"
#include "rtc.h"
#include "pad_mux_ctrl.h"
#include "led_key.h"
#include <string.h>

#define LED_TASK_DELAY        200           /**< Task delay. Delays a LED0 task for 200 ms */
#define LED_TIMER_PERIOD      1000           /**< Task delay. Delays a LED0 task for 200 ms */

QueueHandle_t Ble_RX_Queue =NULL;
#define BLE_RX_QUEUE_LEN  10 /* 队列的长度，最大可包含多少个消息 */
#define BLE_RX_QUEUE_SIZE sizeof(struct queue_att_write_data) /* 队列中每个消息大小（字节） */

TimerHandle_t led_toggle_timer_handle;  /**< Reference to LED2 toggling FreeRTOS timer. */

xQueueHandle KeyMsgQueue;
#define KEY_QUEUE_LEN  10 /* 队列的长度，最大可包含多少个消息 */
#define KEY_QUEUE_SIZE 4  /* 队列中每个消息大小（字节） */

//ancs uid
uint8_t phone_flag = 0;
uint8_t phone_uid[4] = {0};

//master device type
uint8_t per_device_system = 0;//1:IOS  0:not judge  2:android


//adc dma buff
__align(4) uint32_t adc_dma_buff[4] = {0};
uint32_t adc_dma_maxcnt = sizeof(adc_dma_buff);


void message_notification(void);


void key1_irq_handle(void)
{
	BaseType_t xReturn = pdPASS;
	BaseType_t xHigherPriorityTaskWoken;
	uint32_t ulReturn;
	uint32_t send_data = 0;
	
	//xReturn = xQueueSendFromISR(Test_Queue,send_data1,NULL);
	ulReturn = taskENTER_CRITICAL_FROM_ISR();
	
	send_data = 1;
	xReturn = xQueueSendFromISR(KeyMsgQueue,&send_data,&xHigherPriorityTaskWoken);
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	
	if (pdPASS == xReturn) 
	{
		DBGPRINTF(("\r\nkey1 irq send queue success! data:%d\r\n",send_data));
	}
	else
	{	
		DBGPRINTF(("\r\nkey1 irq send queue error!\r\n"));		
	}
	taskEXIT_CRITICAL_FROM_ISR(ulReturn);	
}

void key2_irq_handle(void)
{
	BaseType_t xReturn = pdPASS;
	BaseType_t xHigherPriorityTaskWoken;
	uint32_t ulReturn;
	uint32_t send_data = 0;
	
	//xReturn = xQueueSendFromISR(Test_Queue,send_data1,NULL);
	ulReturn = taskENTER_CRITICAL_FROM_ISR();
	
	send_data = 2;
	
	xReturn = xQueueSendFromISR(KeyMsgQueue,&send_data,&xHigherPriorityTaskWoken);
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	
	if (pdPASS == xReturn) 
	{
		DBGPRINTF(("\r\nkey2 irq send queue success! data:%d\r\n",send_data));
	}
	else
	{	
		DBGPRINTF(("\r\nkey2 irq send queue error!\r\n"));		
	}
	taskEXIT_CRITICAL_FROM_ISR(ulReturn);	
}


void gpio_callback(GPIO_BANK_TYPE type, uint32_t bitmask)
{
	switch(type)
	{
		case GPIO_BANK_0: //GPIO_0 - GPIO_31
			switch (bitmask)
			{
				case U32BIT(KEY1):key1_irq_handle();
					break;
				case U32BIT(KEY2):key2_irq_handle();
					break;
				case U32BIT(KEY3):gpo_toggle(LED4);
					break;
				case U32BIT(KEY4):gpo_toggle(LED4);
					break;
				default:
					break;
			}
			break;
		case GPIO_BANK_1: // GPIO 32 ~ 38
			break;
		default:break;
	}
}


void board_init(void)
{
	
#ifdef CONFIG_UART_ENABLE
	//Uart init GPIO 20 21
	pad_mux_write(GPIO_20, 7);
	pad_mux_write(GPIO_21, 7);

	dbg_init();
	DBGPRINTF(("FreeRTOS BLE %s:%s ...\r\n",__DATE__ ,__TIME__));
#endif
	
	//led 1 2 3 4 --> GPIO25 23 34 10
	pad_mux_write(LED1,0);
	pad_mux_write(LED2,0);
	pad_mux_write(LED3,0);
	pad_mux_write(LED4,0);
	
	gpo_config(LED1,1);
	gpo_config(LED2,1);
	gpo_config(LED3,1);
	gpo_config(LED4,1);
	
	
	//KEY 1 2 3 4 -->GPIO14 15 16 17
	pad_mux_write(KEY1,0);
	pad_mux_write(KEY2,0);
	pad_mux_write(KEY3,0);
	pad_mux_write(KEY4,0);
	gpi_config(KEY1,PULL_UP);
	gpi_config(KEY2,PULL_UP);
	gpi_config(KEY3,PULL_UP);
	gpi_config(KEY4,PULL_UP);
	
	//config KEY 1 
	gpi_irq_set_cb(gpio_callback);
	gpi_enable_int(KEY1, EDGE_TRIGGER,POL_FALLING_LOW);
	gpi_enable_int(KEY2, EDGE_TRIGGER,POL_FALLING_LOW);
	
	pad_mux_write(GPIO_0,0);
	pad_mux_write(GPIO_1,0);
	pad_mux_write(GPIO_2,0);
	
}


void rtc_irq_cb(RTC_INT_TYPE type)
{
    RTC_TIME_TYPE time;
	
	if(type & RTC_INT_TICK)
	{
		time=rtc_get_calendar();
		dbg_printf("RTC_INT_TICK\r\n");
		dbg_printf("%d %d:%d:%d\r\n",time.decimal_format.day,time.decimal_format.hour,time.decimal_format.minute,time.decimal_format.second);
		gpo_toggle(LED1);
	}
	
	if(type & RTC_INT_CMP0)
	{
		dbg_printf("RTC_INT_CMP0\r\n");
	}
	
	if(type & RTC_INT_CMP1)
	{
		dbg_printf("RTC_INT_CMP1\r\n");
	}
}


void rtc_config(void)
{
	RTC_TIME_TYPE time;
	//RTC
	//init calendar
	rtc_stop();
	rtc_clear();
	rtc_int_clear(RTC_INT_ALL);
	rtc_int_disable(RTC_INT_ALL);
	
	time.decimal_format.day    = 7;
	time.decimal_format.hour   = 23;
	time.decimal_format.minute = 59;
	time.decimal_format.second = 0;
	
	rtc_set_calendar(&time);
	
	rtc_set_prescaler(32768, 0);  //1HZ,这个是固定的，只有这样配置，RTC时钟才能准确
	
	//rtc_set_seconds_bit(1);//这个就是配置1秒内tick的频率，1秒内产生2^order次tick中断,默认为tick频率为prescaler频率
	
	//rtc_int_enable((RTC_INT_TYPE)(RTC_INT_TICK | RTC_INT_CMP0));
	rtc_int_enable((RTC_INT_TYPE)(RTC_INT_TICK));
	rtc_set_interrupt_callback(rtc_irq_cb);
	
	if(rtc_status()==false)
	{
		DBG("rtc_start \r\n");
		rtc_start();
	}
	NVIC_EnableIRQ(RTC_IRQn);
}

/**@brief The function to call when the LED1 FreeRTOS timer expires.
 *
 * @param[in] pvParameter   Pointer that will be used as the parameter for the timer.
 */
static void led_toggle_timer_callback (void * pvParameter)
{
	gpo_toggle(LED2);
}

//BLE接收数据处理
void ble_stack_write_data_handle(void)
{
	struct queue_att_write_data r_data = {0};
	BaseType_t xReturn = pdTRUE;
	//xReturn = xQueueReceive( Test_Queue, &r_queue,portMAX_DELAY);//暂时不支持使用这种方式
	xReturn = xQueueReceive(Ble_RX_Queue,(void*)&r_data,0);
	
	if (pdTRUE == xReturn)
	{
		DBGPRINTF(("BLE Queue Recevie\r\n"));
		DBGPRINTF(("UUID:%04x\r\n",r_data.uuid));
		DBGHEXDUMP("data\r\n",r_data.data,r_data.sz);
		DBGPRINTF(("\r\n"));
		if(r_data.uuid == BLE_UART_Write_UUID)
		{
			if(r_data.data[0] == 0xa1)
			{	
				gap_s_security_req(1, 0);
			}
		}
	}
}

//OTA Handle
void ota_user_handle(void)
{
	if(ota_state == 1)
	{
		
	}
	else if(ota_state == 3)
	{
		ota_state = 0;
		delay_ms(500);
		pmu_mcu_reset();
		delay_ms(1000);
	}
}

//ble stack task
void ble_stack_thread( void *pvParameters )
{
	for( ;; )
	{
		ble_sched_execute();
		ble_stack_write_data_handle();
		message_notification();
		ota_user_handle();
		vTaskDelay(10);//10ms
	}	
}

//key irq queue  handle task
void Key_IRQ_Queue_Receive_Task( void *pvParameters )
{
	BaseType_t xReturn = pdTRUE;
	uint32_t r_data = 0;
	
	for( ;; )
	{
//		xReturn = xQueueReceive( KeyMsgQueue, &r_data,portMAX_DELAY);//暂时不能使用这种方式
		xReturn = xQueueReceive(KeyMsgQueue,(void*)&r_data,0);
		if (pdTRUE == xReturn)
		{
			DBGPRINTF(("key %d irq queue receive\r\n",r_data));
			if(r_data == 1)
			{
				if((connect_flag==1) && (phone_flag == 1))
				{
					phone_flag = 0;
					
					DBGPRINTF(("reject the phone\r\n"));
					
					ancs_perform_notification_action(phone_uid, ACTION_ID_NEGATIVE);
				}
			}
			
			else if(r_data == 2)
			{
				if((connect_flag==1) && (phone_flag == 1))
				{
					phone_flag = 0;
				
					DBGPRINTF(("answer the phone\r\n"));
					
					ancs_perform_notification_action(phone_uid, ACTION_ID_POSITIVE);
				}
			}
			
		}
		vTaskDelay(10);//10ms
	}	
}


void vStartThreadTasks( void )
{
	BaseType_t xReturn = pdPASS;/* Create Result*/
	
	DBGPRINTF(("Init task and queue\r\n"));
	
	/* Start timer for LED2 blinking and adc task*/
	led_toggle_timer_handle = xTimerCreate( "LED2", LED_TIMER_PERIOD, pdTRUE, NULL, led_toggle_timer_callback);
	xReturn = xTimerStart(led_toggle_timer_handle, 0);
	if(pdPASS == xReturn) DBGPRINTF(("Create LED2 Softtimer Task Success!\r\n"));
	
		
	xReturn = xTaskCreate( ble_stack_thread, "BLE", configMINIMAL_STACK_SIZE, NULL, 3, ( xTaskHandle * ) NULL );
	if(pdPASS == xReturn) DBGPRINTF(("Create ble_stack_thread Success!\r\n"));
	
	
	xReturn = xTaskCreate( Key_IRQ_Queue_Receive_Task, "KEY_R", configMINIMAL_STACK_SIZE, NULL, 2, ( xTaskHandle * ) NULL );
	if(pdPASS == xReturn) DBGPRINTF(("Create Key_IRQ_Queue_Receive_Task Success!\r\n"));
	
	Ble_RX_Queue = xQueueCreate((UBaseType_t)BLE_RX_QUEUE_LEN, (UBaseType_t)BLE_RX_QUEUE_SIZE);	
	if (NULL != Ble_RX_Queue) DBGPRINTF(("Create Ble_RX_Queue Success!\r\n"));
	
	KeyMsgQueue = xQueueCreate((UBaseType_t)KEY_QUEUE_LEN, (UBaseType_t)KEY_QUEUE_SIZE);	
	if (NULL != KeyMsgQueue) DBGPRINTF(("Create KeyMsgQueue Success!\r\n"));
	
	DBGPRINTF(("\r\nSYD8821 freeRTOS Start\r\n"));
	DBGPRINTF(("\r\n"));
}


/*-----------------------------------------------------------*/
int main( void )
{
	__disable_irq();
	
	//ble stack init
	ble_init();
	
	// RC bumping
	sys_mcu_rc_calibration();	
	// Select External XO
	sys_32k_clock_set(SYSTEM_32K_CLOCK_XO);
	// Set MCU Clock 64M
	sys_mcu_clock_set(MCU_CLOCK_64_MHZ);
	
	rtc_config();
	
	board_init();
	
	DBGPRINTF(("board hardware init end\r\n"));
	__enable_irq();	

	gap_s_adv_start();
	DBGPRINTF(("start ble adv\r\n\r\n"));
	
	/* Start user demo tasks. */
	vStartThreadTasks();

	/* Start the scheduler. */
	vTaskStartScheduler();
	/* Will only get here if there was not enough heap space to create the
	idle task. */
	return 0;
}

void message_notification(void)
{
	if(ancs_msg.valid == 1)
	{
		char * name_offect=(char *)ancs_msg.msg;
		
		#ifdef CONFIG_UART_ENABLE
			dbg_printf("appid_len:%d\r\n",ancs_msg.appid_len);
			dbg_printf("appid:%s\r\n",ancs_msg.appid);
			
			dbg_printf("title_len:%d\r\n",ancs_msg.title_len);
			dbg_printf("title:%s\r\n",ancs_msg.title);

			dbg_printf("msg len:%d  ",ancs_msg.msg_len);
			dbg_hexdump("msg: \r\n", ancs_msg.msg,ancs_msg.msg_len);
			dbg_hexdump("ancs_uid: \r\n", ancs_msg.ancs_uid,4);
		#endif
		
		//消息处理
		if (strstr((const char *)ancs_msg.appid,"mobilephone") != NULL)
		{
			phone_flag = 1;
			memcpy(phone_uid, ancs_msg.ancs_uid, 4);
		}
		
		else if (strstr((const char *)ancs_msg.appid,"MobileSMS") != NULL) 
		{
			
		}
		else if (strstr((const char *)ancs_msg.appid,"Tweetie") != NULL) 
		{
			name_offect=strstr((const char *)ancs_msg.msg,":");
	
			if(name_offect!= NULL)
			{
				ancs_msg.title_len=((name_offect-(char *)ancs_msg.msg)<ANCS_TITLE_LEN)?(name_offect-(char *)ancs_msg.msg):ANCS_TITLE_LEN;
				memcpy(ancs_msg.title,ancs_msg.msg,ancs_msg.title_len);			
				ancs_msg.msg_len -=name_offect-(char *)ancs_msg.msg+1;
				name_offect +=1;
			}
			else
			{
				name_offect=(char *)ancs_msg.msg;
			}	
		}
		else if (strstr((const char *)ancs_msg.appid,"skype") != NULL) 
		{
			
			if(ancs_msg.valid == 1) 
			{ 
				name_offect=strstr((const char *)ancs_msg.msg,":");
			
			}
			else
			{
				uint8_t temp[2]={0x0a,'\0'};
				name_offect=strstr((const char *)ancs_msg.msg,(const char *)temp);
			}
			
			if(name_offect!= NULL)
			{
				ancs_msg.title_len=((name_offect-(char *)ancs_msg.msg)<ANCS_TITLE_LEN)?(name_offect-(char *)ancs_msg.msg):ANCS_TITLE_LEN;
				memcpy(ancs_msg.title,ancs_msg.msg,ancs_msg.title_len);			
				ancs_msg.msg_len -=name_offect-(char *)ancs_msg.msg+1;
				name_offect +=1;
			}else
			{
			name_offect=(char *)ancs_msg.msg;
			
			}
		}
		else if (strstr((const char *)ancs_msg.appid,"Line") != NULL) 
		{
			name_offect=strstr((const char *)ancs_msg.msg,":");
			if(name_offect!= NULL)
			{
				ancs_msg.title_len=((name_offect-(char *)ancs_msg.msg)<ANCS_TITLE_LEN)?(name_offect-(char *)ancs_msg.msg):ANCS_TITLE_LEN;
				memcpy(ancs_msg.title,ancs_msg.msg,ancs_msg.title_len);			
				ancs_msg.msg_len -=name_offect-(char *)ancs_msg.msg+1;
				name_offect +=1;
			}
			else
			{					
				name_offect=(char *)ancs_msg.msg;
			}	
		}
		else if (strstr((const char *)ancs_msg.appid,"WhatsApp") != NULL) 
		{
			if(ancs_msg.valid == 1)
			{
				name_offect=strstr((const char *)ancs_msg.msg,":");
				if(name_offect== NULL)
				{
					uint8_t temp[4]={0xef,0xbc,0x9a,'\0'};
					name_offect=strstr((const char *)ancs_msg.msg,(const char *)temp);
				}
				else
				{ 
					name_offect=(char *)ancs_msg.msg;
				}
			}
			else
			{
				name_offect=strstr((const char *)ancs_msg.msg," ");
			}
			if(name_offect!= NULL)
			{
				ancs_msg.title_len=((name_offect-(char *)ancs_msg.msg)<ANCS_TITLE_LEN)?(name_offect-(char *)ancs_msg.msg):ANCS_TITLE_LEN;
				memcpy(ancs_msg.title,ancs_msg.msg,ancs_msg.title_len);			
				
				if(ancs_msg.valid == 2)
				{
					ancs_msg.msg_len -=name_offect-(char *)ancs_msg.msg+1;
					name_offect +=1;
//					dbg_hexdump("data1:\r\n", (uint8_t *)name_offect,ancs_msg.msg_len);
				}
				else
				{
					ancs_msg.title_len -=3;
					memcpy(ancs_msg.title,ancs_msg.msg+3,ancs_msg.title_len);
					ancs_msg.msg_len -=name_offect-(char *)ancs_msg.msg+3;
					name_offect +=3;
				}
//					bg_printf("title_len:%x msg_len:%x\r\n",ancs_msg.title_len,ancs_msg.msg_len);
			}
			else
			{ 
				name_offect=(char *)ancs_msg.msg;
			}
					
		}
		else if (strstr((const char *)ancs_msg.appid,"facebook") != NULL) 
		{
		
			name_offect=strstr((const char *)ancs_msg.msg," ");
			if(name_offect!= NULL)
			{
				ancs_msg.title_len=((name_offect-(char *)ancs_msg.msg)<ANCS_TITLE_LEN)?(name_offect-(char *)ancs_msg.msg):ANCS_TITLE_LEN;
				memcpy(ancs_msg.title,ancs_msg.msg,ancs_msg.title_len);			
				ancs_msg.msg_len -=name_offect-(char *)ancs_msg.msg+1;
				name_offect +=1;
			}
			else 
			{
				name_offect=(char *)ancs_msg.msg;
			}
					
				
		}
		else if (strstr((const char *)ancs_msg.appid,"xin") != NULL) 
		{
			name_offect=strstr((const char *)ancs_msg.msg,":");
			if(name_offect!= NULL){
				ancs_msg.title_len=((name_offect-(char *)ancs_msg.msg)<ANCS_TITLE_LEN)?(name_offect-(char *)ancs_msg.msg):ANCS_TITLE_LEN;
				memcpy(ancs_msg.title,ancs_msg.msg,ancs_msg.title_len);			
				ancs_msg.msg_len -=name_offect-(char *)ancs_msg.msg+1;
				name_offect +=1;
			}
			else
			{	
				name_offect=(char *)ancs_msg.msg;
			}
			
		}
		else if (strstr((const char *)ancs_msg.appid,"mqq") != NULL) 
		{
			name_offect=strstr((const char *)ancs_msg.msg,":");
			if(name_offect!= NULL){
				ancs_msg.title_len=((name_offect-(char *)ancs_msg.msg)<ANCS_TITLE_LEN)?(name_offect-(char *)ancs_msg.msg):ANCS_TITLE_LEN;
				memcpy(ancs_msg.title,ancs_msg.msg,ancs_msg.title_len);			
				ancs_msg.msg_len -=name_offect-(char *)ancs_msg.msg+1;
				name_offect +=1;
			}
			else 
			{
				name_offect=(char *)ancs_msg.msg;
			}
		}
		else{//other Message
			ancs_msg.valid =0;
			clr_ancs_msg();
		}
		//转接完一定要清楚 ancs_msg_t buffer，不然不能接收新消息
		clr_ancs_msg();
	}
}


