#include "ARMCM0.h"
#include "gpio.h"
#include "debug.h"
#include "delay.h"
#include "ota.h"
#include "config.h"
#include "ble_slave.h"
#include "SYD_ble_service_Profile.h"
#include <string.h>
#include "timer.h"
#include "pad_mux_ctrl.h"
#include "Debuglog.h"
#include "ancs.h"


struct gap_evt_callback GAP_Event;
static struct gap_att_report_handle *g_report;

//notify ��־ 1�����Ѿ�ʹ�� 0����δʹ��
static uint8_t start_tx=0;
uint8_t battery_tx=0;
uint8_t wechat_tx=0;

//connection
uint8_t  connect_flag=0;

//device type
uint8_t per_device_system = 0;//1:IOS  0:not judge  2:android

//ancs uid
uint8_t phone_flag = 0;
uint8_t phone_uid[4] = {0};

uint8_t ADV_DATA[] = {
						0x03,// length
						0x19,
						0x00,
						0x00,
						0x02,// length
						0x01,// AD Type: Flags
						0x05,// LE Limited Discoverable Mode & BR/EDR Not Supported
						0x03,// length
						0x03,// AD Type: Complete list of 16-bit UUIDs 
						0x01,// UUID: Human Interface Device (0x0001)//12
						0x00,// UUID: Human Interface Device (0x0001)//18
						0X09,// length
						0XFF,// AD Type: MANUFACTURER SPECIFIC DATA
						0X00,// Company Identifier (0x00)
						0X00,// Company Identifier (0x00)
						0X00,
						0X00,
						0X00,
						0X00,
						0X00,
						0X00,
						0x06,// length
						0x09,// AD Type: Complete local name
						'U',
						'A',
						'R',
						'T',
						'F',
};

uint16_t ADV_DATA_SZ = sizeof(ADV_DATA); 
uint8_t SCAN_DATA[]={0x00};
uint16_t SCAN_DATA_SZ = 0; 

static void ble_init(void);
uint8_t BLE_SendData(uint8_t *buf, uint8_t len);
void message_notification(void);


static void setup_adv_data()
{
	struct gap_adv_params adv_params;	
	static struct gap_ble_addr dev_addr;
	
	adv_params.type = ADV_IND;
	adv_params.channel = 0x07;    // advertising channel : 37 & 38 & 39
	adv_params.interval = 0x640;  // advertising interval : unit 0.625ms)
	adv_params.timeout = 0x3FFF;    // timeout : uint seconds
	adv_params.hop_interval = 0x03;  //0x1c 
	adv_params.policy = 0x00;   

	gap_s_adv_parameters_set(&adv_params);

	/*get bluetooth address */
	gap_s_ble_address_get(&dev_addr);
	ADV_DATA[15] = dev_addr.addr[0];
	ADV_DATA[16] = dev_addr.addr[1];
	ADV_DATA[17] = dev_addr.addr[2];
	ADV_DATA[18] = dev_addr.addr[3];
	ADV_DATA[19] = dev_addr.addr[4];
	ADV_DATA[20] = dev_addr.addr[5];
	
//	DBGPRINTF(("addr type %02x \r\n",dev_addr.type));
//	DBGHEXDUMP("addr", dev_addr.addr, 6);

	gap_s_adv_data_set(ADV_DATA, ADV_DATA_SZ, SCAN_DATA, SCAN_DATA_SZ); 
}

