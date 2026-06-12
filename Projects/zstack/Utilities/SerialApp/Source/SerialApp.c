/*********************************************************************
 * INCLUDES
 */

#include <stdio.h>
#include <string.h>
#include "AF.h"
#include "OnBoard.h"
#include "OSAL_Tasks.h"
#include "SerialApp.h"
#include "ZDApp.h"
#include "ZDObject.h"
#include "ZDProfile.h"

#include "hal_drivers.h"
#include "hal_key.h"
#if defined ( LCD_SUPPORTED )
  #include "hal_lcd.h"
#endif
#include "hal_led.h"
#include "hal_uart.h"
#if defined ( ZDO_COORDINATOR )
  #include "esp8266.h"
#endif

#if !defined ( ZDO_COORDINATOR )
  #include "DHT11.h"
  #include "hal_adc.h"

  // Set device type: 1=TEM1(DHT11), 2=LIGHT2(Light Sensor)
  #define DEVICE_TYPE 1
#endif

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

#if !defined( SERIAL_APP_PORT )
#define SERIAL_APP_PORT  0
#endif

#if !defined( SERIAL_APP_BAUD )
  //#define SERIAL_APP_BAUD  HAL_UART_BR_38400
  #define SERIAL_APP_BAUD  HAL_UART_BR_9600
#endif

// When the Rx buf space is less than this threshold, invoke the Rx callback.
#if !defined( SERIAL_APP_THRESH )
#define SERIAL_APP_THRESH  64
#endif

#if !defined( SERIAL_APP_RX_SZ )
#define SERIAL_APP_RX_SZ  128
#endif

#if !defined( SERIAL_APP_TX_SZ )
#define SERIAL_APP_TX_SZ  128
#endif

// Millisecs of idle time after a byte is received before invoking Rx callback.
#if !defined( SERIAL_APP_IDLE )
#define SERIAL_APP_IDLE  6
#endif

// Loopback Rx bytes to Tx for throughput testing.
#if !defined( SERIAL_APP_LOOPBACK )
#define SERIAL_APP_LOOPBACK  FALSE
#endif

// This is the max byte count per OTA message.
#if !defined( SERIAL_APP_TX_MAX )
#define SERIAL_APP_TX_MAX  80
#endif

#define SERIAL_APP_RSP_CNT  4

// This list should be filled with Application specific Cluster IDs.
const cId_t SerialApp_ClusterList[SERIALAPP_MAX_CLUSTERS] =
{
  SERIALAPP_CLUSTERID1,
  SERIALAPP_CLUSTERID2,
  SERIALAPP_CONNECTREQ_CLUSTER,
  SERIALAPP_CONNECTRSP_CLUSTER,
  SERIALAPP_SENSOR_CLUSTER
};

const SimpleDescriptionFormat_t SerialApp_SimpleDesc =
{
  SERIALAPP_ENDPOINT,              //  int   Endpoint;
  SERIALAPP_PROFID,                //  uint16 AppProfId[2];
  SERIALAPP_DEVICEID,              //  uint16 AppDeviceId[2];
  SERIALAPP_DEVICE_VERSION,        //  int   AppDevVer:4;
  SERIALAPP_FLAGS,                 //  int   AppFlags:4;
  SERIALAPP_MAX_CLUSTERS,          //  byte  AppNumInClusters;
  (cId_t *)SerialApp_ClusterList,  //  byte *pAppInClusterList;
  SERIALAPP_MAX_CLUSTERS,          //  byte  AppNumOutClusters;
  (cId_t *)SerialApp_ClusterList   //  byte *pAppOutClusterList;
};

endPointDesc_t SerialApp_epDesc =
{
  SERIALAPP_ENDPOINT,
 &SerialApp_TaskID,
  (SimpleDescriptionFormat_t *)&SerialApp_SimpleDesc,
  noLatencyReqs
};

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */
devStates_t SampleApp_NwkState;   
uint8 SerialApp_TaskID;           // Task ID for internal task/event processing.

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

static uint8 SerialApp_MsgID;

static afAddrType_t SerialApp_TxAddr;
static uint8 SerialApp_TxSeq;
static uint8 SerialApp_TxBuf[SERIAL_APP_TX_MAX+1];
static uint8 SerialApp_TxLen;

