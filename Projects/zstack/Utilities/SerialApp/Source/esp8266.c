#include "hal_types.h"
#include "hal_mcu.h"
#include "hal_uart.h"
#include "OnBoard.h"
#include "esp8266.h"
#include <string.h>
#include <stdio.h>

#define IGT_PIN P0_6

// WiFi credentials
static char wifiSSID[] = "mljtb";
static char wifiPWD[]  = "12345678";

#define RBUF_SZ 128
static uint8 rbuf[RBUF_SZ];
static volatile uint8 rHead, rTail;

#define S_BOOT     0
#define S_CWMODE   1
#define S_CWJAP    2
#define S_CIPMUX   3
#define S_SERVER   4
#define S_READY    5
#define S_SENDLEN  6
#define S_SENDDAT  7
#define S_FAIL     8

static uint8 st = S_BOOT;
static uint8 ticks = 0;
static uint8 conn = 0;
static uint8 srvOk = 0;
static uint8 lastSt = 0xFF;

uint8  g_wifiState = 0;
uint8  g_wifiClient = 0;

static int16  _temp;
static uint16 _humi;
static uint8  _light;
static char pendingJson[96];
static uint8 rxScratch[128];
static char hexScratch[16];
static char commandBuf[96];
static char bodyBuf[80];

// ---- Debug output via UART0 (9600) ----
static void dbg(char *s)
{
  HalUARTWrite(0, (uint8*)s, strlen(s));
}

// ---- UART1 ----
static void u1_init(void)
{
  // EXACT copy of working example initUART1
  PERCFG &= ~0x02;
  P0SEL |= 0x30;     // P0_4, P0_5 as peripheral
  ADCCFG &= ~0x30;
  U1CSR = 0x80;
  U1GCR = 11;
  U1BAUD |= 216;     // BAUD_M = 216 → 115200 @ 32MHz
  U1UCR = 0x02;
  U1BAUD = 216;
  URX1IE = 1;
  URX1IF = 0;
  UTX1IF = 0;
  U1CSR |= 0x40;     // Enable RX
  rHead = rTail = 0;
}

static void u1_poll(void)
{
  // UART1 RX bytes are captured by the ISR below.
}

HAL_ISR_FUNCTION( esp8266Uart1RxIsr, URX1_VECTOR )
{
  uint8 b = U1DBUF;
  uint8 n = (rHead + 1) % RBUF_SZ;
  URX1IF = 0;
  if (n != rTail) {
    rbuf[rHead] = b;
    rHead = n;
  }
}

// Dump ring buffer to debug UART as hex
static void u1_dump(void)
{
  uint8 i = rTail;
  uint8 n = 0;
  while (i != rHead && n < 127) { rxScratch[n++] = rbuf[i]; i = (i+1)%RBUF_SZ; }
  if (n > 0) {
    dbg("HEX: ");
    for (i = 0; i < n && i < 50; i++) {
      sprintf(hexScratch, "%02X ", rxScratch[i]);
      dbg(hexScratch);
    }
    dbg(" | ");
    rxScratch[n>50?50:n] = 0;
    dbg((char*)rxScratch);
    dbg("\r\n");
  }
}

static uint8 u1_has(char *s)
{
  uint8 i = rTail;
  uint8 n = 0;
  while (i != rHead && n < 127) { rxScratch[n++] = rbuf[i]; i = (i+1)%RBUF_SZ; }
  rxScratch[n] = 0;
  return (strstr((char*)rxScratch, s) != NULL);
}

static void u1_flush(void) { rHead = rTail = 0; }

static void u1_tx(char *s)
{
  while (*s) {
    uint16 tout = 60000;
    UTX1IF = 0;
    U1DBUF = *s++;
    while (!UTX1IF && --tout);
    if (!tout) {
      dbg("[ESP] UART1 TX timeout\r\n");
      return;
    }
  }
}

static void at_send(char *cmd)
{
  dbg(">> "); dbg(cmd); dbg("\r\n");
  u1_tx(cmd); u1_tx("\r\n");
}

// ---- Public API ----
void ESP8266_Init(void)
{
  P0DIR |= 0x40; IGT_PIN = 0;
  u1_init();
  st = S_BOOT; ticks = 0; conn = 0; srvOk = 0; lastSt = 0xFF;
  g_wifiState = 0; g_wifiClient = 0;
  dbg("[ESP] Init\r\n");
}

void ESP8266_SetSensorData(int16 t, uint16 h, uint8 l)
{ _temp = t; _humi = h; _light = l; }

uint8 ESP8266_IsConnected(void) { return srvOk; }