/*
uint8_t target
0:fast
1:slow
*/
void BLSetConnectionUpdate(uint8_t target){
	struct gap_link_params  link_app;
	struct gap_smart_update_params smart_params;
	uint8_t buffer_cha1[5]={0XFC,0X01,0X00,0X00,0X00};
	gap_s_link_parameters_get(&link_app);
	#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
	dbg_printf("interval:%x latency:%x\r\n",link_app.interval,link_app.latency);
	#endif
	switch(target){
		case 0: 
				if((link_app.latency !=0) && (link_app.interval >0x10)){
					/* connection parameters */
					smart_params.updateitv_target=0x0010;  //target connection interval (60 * 1.25ms = 75 ms)
					smart_params.updatesvto=0x00c8;  //supervisory timeout (400 * 10 ms = 4s)
					smart_params.updatelatency=0x0000;
					smart_params.updatectrl=SMART_CONTROL_LATENCY | SMART_CONTROL_UPDATE;
					smart_params.updateadj_num=MAX_UPDATE_ADJ_NUM;
					gap_s_smart_update_latency(&smart_params);
				}
			  #if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
				DBGPRINTF(("SetUpdate ota link\r\n"));
			  #endif
				BLE_SendData(buffer_cha1,5);
		break;
		case 1:
				if((link_app.latency <0x000A) && (link_app.interval <0x0050)){
					/* connection parameters */
					smart_params.updateitv_target=0x0050;
					smart_params.updatelatency=0x000A;
					smart_params.updatesvto=0x0258;
					smart_params.updatectrl=SMART_CONTROL_LATENCY | SMART_CONTROL_UPDATE;
					smart_params.updateadj_num=MAX_UPDATE_ADJ_NUM;
					gap_s_smart_update_latency(&smart_params);	   
			#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
					DBGPRINTF(("SetUpdate ios link\r\n"));
			#endif
				}
		break;
	}
}

static void ble_gatt_read(struct gap_att_read_evt evt)
{
	if(evt.uuid == BLE_DEVICE_NAME_UUID)
	{
		uint8_t gatt_buf[]={'U', 'A', 'R', 'T','F'};
		uint8_t gatt_buf_sz = sizeof(gatt_buf); 

		gap_s_gatt_read_rsp_set(gatt_buf_sz, gatt_buf);
	}
	else if(evt.uuid == BLE_APPEARANCE_UUID)
	{
		uint8_t gatt_buf[]={0xff, 0xff};
		uint8_t gatt_buf_sz = sizeof(gatt_buf); 

		gap_s_gatt_read_rsp_set(gatt_buf_sz, gatt_buf);
	}
	else if(evt.uuid == BLE_MANUFACTURER_NAME_STRING_UUID)
	{
		uint8_t gatt_buf[]={'S','Y','D',' ','I', 'n', 'c', '.'};
		uint8_t gatt_buf_sz = sizeof(gatt_buf); 

		gap_s_gatt_read_rsp_set(gatt_buf_sz, gatt_buf);
	}
	else if(evt.uuid == BLE_MODEL_NUMBER_STRING_UUID)
	{
		uint8_t gatt_buf[]={'B', 'L', 'E', ' ', '1', '.', '0'};
		uint8_t gatt_buf_sz = sizeof(gatt_buf); 

		gap_s_gatt_read_rsp_set(gatt_buf_sz, gatt_buf);
	}
	else if(evt.uuid == BLE_SERIAL_NUMBER_STRING_UUID)
	{
		uint8_t gatt_buf[]={'1','.','0','.','0'};
		uint8_t gatt_buf_sz = sizeof(gatt_buf); 

		gap_s_gatt_read_rsp_set(gatt_buf_sz, gatt_buf);
	}
	else if(evt.uuid == BLE_HARDWARE_REVISION_STRING_UUID)
	{
		uint8_t gatt_buf[]={'2','.','0','0'};
		uint8_t gatt_buf_sz = sizeof(gatt_buf); 

		gap_s_gatt_read_rsp_set(gatt_buf_sz, gatt_buf);
	}
	else if(evt.uuid == BLE_FIRMWARE_REVISION_STRING_UUID)
	{
		uint8_t gatt_buf[]={'3','.','0','0'};
		uint8_t gatt_buf_sz = sizeof(gatt_buf); 

		gap_s_gatt_read_rsp_set(gatt_buf_sz, gatt_buf);
	}
	else if(evt.uuid == BLE_SOFTWARE_REVISION_STRING_UUID)
	{
		uint8_t gatt_buf[]={'4','.','0','0'};
		uint8_t gatt_buf_sz = sizeof(gatt_buf); 

		gap_s_gatt_read_rsp_set(gatt_buf_sz, gatt_buf);
	}
	else if(evt.uuid == BLE_PNP_ID_UUID)
	{
		uint8_t gatt_buf[]={ 0x02, 						//		Vendor ID Source
						     0x3a,0x09,					//		USB Vendor ID
						    0x05,0x0a,					//		Product ID
						    0x02,0x00	};				//		Product Version
												 
		uint8_t gatt_buf_sz = sizeof(gatt_buf); 

		gap_s_gatt_read_rsp_set(gatt_buf_sz, gatt_buf);
	}
	else if(evt.uuid == BLE_WECHAT_Read_UUID)
	{
		uint8_t buf[]={0x00,0x01,0x03,0x04};
		
		BLE_SendData(buf, 4);
	}
	#ifdef _OTA_
	else if(evt.uuid == BLE_OTA_Read_Write_UUID)
	{
		uint8_t sz=0;

		uint8_t rsp[sizeof(struct hci_evt)]={0};

		ota_rsp(rsp, &sz);
		
		gap_s_gatt_read_rsp_set(sz, rsp);
	}
	#endif
	else if(evt.uuid == BLE_Battry_Level_UUID)
	{
		//gap_s_gatt_read_rsp_set(0x01, 0X64);
	}
}