static afAddrType_t SerialApp_RxAddr;
static uint8 SerialApp_RxSeq;
static uint8 SerialApp_RspBuf[SERIAL_APP_RSP_CNT];

// Child node tracking (coordinator only)
#define MAX_CHILD_NODES 4
static uint16 ChildAddrs[MAX_CHILD_NODES];
static uint8  ChildCount = 0;
static uint8  ChildLastSeen[MAX_CHILD_NODES];  // heartbeat: seconds since last data
#define CHILD_TIMEOUT  15  // remove child after 15s of no data

// OLED display mode: 0=Nodes, 1=TEM1, 2=LIGHT2
static uint8  dispMode = 0;

// Sensor data storage (received from end devices)
// TEM1: temperature(°C x10) + humidity(%RH x10)
// LIGHT2: light level (0-100%)
static int16  TEM1_Temp = 0;       // e.g. 256 = 25.6°C
static uint16 TEM1_Humi = 0;       // e.g. 560 = 56.0%RH
static uint8  LIGHT2_Val = 0;      // 0-100%

/*********************************************************************
 * LOCAL FUNCTIONS
 */

static void SerialApp_ProcessMSGCmd( afIncomingMSGPacket_t *pkt );
static void SerialApp_Send(void);
static void SerialApp_Resp(void);
static void SerialApp_CallBack(uint8 port, uint8 event); 
static void SerialApp_DeviceConnect(void);              
static void SerialApp_DeviceConnectRsp(uint8*);         
static void SerialApp_HandleKeys( uint8 shift, uint8 keys );
static void SerialApp_ConnectReqProcess(uint8*);
#if defined ( LCD_SUPPORTED )
static void UpdateOled(void);
#endif

/*********************************************************************
 * @fn      SerialApp_Init
 *
 * @brief   This is called during OSAL tasks' initialization.
 *
 * @param   task_id - the Task ID assigned by OSAL.
 *
 * @return  none
 */
void SerialApp_Init( uint8 task_id )
{
  halUARTCfg_t uartConfig;

  SerialApp_TaskID = task_id;
  SerialApp_RxSeq = 0xC3;
  SampleApp_NwkState = DEV_INIT;       
  
  afRegister( (endPointDesc_t *)&SerialApp_epDesc );

  RegisterForKeys( task_id );

  uartConfig.configured           = TRUE;              // 2x30 don't care - see uart driver.
  uartConfig.baudRate             = SERIAL_APP_BAUD;
  uartConfig.flowControl          = FALSE;
  uartConfig.flowControlThreshold = SERIAL_APP_THRESH; // 2x30 don't care - see uart driver.
  uartConfig.rx.maxBufSize        = SERIAL_APP_RX_SZ;  // 2x30 don't care - see uart driver.
  uartConfig.tx.maxBufSize        = SERIAL_APP_TX_SZ;  // 2x30 don't care - see uart driver.
  uartConfig.idleTimeout          = SERIAL_APP_IDLE;   // 2x30 don't care - see uart driver.
  uartConfig.intEnable            = TRUE;              // 2x30 don't care - see uart driver.
  uartConfig.callBackFunc         = SerialApp_CallBack;
  HalUARTOpen (SERIAL_APP_PORT, &uartConfig);

  HalUARTWrite(SERIAL_APP_PORT, "HELLO\r\n", 7);

#if defined ( LCD_SUPPORTED )
  #if ZDO_COORDINATOR
    UpdateOled();
  #else
    HalLcdWriteString( "EndDevice", HAL_LCD_LINE_1 );
    HalLcdWriteString( "Ready", HAL_LCD_LINE_2 );
  #endif
#endif
  
  ZDO_RegisterForZDOMsg( SerialApp_TaskID, End_Device_Bind_rsp );
  ZDO_RegisterForZDOMsg( SerialApp_TaskID, Match_Desc_rsp );
}

/*********************************************************************
 * @fn      SerialApp_ProcessEvent
 *
 * @brief   Generic Application Task event processor.
 *
 * @param   task_id  - The OSAL assigned task ID.
 * @param   events   - Bit map of events to process.
 *
 * @return  Event flags of all unprocessed events.
 */