// ---- State machine ----
void ESP8266_Tick(void)
{
  char *buf = commandBuf;
  char *body = bodyBuf;
  int8 neg; int16 tv;
  uint8 i;

  u1_poll();

  // Dump RX buffer to debug UART on state change
  if (st != lastSt) {
    dbg("[ESP] S="); buf[0]='0'+st; buf[1]=0; dbg(buf);
    dbg("\r\n");
    lastSt = st;
  }

  switch (st) {

  case S_BOOT:
    ticks++;
    if (ticks == 20) { IGT_PIN = 1; }   // 1s LOW reset pulse
    if (ticks >= 80) {  // 4s total boot wait
      ticks = 0; u1_flush(); g_wifiState = 1;
      at_send("AT+CWMODE=1"); st = S_CWMODE;
    }
    break;

  case S_CWMODE:
    ticks++;
    // Dump any response every 20 ticks (~1s)
    if (ticks == 20 && rHead != rTail) { dbg("CWMODE rx: "); u1_dump(); }
    if (u1_has("OK") || u1_has("no change")) {
      dbg("<< OK: "); u1_dump();
      ticks = 0; u1_flush();
      sprintf(buf, "AT+CWJAP=\"%s\",\"%s\"", wifiSSID, wifiPWD);
      at_send(buf); st = S_CWJAP;
    } else if (ticks > 40) {  // 2s timeout
      dbg("CWMODE timeout, buf: "); u1_dump();
      ticks = 0; st = S_FAIL;
    }
    break;

  case S_CWJAP:
    ticks++;
    if (u1_has("OK")) {
      dbg("<< "); u1_dump();
      ticks = 0; u1_flush();
      at_send("AT+CIPMUX=1"); st = S_CIPMUX;
    } else if (u1_has("FAIL") || u1_has("ERROR")) {
      dbg("<< FAIL: "); u1_dump();
      ticks = 0; st = S_FAIL;
    } else if (ticks > 300) {  // 15s timeout
      dbg("<< TIMEOUT: "); u1_dump();
      ticks = 0; st = S_FAIL;
    }
    break;

  case S_CIPMUX:
    ticks++;
    if (u1_has("OK")) {
      dbg("<< "); u1_dump();
      ticks = 0; u1_flush();
      at_send("AT+CIPSERVER=1,25576"); st = S_SERVER;
    } else if (ticks > 40) { ticks = 0; st = S_FAIL; }
    break;

  case S_SERVER:
    ticks++;
    if (u1_has("OK")) {
      dbg("<< "); u1_dump();
      ticks = 0; u1_flush(); srvOk = 1;
      g_wifiState = 2; g_wifiClient = 0;
      st = S_READY;
    } else if (ticks > 40) { ticks = 0; st = S_FAIL; }
    break;

  case S_READY:
    if (u1_has("CONNECT") || u1_has("LINK")) { conn = 1; g_wifiClient = 1; u1_flush(); dbg("[ESP] Client connected\r\n"); }
    if (u1_has("CLOSED") || u1_has("link is not")) { conn = 0; g_wifiClient = 0; u1_flush(); }

    ticks++;
    // Try sending regardless of conn flag — AT+CIPSEND fails gracefully if no client
    if (ticks >= 20) {
      ticks = 0;
      neg = (_temp < 0); tv = neg ? -_temp : _temp;
      sprintf(body, "{\"temp\":%s%d.%d,\"humi\":%d.%d,\"light\":%d}",
              neg?"-":"", tv/10, tv%10, _humi/10, _humi%10, _light);
      sprintf(buf, "AT+CIPSEND=0,%d", (int)strlen(body));
      at_send(buf);
      for (i = 0; body[i]; i++) pendingJson[i] = body[i];
      pendingJson[i] = 0;
      st = S_SENDLEN;
    }
    break;

  case S_SENDLEN:
    ticks++;
    if (u1_has(">")) {
      ticks = 0; u1_flush();
      u1_tx(pendingJson); u1_tx("\r\n");
      st = S_SENDDAT;
    } else if (u1_has("ERROR") || u1_has("link is not")) {
      ticks = 0; u1_flush(); st = S_READY;
    } else if (ticks > 40) { ticks = 0; st = S_READY; }
    break;

  case S_SENDDAT:
    ticks++;
    if (u1_has("SEND OK")) {
      ticks = 0; u1_flush(); st = S_READY;
    } else if (u1_has("SEND FAIL") || u1_has("ERROR")) {
      ticks = 0; u1_flush(); st = S_READY;
    } else if (ticks > 40) { ticks = 0; st = S_READY; }
    break;

  case S_FAIL:
    ticks++;
    if (ticks > 40) {
      ticks = 0; g_wifiState = 3;
      IGT_PIN = 0;
      st = S_BOOT;
    }
    break;
  }
}