//���պ���
static void ble_gatt_write(struct gap_att_write_evt evt)
{
	// rx data
	//evt.data����ȡ��������buf
	if(evt.uuid== BLE_UART_Write_UUID)
	{
		if(evt.data[0] == 0xa1)
		{	
			gap_s_security_req(1, 0);
		}
	}
	#ifdef _OTA_
	else if(evt.uuid== BLE_OTA_Read_Write_UUID)
	{
		ota_cmd(evt.data, evt.sz);
	}
	#endif
}
//���ͺ���
uint8_t BLE_SendData(uint8_t *buf, uint8_t len)
{
	struct gap_att_report report;
	
	if(start_tx == 1)
	{
		report.primary = BLE_UART;
		report.uuid = BLE_UART_NOTIFY_UUID;
		report.hdl = BLE_UART_NOTIFY_VALUE_HANDLE;					
		report.value = BLE_GATT_NOTIFICATION;
		return gap_s_gatt_data_send(BLE_GATT_NOTIFICATION, &report, len, buf);
	}
	return 0;
}

uint8_t Battery_SendData(uint8_t *buf, uint8_t len)
{
	struct gap_att_report report;
	
	if(battery_tx == 1)
	{
		report.primary = BLE_BATTERY_SERVICE;
		report.uuid = BLE_Battry_Level_UUID;
		report.hdl = BLE_Battry_Level_VALUE_HANDLE;					
		report.value = BLE_GATT_NOTIFICATION;
		return gap_s_gatt_data_send(BLE_GATT_NOTIFICATION, &report, len, buf);
	}
	return 0;
}