UINT16 SerialApp_ProcessEvent( uint8 task_id, UINT16 events )
{
  (void)task_id;  // Intentionally unreferenced parameter
  
  if ( events & SYS_EVENT_MSG )
  {
    afIncomingMSGPacket_t *MSGpkt;

    while ( (MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive( SerialApp_TaskID )) )
    {
      switch ( MSGpkt->hdr.event )
      {
      case AF_INCOMING_MSG_CMD:
        SerialApp_ProcessMSGCmd( MSGpkt );
        break;
        
      case KEY_CHANGE:
        SerialApp_HandleKeys( ((keyChange_t *)MSGpkt)->state, ((keyChange_t *)MSGpkt)->keys );
        break;

      case ZDO_STATE_CHANGE:
        SampleApp_NwkState = (devStates_t)(MSGpkt->hdr.status);
        if ( (SampleApp_NwkState == DEV_ZB_COORD)
            || (SampleApp_NwkState == DEV_ROUTER)
            || (SampleApp_NwkState == DEV_END_DEVICE) )
        {
            HalLedSet(HAL_LED_1, HAL_LED_MODE_ON);

            if(SampleApp_NwkState == DEV_ZB_COORD) {
#if defined ( ZDO_COORDINATOR )
              // Start heartbeat timer (every 5s)
              osal_start_timerEx( SerialApp_TaskID, SERIALAPP_HEARTBEAT_EVT, 5000 );
              // Init ESP8266 and start WiFi tick (every 50ms)
              ESP8266_Init();
              osal_start_timerEx( SerialApp_TaskID, SERIALAPP_WIFI_EVT, 50 );
              #if defined ( LCD_SUPPORTED )
                UpdateOled();
              #endif
#endif
            } else {
              #if defined ( LCD_SUPPORTED )
                HalLcdWriteString( "Net:OK  EndDev", HAL_LCD_LINE_1 );
              #endif
              SerialApp_DeviceConnect();
            }
        }
        else
        {
          // Device is no longer in the network
        }
        break;

      default:
        break;
      }

      osal_msg_deallocate( (uint8 *)MSGpkt );
    }

    return ( events ^ SYS_EVENT_MSG );
  }

  if ( events & SERIALAPP_SEND_EVT )
  {
    SerialApp_Send();
    return ( events ^ SERIALAPP_SEND_EVT );
  }

  if ( events & SERIALAPP_RESP_EVT )
  {
    SerialApp_Resp();
    return ( events ^ SERIALAPP_RESP_EVT );
  }

  if ( events & SERIALAPP_SENSOR_EVT )
  {
#if !defined ( ZDO_COORDINATOR )
  #if DEVICE_TYPE == 1
    // TEM1: read DHT11 and send to coordinator
    {
      uint8 sensorBuf[5];
      char dbg[48];
      DHT11();
      sensorBuf[0] = 0x01;  // TEM1 identifier
      sensorBuf[1] = wendu_shi;
      sensorBuf[2] = wendu_ge;
      sensorBuf[3] = shidu_shi;
      sensorBuf[4] = shidu_ge;
      sprintf(dbg, "TEM1: %02X %02X %02X %02X %02X | T=%d.%d H=%d.%d\r\n",
              dht11_raw[0], dht11_raw[1], dht11_raw[2],
              dht11_raw[3], dht11_raw[4],
              wendu_shi, wendu_ge, shidu_shi, shidu_ge);
      HalUARTWrite( SERIAL_APP_PORT, (uint8*)dbg, strlen(dbg) );
      AF_DataRequest( &SerialApp_TxAddr,
                      (endPointDesc_t *)&SerialApp_epDesc,
                      SERIALAPP_SENSOR_CLUSTER,
                      5, sensorBuf, &SerialApp_MsgID, 0, AF_DEFAULT_RADIUS );
    }
  #elif DEVICE_TYPE == 2
    // LIGHT2: read light sensor ADC on P0_6
    {
      uint8 sensorBuf[3];
      char dbg[32];
      uint16 adcVal = HalAdcRead( HAL_ADC_CHANNEL_6, HAL_ADC_RESOLUTION_8 );
      uint8 rawLight = (uint8)(100 - (adcVal * 100 / 255));
      uint8 light = (rawLight <= 50) ? 0 :
                    (rawLight >= 100) ? 90 :
                    (uint8)((rawLight - 50) * 9 / 5);
      sensorBuf[0] = 0x02;  // LIGHT2 identifier
      sensorBuf[1] = light;
      sensorBuf[2] = 0;     // padding
      sprintf(dbg, "LIGHT2: ADC=%d Light=%d%%\r\n", adcVal, light);
      HalUARTWrite( SERIAL_APP_PORT, (uint8*)dbg, strlen(dbg) );
      AF_DataRequest( &SerialApp_TxAddr,
                      (endPointDesc_t *)&SerialApp_epDesc,
                      SERIALAPP_SENSOR_CLUSTER,
                      3, sensorBuf, &SerialApp_MsgID, 0, AF_DEFAULT_RADIUS );
    }
  #endif
    // Restart timer (1s interval)
    osal_start_timerEx( SerialApp_TaskID, SERIALAPP_SENSOR_EVT, 1000 );
#endif
    return ( events ^ SERIALAPP_SENSOR_EVT );
  }

#if defined ( ZDO_COORDINATOR )
  if ( events & SERIALAPP_HEARTBEAT_EVT )
  {
    uint8 i, j;
    // Increment last-seen counters, remove expired children
    for (i = 0; i < ChildCount; ) {
      ChildLastSeen[i]++;
      if (ChildLastSeen[i] > CHILD_TIMEOUT) {
        // Remove expired child
        for (j = i; j < ChildCount - 1; j++) {
          ChildAddrs[j] = ChildAddrs[j + 1];
          ChildLastSeen[j] = ChildLastSeen[j + 1];
        }
        ChildCount--;
        #if defined ( LCD_SUPPORTED )
          if (dispMode == 0) UpdateOled();
        #endif
      } else {
        i++;
      }
    }
    // Restart timer
    osal_start_timerEx( SerialApp_TaskID, SERIALAPP_HEARTBEAT_EVT, 5000 );
    return ( events ^ SERIALAPP_HEARTBEAT_EVT );
  }

  if ( events & SERIALAPP_WIFI_EVT )
  {
    static uint16 wifiTickCount = 0;
    static uint8  lastWifiState = 0xFF;  // force first update
    static uint8  lastWifiClient = 0xFF;
    // Call ESP8266 state machine every 50ms
    ESP8266_Tick();
    wifiTickCount++;

    // Update OLED when WiFi state changes
    if ((g_wifiState != lastWifiState) || (g_wifiClient != lastWifiClient)) {
      lastWifiState = g_wifiState;
      lastWifiClient = g_wifiClient;
      #if defined ( LCD_SUPPORTED )
        if (dispMode == 0) UpdateOled();
      #endif
    }

    // Trigger data upload every 1s (20 ticks * 50ms)
    if (wifiTickCount >= 20) {
      wifiTickCount = 0;
      if (ESP8266_IsConnected()) {
        ESP8266_SetSensorData( TEM1_Temp, TEM1_Humi, LIGHT2_Val );
        // State machine will handle the upload
      }
    }
    osal_start_timerEx( SerialApp_TaskID, SERIALAPP_WIFI_EVT, 50 );
    return ( events ^ SERIALAPP_WIFI_EVT );
  }
#endif

  return ( 0 );  // Discard unknown events.
}

/*********************************************************************
 * @fn      SerialApp_ProcessMSGCmd
 *
 * @brief   Data message processor callback. This function processes
 *          any incoming data - probably from other devices. Based
 *          on the cluster ID, perform the intended action.
 *
 * @param   pkt - pointer to the incoming message packet
 *
 * @return  TRUE if the 'pkt' parameter is being used and will be freed later,
 *          FALSE otherwise.
 */
void SerialApp_ProcessMSGCmd( afIncomingMSGPacket_t *pkt )
{
  uint8 stat;
  uint8 seqnb;
  uint8 delay;

  switch ( pkt->clusterId )
  {
  // A message with a serial data block to be transmitted on the serial port.
  case SERIALAPP_CLUSTERID1: //�յ����͹���������ͨ�����������������ʾ
    // Store the address for sending and retrying.
    osal_memcpy(&SerialApp_RxAddr, &(pkt->srcAddr), sizeof( afAddrType_t ));

    seqnb = pkt->cmd.Data[0];

    // Keep message if not a repeat packet
    if ( (seqnb > SerialApp_RxSeq) ||                    // Normal
        ((seqnb < 0x80 ) && ( SerialApp_RxSeq > 0x80)) ) // Wrap-around
    {
        // Transmit the data on the serial port. // ͨ�����ڷ������ݵ�PC��
        if ( HalUARTWrite( SERIAL_APP_PORT, pkt->cmd.Data+1, (pkt->cmd.DataLength-1) ) )
        {
          // Save for next incoming message
          SerialApp_RxSeq = seqnb;
          stat = OTA_SUCCESS;
        }
        else
        {
          stat = OTA_SER_BUSY;
        }
    }
    else
    {
        stat = OTA_DUP_MSG;
    }

    // Select approproiate OTA flow-control delay.
    delay = (stat == OTA_SER_BUSY) ? SERIALAPP_NAK_DELAY : SERIALAPP_ACK_DELAY;

    // Build & send OTA response message.
    SerialApp_RspBuf[0] = stat;
    SerialApp_RspBuf[1] = seqnb;
    SerialApp_RspBuf[2] = LO_UINT16( delay );
    SerialApp_RspBuf[3] = HI_UINT16( delay );
    osal_set_event( SerialApp_TaskID, SERIALAPP_RESP_EVT ); //�յ����ݺ󣬷���һ����Ӧ�¼�
    osal_stop_timerEx(SerialApp_TaskID, SERIALAPP_RESP_EVT);
    break;

  // A response to a received serial data block.   // �ӵ���Ӧ��Ϣ
  case SERIALAPP_CLUSTERID2:
    if ((pkt->cmd.Data[1] == SerialApp_TxSeq) &&
       ((pkt->cmd.Data[0] == OTA_SUCCESS) || (pkt->cmd.Data[0] == OTA_DUP_MSG)))
    {
      SerialApp_TxLen = 0;
      osal_stop_timerEx(SerialApp_TaskID, SERIALAPP_SEND_EVT);
    }
    else
    {
      // Re-start timeout according to delay sent from other device.
      delay = BUILD_UINT16( pkt->cmd.Data[2], pkt->cmd.Data[3] );
      osal_start_timerEx( SerialApp_TaskID, SERIALAPP_SEND_EVT, delay );
    }
    break;

    case SERIALAPP_SENSOR_CLUSTER:
#if defined ( ZDO_COORDINATOR )
      // Reset heartbeat for the sending child (find by source address)
      {
        uint8 ci;
        for (ci = 0; ci < ChildCount; ci++) {
          if (ChildAddrs[ci] == pkt->srcAddr.addr.shortAddr) {
            ChildLastSeen[ci] = 0;
            break;
          }
        }
      }

      if (pkt->cmd.Data[0] == 0x01 && pkt->cmd.DataLength >= 5) {
        // TEM1: DHT11 data [type, temp_shi, temp_ge, humi_shi, humi_ge]
        char sensorLog[48];
        TEM1_Temp = (int16)(pkt->cmd.Data[1] * 10 + pkt->cmd.Data[2]);
        TEM1_Humi = (uint16)(pkt->cmd.Data[3] * 10 + pkt->cmd.Data[4]);
        sprintf(sensorLog, "RCV-TEM1: %02X %02X %02X %02X %02X | T=%d.%d H=%d.%d\r\n",
                pkt->cmd.Data[0], pkt->cmd.Data[1], pkt->cmd.Data[2],
                pkt->cmd.Data[3], pkt->cmd.Data[4],
                TEM1_Temp / 10, TEM1_Temp % 10,
                TEM1_Humi / 10, TEM1_Humi % 10);
        HalUARTWrite(SERIAL_APP_PORT, (uint8 *)sensorLog, strlen(sensorLog));
        #if defined ( LCD_SUPPORTED )
          if (dispMode == 1) UpdateOled();
        #endif
      }
      else if (pkt->cmd.Data[0] == 0x02 && pkt->cmd.DataLength >= 2) {
        char sensorLog[48];
        // LIGHT2: light sensor data [type, light_val, pad]
        LIGHT2_Val = pkt->cmd.Data[1];
        sprintf(sensorLog, "RCV-LIGHT2: %02X %02X %02X | Light=%d%%\r\n",
                pkt->cmd.Data[0], pkt->cmd.Data[1], pkt->cmd.Data[2],
                LIGHT2_Val);
        HalUARTWrite(SERIAL_APP_PORT, (uint8 *)sensorLog, strlen(sensorLog));
        #if defined ( LCD_SUPPORTED )
          if (dispMode == 2) UpdateOled();
        #endif
      }
#endif
      break;

    case SERIALAPP_CONNECTREQ_CLUSTER:
      SerialApp_ConnectReqProcess((uint8*)pkt->cmd.Data);
      break;

    case SERIALAPP_CONNECTRSP_CLUSTER:
      SerialApp_DeviceConnectRsp((uint8*)pkt->cmd.Data);
      break;

    default:
      break;
  }
}