void ble_evt_callback(struct gap_ble_evt *p_evt)
{
	if(p_evt->evt_code == GAP_EVT_ADV_END)
	{		
		gap_s_adv_start();
		#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
		DBGPRINTF(("GAP_EVT_ADV_END\r\n"));
		#endif
	}
	else if(p_evt->evt_code == GAP_EVT_CONNECTED)	 //�����¼�
	{
		start_tx = 0;
		connect_flag=1;								 //����״̬
		ota_state =0;
		per_device_system = 0;
		
		#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_) 
		DBGHEXDUMP("GAP_EVT_CONNECTED addr:",p_evt->evt.bond_dev_evt.addr,sizeof(p_evt->evt.bond_dev_evt.addr));
		#endif
		
		//BLSetConnectionUpdate(1);
	}
	else if(p_evt->evt_code == GAP_EVT_DISCONNECTED) //�����¼�
	{	
		connect_flag=0;								 //����״̬
		ota_state =0;
		start_tx = 0;
		per_device_system = 0;
		
		#if defined(CONFIG_UART_ENABLE)
			DBGPRINTF(("GAP_EVT_DISCONNECTED(%02x)\r\n",p_evt->evt.disconn_evt.reason));
		#elif defined(_SYD_RTT_DEBUG_)
			DBGPRINTF("GAP_EVT_DISCONNECTED(%02x)\r\n",p_evt->evt.disconn_evt.reason);
		#endif   
		gap_s_adv_start();
	}
	else if(p_evt->evt_code == GAP_EVT_ATT_HANDLE_CONFIGURE)
	{	
		#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_) 
			dbg_printf("config_evt.hdl: %02x  \r\n",p_evt->evt.att_handle_config_evt.hdl);
			dbg_printf("config_evt.value:%02x \r\n",p_evt->evt.att_handle_config_evt.value);
			dbg_printf("config_evt.uuid:%02x  \r\n",p_evt->evt.att_handle_config_evt.uuid);
		#endif
		
		if(p_evt->evt.att_handle_config_evt.uuid == BLE_UART)
		{
			if(p_evt->evt.att_handle_config_evt.hdl == (BLE_UART_NOTIFY_VALUE_HANDLE + 1))
			{
				if(p_evt->evt.att_handle_config_evt.value == BLE_GATT_NOTIFICATION)
				{
					#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_) 
					DBGPRINTF(("start_tx enable\r\n"));
					#endif
					start_tx = 1;
				}
				else
				{			
					start_tx = 0;
					#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_) 
					DBGPRINTF(("start_tx disable\r\n"));
					#endif
				}
			}
		}
		else if(p_evt->evt.att_handle_config_evt.uuid == BLE_WECHAT)
		{
			if(p_evt->evt.att_handle_config_evt.hdl == (BLE_WECHAT_Indication_VALUE_HANDLE + 1))
			{
				if(p_evt->evt.att_handle_config_evt.value == BLE_GATT_INDICATION)
				{
					#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_) 
					DBGPRINTF(("wechat_tx enable\r\n"));
					#endif
					wechat_tx = 1;
				}
				else
				{	
					#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_) 
					DBGPRINTF(("wechat_tx disable\r\n"));
					#endif
					wechat_tx = 0;
				}
			}
		}
		else if(p_evt->evt.att_handle_config_evt.uuid == BLE_BATTERY_SERVICE)
		{
			if(p_evt->evt.att_handle_config_evt.hdl == (BLE_Battry_Level_VALUE_HANDLE + 1))
			{
				if(p_evt->evt.att_handle_config_evt.value == BLE_GATT_NOTIFICATION)
				{
					#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_) 
					DBGPRINTF(("battery_tx enable\r\n"));
					#endif
					battery_tx = 1;
				}
				else
				{			
					battery_tx = 0;
					#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_) 
					DBGPRINTF(("battery_tx disable\r\n"));
					#endif
				}
			}
		}
	}
	else if(p_evt->evt_code == GAP_EVT_ATT_WRITE)
	{
		ble_gatt_write(p_evt->evt.att_write_evt);
		//DBGPRINTF(("GAP_EVT_ATT_WRITE uuid:(%02x)\r\n",p_evt->evt.att_write_evt.uuid));
	}
	else if(p_evt->evt_code == GAP_EVT_ATT_READ)
	{
		ble_gatt_read(p_evt->evt.att_read_evt);
    //DBGPRINTF(("GAP_EVT_ATT_READ uuid:(%02x)\r\n",p_evt->evt.att_write_evt.uuid));
	}
	else if(p_evt->evt_code == GAP_EVT_ATT_HANDLE_CONFIRMATION)
	{
		//DBGPRINTF(("GAP_EVT_ATT_HANDLE_CONFIRMATION uuid:(%02x)\r\n",p_evt->evt.att_handle_config_evt.uuid));
	}
	else if(p_evt->evt_code == GAP_EVT_ENC_KEY)
	{
		#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_) 
			DBGPRINTF(("GAP_EVT_ENC_KEY\r\n"));
		#endif
	}
	else if(p_evt->evt_code == GAP_EVT_ENC_START)
	{
		ancs_service_enable();
		#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_) 
			DBGPRINTF(("GAP_EVT_ENC_START\r\n"));
		#endif
	}
  else if(p_evt->evt_code == GAP_EVT_CONNECTION_UPDATE_RSP)
	{
		switch(p_evt->evt.connection_update_rsp_evt.result)
		{
			case CONN_PARAM_ACCEPTED:
				#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_) 
				DBGPRINTF(("update rsp ACCEPTED\r\n"));
				#endif
				break;
			case CONN_PARAM_REJECTED:
				#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
				DBGPRINTF(("update rsp REJECTED\r\n"));
				#endif
				break;
			case CONN_PARAM_SMART_TIMEROUT:
				#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
				DBGPRINTF(("update rsp TIMEROUT\r\n"));
				#endif
				break;
			case CONN_PARAM_SMART_SUCCEED:
				#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
				DBGPRINTF(("update rsp SUCCEED\r\n"));
				#endif
				break;
			case CONN_PARAM_LATENCY_ENABLE:
				#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
				DBGPRINTF(("Enable latency\r\n"));
				#endif
				break;
			case CONN_PARAM_LATENCY_DISABLE:
				#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
				DBGPRINTF(("Disable latency\r\n"));
				#endif
				break;
		}
	}
}