/*********************************************************************
 * @fn      SerialApp_Send
 *
 * @brief   Send data OTA.
 *
 * @param   none
 *
 * @return  none
 */
static void SerialApp_Send(void)
{
#if SERIAL_APP_LOOPBACK
    if (SerialApp_TxLen < SERIAL_APP_TX_MAX)
    {
        SerialApp_TxLen += HalUARTRead(SERIAL_APP_PORT, SerialApp_TxBuf+SerialApp_TxLen+1,
                                                      SERIAL_APP_TX_MAX-SerialApp_TxLen);
    }
  
    if (SerialApp_TxLen)
    {
      (void)SerialApp_TxAddr;
      if (HalUARTWrite(SERIAL_APP_PORT, SerialApp_TxBuf+1, SerialApp_TxLen))
      {
        SerialApp_TxLen = 0;
      }
      else
      {
        osal_set_event(SerialApp_TaskID, SERIALAPP_SEND_EVT);
      }
    }
#else
    if (!SerialApp_TxLen && 
        (SerialApp_TxLen = HalUARTRead(SERIAL_APP_PORT, SerialApp_TxBuf+1, SERIAL_APP_TX_MAX)))
    {
      // Pre-pend sequence number to the Tx message.
      SerialApp_TxBuf[0] = ++SerialApp_TxSeq;
    }
  
    if (SerialApp_TxLen)
    {
      if (afStatus_SUCCESS != AF_DataRequest(&SerialApp_TxAddr,
                                             (endPointDesc_t *)&SerialApp_epDesc,
                                              SERIALAPP_CLUSTERID1,
                                              SerialApp_TxLen+1, SerialApp_TxBuf,
                                              &SerialApp_MsgID, 0, AF_DEFAULT_RADIUS))
      {
        osal_set_event(SerialApp_TaskID, SERIALAPP_SEND_EVT);
      }
    }
#endif
}

/*********************************************************************
 * @fn      SerialApp_Resp
 *
 * @brief   Send data OTA.
 *
 * @param   none
 *
 * @return  none
 */
static void SerialApp_Resp(void)
{
  if (afStatus_SUCCESS != AF_DataRequest(&SerialApp_RxAddr,
                                         (endPointDesc_t *)&SerialApp_epDesc,
                                          SERIALAPP_CLUSTERID2,
                                          SERIAL_APP_RSP_CNT, SerialApp_RspBuf,
                                         &SerialApp_MsgID, 0, AF_DEFAULT_RADIUS))
  {
    osal_set_event(SerialApp_TaskID, SERIALAPP_RESP_EVT);
  }
}

/*********************************************************************
 * @fn      SerialApp_CallBack
 *
 * @brief   Send data OTA.
 *
 * @param   port - UART port.
 * @param   event - the UART port event flag.
 *
 * @return  none
 */
static void SerialApp_CallBack(uint8 port, uint8 event)
{
  (void)port;

  if ((event & (HAL_UART_RX_FULL | HAL_UART_RX_ABOUT_FULL | HAL_UART_RX_TIMEOUT)) &&
#if SERIAL_APP_LOOPBACK
      (SerialApp_TxLen < SERIAL_APP_TX_MAX))
#else
      !SerialApp_TxLen)
#endif
  {
    SerialApp_Send();
  }
}

#if defined ( LCD_SUPPORTED )
/*********************************************************************
 * @fn      UpdateOled
 *
 * @brief   Update OLED based on current display mode
 *********************************************************************/