void ble_attc_callback(struct attc_ble_evt *p_attc)
{
	ancs_service_att_data(p_attc);
}



static void ble_init()
{	
	struct gap_wakeup_config pw_cfg;
	struct gap_profile_struct gatt;
	struct gap_pairing_req sec_params;
	struct gap_connection_param_rsp_pdu connection_params;

	gap_s_ble_init();
	
	//set profile
	gatt.report_handle_address = (uint32_t)_gatt_database_report_handle;
	gatt.primary_address	= (uint32_t)_gatt_database_primary;
	gatt.include_address	= (uint32_t)_gatt_database_include;
	gatt.characteristic_address	= (uint32_t)_gatt_database_characteristic;
	gatt.value_address = (uint32_t)_gatt_database_value;
	gap_s_gatt_profiles_set(&gatt);

	//set device bond configure
	sec_params.io = IO_NO_INPUT_OUTPUT;
	sec_params.oob = OOB_AUTH_NOT_PRESENT;
	sec_params.flags = AUTHREQ_BONDING;
	sec_params.mitm = 0;
	sec_params.max_enc_sz = 16;
	sec_params.init_key = 0;
	sec_params.rsp_key = (GAP_KEY_MASTER_IDEN |GAP_KEY_ADDR_INFO);
	gap_s_security_parameters_set(&sec_params);
 
	//set ble connect params
	connection_params.Interval_Min = 6;
	connection_params.Interval_Max = 9;
	connection_params.Latency = 100;
	connection_params.Timeout = 100;
	connection_params.PeferredPeriodicity = 6;
	connection_params.ReferenceConnEventCount = 50;
	connection_params.Offset[0] = 0;
	connection_params.Offset[1] = 1;
	connection_params.Offset[2] = 2;
	connection_params.Offset[3] = 3;
	connection_params.Offset[4] = 4;
	connection_params.Offset[5] = 5;
	gap_s_connection_param_set(&connection_params);

	//set connect event callback 
	GAP_Event.evt_mask=(GAP_EVT_CONNECTION_EVENT);
	GAP_Event.p_callback=&ble_evt_callback;
	gap_s_evt_handler_set(&GAP_Event);

	gap_s_att_c_evt_handler_set(&ble_attc_callback);
	
	gap_s_gatt_report_handle_get(&g_report);

	bm_s_bond_manager_idx_set(0);
	
	setup_adv_data();

	//set MCU wakup source
	pw_cfg.timer_wakeup_en = 1;
	pw_cfg.gpi_wakeup_en = 0;
	pw_cfg.gpi_wakeup_cfg = 0 ; 
	pw_cfg.gpi_wakeup_pol = 0; 
	pmu_wakeup_config(&pw_cfg);
}

/*
ancs_result  ����ancs_service_enable��Э��ջ����ancs��ʼ�����
result: 0: �Ƿ���Ӧ
        1: ancs end �Է��豸��IOS �����Ѿ�����
        2: not ancs �Է��豸����IOS
        3: not band �Է��豸��IOS ����û�а�
        4: not encryption �Է��豸��IOS ����û�м���
*/
void ancs_result(uint8_t result)
{
	switch(result)
	{
		case 1:
			per_device_system=1;
			#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
			DBGPRINTF(("ancs end\r\n"));
			#endif
			break;
		case 2:
			per_device_system=2;
			#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
			dbg_printf("not ancs\r\n");  
			#endif
			break;
		case 3:
			per_device_system=1;
			#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
			dbg_printf("not band\r\n");
			#endif
			break;
		case 4:
			per_device_system=1;
			#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
			dbg_printf("not encryption\r\n");   //����������¼���
			#endif
			break;
		default : 
			break;
	}
}