static void UpdateOled(void)
{
  char lcdBuf[17];
  uint8 i;

  switch (dispMode) {
  case 0: // Node connection status + WiFi
    HalLcdWriteString( "team4", HAL_LCD_LINE_1 );
    sprintf(lcdBuf, "Nodes: %d", ChildCount);
    HalLcdWriteString( lcdBuf, HAL_LCD_LINE_2 );
#if defined ( ZDO_COORDINATOR )
    if (g_wifiState == 0)
      HalLcdWriteString( "WiFi:Init", HAL_LCD_LINE_3 );
    else if (g_wifiState == 1)
      HalLcdWriteString( "WiFi:Connecting", HAL_LCD_LINE_3 );
    else if (g_wifiState == 2)
      HalLcdWriteString( g_wifiClient ? "WiFi:OK Client" : "WiFi:OK NoCli", HAL_LCD_LINE_3 );
    else
      HalLcdWriteString( "WiFi:Fail", HAL_LCD_LINE_3 );
#else
    HalLcdWriteString( "", HAL_LCD_LINE_3 );
#endif
    HalLcdWriteString( "", HAL_LCD_LINE_4 );
    break;

  case 1: // TEM1: Temperature + Humidity
    HalLcdWriteString( "TEM1  DHT11", HAL_LCD_LINE_1 );
    if (TEM1_Temp != 0 || TEM1_Humi != 0) {
      sprintf(lcdBuf, "T: %d.%d C", TEM1_Temp/10, (TEM1_Temp<0? -TEM1_Temp:TEM1_Temp)%10);
      HalLcdWriteString( lcdBuf, HAL_LCD_LINE_2 );
      sprintf(lcdBuf, "H: %d.%d %%", TEM1_Humi/10, TEM1_Humi%10);
      HalLcdWriteString( lcdBuf, HAL_LCD_LINE_3 );
    } else {
      HalLcdWriteString( "Waiting data...", HAL_LCD_LINE_2 );
      HalLcdWriteString( "", HAL_LCD_LINE_3 );
    }
    HalLcdWriteString( "", HAL_LCD_LINE_4 );
    break;

  case 2: // LIGHT2: Light sensor
    HalLcdWriteString( "LIGHT2 Light", HAL_LCD_LINE_1 );
    if (LIGHT2_Val != 0) {
      sprintf(lcdBuf, "Light: %d %%", LIGHT2_Val);
      HalLcdWriteString( lcdBuf, HAL_LCD_LINE_2 );
      HalLcdWriteString( "", HAL_LCD_LINE_3 );
      HalLcdWriteString( "", HAL_LCD_LINE_4 );
    } else {
      HalLcdWriteString( "Waiting data...", HAL_LCD_LINE_2 );
      HalLcdWriteString( "", HAL_LCD_LINE_3 );
      HalLcdWriteString( "", HAL_LCD_LINE_4 );
    }
    break;
  }
}

// Backward compat
#define UpdateOled()  UpdateOled()
#endif

/*********************************************************************
 * @fn      SerialApp_HandleKeys
 *
 * @brief   Key event handler. S1 cycles OLED display mode.
 *********************************************************************/
static void SerialApp_HandleKeys( uint8 shift, uint8 keys )
{
  (void)shift;

  // S1 (P0_1, HAL_KEY_SW_6) cycles OLED display mode
  if ( keys & HAL_KEY_SW_6 )
  {
    dispMode = (dispMode + 1) % 3;
    #if defined ( LCD_SUPPORTED )
      UpdateOled();
    #endif
  }
}

/*********************************************************************
*********************************************************************/
void  SerialApp_DeviceConnect()
{
#if ZDO_COORDINATOR
  
#else
  
  uint16 nwkAddr;
  uint16 parentNwkAddr;
  char buff[30] = {0};
  
  HalLedBlink( HAL_LED_2, 3, 50, (1000 / 4) );
  
  nwkAddr = NLME_GetShortAddr();
  parentNwkAddr = NLME_GetCoordShortAddr();
  sprintf(buff, "parent:%d   self:%d\r\n", parentNwkAddr, nwkAddr);
  HalUARTWrite ( 0, (uint8*)buff, strlen(buff));

  #if defined ( LCD_SUPPORTED )
    char lcdBuf[17];
    sprintf(lcdBuf, "Me:%04X  Pa:%04X", nwkAddr, parentNwkAddr);
    HalLcdWriteString( lcdBuf, HAL_LCD_LINE_2 );
  #endif
  
  SerialApp_TxAddr.addrMode = (afAddrMode_t)Addr16Bit;
  SerialApp_TxAddr.endPoint = SERIALAPP_ENDPOINT;
  SerialApp_TxAddr.addr.shortAddr = parentNwkAddr;
  
  buff[0] = HI_UINT16( nwkAddr );
  buff[1] = LO_UINT16( nwkAddr );
  
  if ( AF_DataRequest( &SerialApp_TxAddr, &SerialApp_epDesc,
                       SERIALAPP_CONNECTREQ_CLUSTER,
                       2,
                       (uint8*)buff,
                       &SerialApp_MsgID, 
                       0, 
                       AF_DEFAULT_RADIUS ) == afStatus_SUCCESS )
  {
  }
  else
  {
    // Error occurred in request to send.
  }
  
#endif    //ZDO_COORDINATOR
}

void SerialApp_DeviceConnectRsp(uint8 *buf)
{
#if ZDO_COORDINATOR
  
#else
  SerialApp_TxAddr.addrMode = (afAddrMode_t)Addr16Bit;
  SerialApp_TxAddr.endPoint = SERIALAPP_ENDPOINT;
  SerialApp_TxAddr.addr.shortAddr = BUILD_UINT16(buf[1], buf[0]);
  
  HalLedSet(HAL_LED_2, HAL_LED_MODE_ON);
  HalUARTWrite ( 0, "< connect success>\n", 23);

  // Immediate sensor test
  {
    char dbg[48];
#if DEVICE_TYPE == 1
    DHT11();
    sprintf(dbg, "TEM1: %02X %02X %02X %02X %02X | T=%d.%d H=%d.%d\r\n",
            dht11_raw[0], dht11_raw[1], dht11_raw[2],
            dht11_raw[3], dht11_raw[4],
            wendu_shi, wendu_ge, shidu_shi, shidu_ge);
#elif DEVICE_TYPE == 2
    uint16 adcVal = HalAdcRead( HAL_ADC_CHANNEL_6, HAL_ADC_RESOLUTION_8 );
    uint8 rawLight = (uint8)(100 - (adcVal * 100 / 255));
    uint8 light = (rawLight <= 50) ? 0 :
                  (rawLight >= 100) ? 90 :
                  (uint8)((rawLight - 50) * 9 / 5);
    sprintf(dbg, "LIGHT2: ADC=%d Light=%d%%\r\n", adcVal, light);
#endif
    HalUARTWrite( 0, (uint8*)dbg, strlen(dbg) );
  }

  // Start periodic sensor data reporting (every 1 second)
  osal_start_timerEx( SerialApp_TaskID, SERIALAPP_SENSOR_EVT, 1000 );

  #if defined ( LCD_SUPPORTED )
    HalLcdWriteString( "Connected!", HAL_LCD_LINE_2 );
  #endif
#endif
}

void SerialApp_ConnectReqProcess(uint8 *buf)
{
  uint16 nwkAddr;
  uint16 childAddr;
  uint8 i;
  uint8 known = 0;
  char buff[30] = {0};

  childAddr = BUILD_UINT16(buf[1], buf[0]);
  SerialApp_TxAddr.addrMode = (afAddrMode_t)Addr16Bit;
  SerialApp_TxAddr.endPoint = SERIALAPP_ENDPOINT;
  SerialApp_TxAddr.addr.shortAddr = childAddr;
  nwkAddr = NLME_GetShortAddr();

  sprintf(buff, "self:%d   child:%d\r\n", nwkAddr, childAddr);
  HalUARTWrite ( 0, (uint8*)buff, strlen(buff));

  // Register child node if new, or reset heartbeat if known
  for (i = 0; i < ChildCount; i++) {
    if (ChildAddrs[i] == childAddr) {
      known = 1;
      ChildLastSeen[i] = 0;  // reset heartbeat
      break;
    }
  }
  if (!known && ChildCount < MAX_CHILD_NODES) {
    ChildAddrs[ChildCount] = childAddr;
    ChildLastSeen[ChildCount] = 0;
    ChildCount++;
  }

  #if defined ( LCD_SUPPORTED )
    UpdateOled();
  #endif

  buff[0] = HI_UINT16( nwkAddr );
  buff[1] = LO_UINT16( nwkAddr );

  if ( AF_DataRequest( &SerialApp_TxAddr, &SerialApp_epDesc,
                       SERIALAPP_CONNECTRSP_CLUSTER,
                       2,
                       (uint8*)buff,
                       &SerialApp_MsgID,
                       0,
                       AF_DEFAULT_RADIUS ) == afStatus_SUCCESS )
  {
  }
  else
  {
    // Error occurred in request to send.
  }

  HalLedSet(HAL_LED_2, HAL_LED_MODE_ON);
  HalUARTWrite ( 0, "< connect success>\n", 23);
}