void gpio_init(void)
{
	uint8_t i;
   for(i=0;i<39;i++)
    {
        switch(i)
        {
            case GPIO_0:
                pad_mux_write(i, 0);
                gpi_config(i,PULL_DOWN);
                gpo_config(i,0);
            break;
			#ifdef CONFIG_UART_ENABLE
			case UART_RXD_0:
			case UART_TXD_0:
				pad_mux_write(i, 7);
			break;
			#endif
			case LED1_Pin:
			case LED2_Pin:
			case LED3_Pin:
			case LED4_Pin:
                pad_mux_write(i, 0);
                gpo_config(i,1);
            break;
						
			case GPIO_2:
			case GPIO_4:
			#ifdef _SWDDEBUG_DISENABLE_
			case GPIO_31:
			#endif
                pad_mux_write(i, 0);
                gpo_config(i,0);
            break;
						
			case GPIO_24:
			case GPIO_30:
			#ifndef _SWDDEBUG_DISENABLE_
			case GPIO_31:
			#endif
            break;
						
            default:
                pad_mux_write(i, 0);
                gpi_config(i,PULL_UP);
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

int main()
{	
	__disable_irq();
	
	ble_init();
	
	sys_mcu_clock_set(MCU_CLOCK_64_MHZ);
	// RC bumping
    sys_mcu_rc_calibration();
	
	#ifdef USER_32K_CLOCK_RCOSC
	sys_32k_clock_set(SYSTEM_32K_CLOCK_LPO);
	delay_ms(500);
	sys_32k_lpo_calibration();						//�����ڲ�RC32k�����У׼����	�����ú�����ʱ���ܹ��õ�һ���Ƚ�׼ȷ��ֵ
	#else
	sys_32k_clock_set(SYSTEM_32K_CLOCK_XO);
	#endif
	
	gpio_init();								 //gpio��ʼ��
	
	#ifdef _SYD_RTT_DEBUG_
		DebugLogInit();
		dbg_printf("SYD-TEK.Inc\r\n");
		dbg_printf("SYD RTT Init %s:%s\r\n",__DATE__ ,__TIME__);
	#elif defined(CONFIG_UART_ENABLE) 
		dbg_init();
		dbg_printf("SYD-TEK.Inc\r\n");
		dbg_printf("SYD8821 BLE ancs %s:%s\r\n",__DATE__ ,__TIME__);
	#endif
	
	__enable_irq();	
	
	gap_s_adv_start();
	
	#if defined(CONFIG_UART_ENABLE) || defined(_SYD_RTT_DEBUG_)
	DBGPRINTF(("gap_s_adv_start\r\n"));
	#endif

	while(1)
	{				
		ble_sched_execute();
        gpo_toggle(LED2_Pin);
		if(connect_flag)
		{

		}
		
		//�ܽӵ绰
		if(!gpi_get_val(KEY1_Pin))
		{
			if((connect_flag==1) && (phone_flag == 1))
			{
				phone_flag = 0;
				
				#if defined(CONFIG_UART_ENABLE)
				dbg_printf("reject the phone\r\n");
				#endif
				
				ancs_perform_notification_action(phone_uid, ACTION_ID_NEGATIVE);
			}
			while(!gpi_get_val(KEY1_Pin));
		}
		
		//�ӵ绰
		if(!gpi_get_val(KEY2_Pin))
		{
			if((connect_flag==1) && (phone_flag == 1))
			{
				phone_flag = 0;
				
				#if defined(CONFIG_UART_ENABLE)
				dbg_printf("answer the phone\r\n");
				#endif
				
				ancs_perform_notification_action(phone_uid, ACTION_ID_POSITIVE);
			}
			while(!gpi_get_val(KEY2_Pin));
		}
		
		//ancs handle
		message_notification();
		ota_user_handle();
		delay_ms(100);
		
		#ifdef _SWDDEBUG_DISENABLE_
		PMU_CTRL->UART_EN = 0;
		SystemSleep(POWER_SAVING_RC_OFF, FLASH_LDO_MODULE, 11000 , (PMU_WAKEUP_CONFIG_TYPE)(FSM_SLEEP_EN|TIMER_WAKE_EN|RTC_WAKE_EN));
		#endif
	}	
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
		
		//��Ϣ����
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
//									dbg_hexdump("data1:\r\n", (uint8_t *)name_offect,ancs_msg.msg_len);
				}
				else
				{
					ancs_msg.title_len -=3;
					memcpy(ancs_msg.title,ancs_msg.msg+3,ancs_msg.title_len);
					ancs_msg.msg_len -=name_offect-(char *)ancs_msg.msg+3;
					name_offect +=3;
				}
//								dbg_printf("title_len:%x msg_len:%x\r\n",ancs_msg.title_len,ancs_msg.msg_len);
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
		//ת����һ��Ҫ��� ancs_msg_t buffer����Ȼ���ܽ�������Ϣ
		clr_ancs_msg();
	}
}

