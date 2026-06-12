#include "hal_types.h"
#include "hal_mcu.h"
#include "OnBoard.h"
#include "DHT11.h"

typedef unsigned char uchar;
typedef unsigned int  uint;

#define DATA_PIN P0_7
#define DATA_PDIR P0DIR
#define DATA_PMASK 0x80

uchar ucharFLAG, uchartemp;
uchar shidu_shi, shidu_ge, wendu_shi, wendu_ge;
uchar ucharT_data_H, ucharT_data_L, ucharRH_data_H, ucharRH_data_L, ucharcheckdata;
uchar dht11_raw[5];

// NOP-based delay calibrated for CC2530 @ 32MHz
// 1 NOP ~= 31.25ns, ~32 NOPs = 1us
static void Delay_us(uint n) {
  while (n--) { MicroWait(1); }
}

static void Delay_ms(uint n) {
  while (n--) { MicroWait(1000); }
}

// Read one byte from DHT11
static uchar DHT11_ReadByte(void)
{
  uchar i, val = 0;
  for (i = 0; i < 8; i++)
  {
    ucharFLAG = 2;
    while (!DATA_PIN && ucharFLAG++);
    // Delay ~30us
    MicroWait(28);
    // Read bit
    uchartemp = 0;
    if (DATA_PIN) uchartemp = 1;
    ucharFLAG = 2;
    while (DATA_PIN && ucharFLAG++);
    val <<= 1;
    val |= uchartemp;
  }
  return val;
}

void DHT11(void)
{
  uchar tmp;

  // Start signal: pull low >18ms
  DATA_PDIR |= DATA_PMASK;   // Output mode
  DATA_PIN = 0;
  Delay_ms(20);

  // Pull high and switch to input
  DATA_PIN = 1;
  MicroWait(30);
  DATA_PDIR &= ~DATA_PMASK;  // Input mode

  // Wait for DHT11 response
  if (!DATA_PIN)
  {
    ucharFLAG = 2;
    while (!DATA_PIN && ucharFLAG++);
    ucharFLAG = 2;
    while (DATA_PIN && ucharFLAG++);

    // Read 5 bytes: RH_int, RH_dec, T_int, T_dec, checksum
    ucharRH_data_H = DHT11_ReadByte();
    ucharRH_data_L = DHT11_ReadByte();
    ucharT_data_H = DHT11_ReadByte();
    ucharT_data_L = DHT11_ReadByte();
    ucharcheckdata = DHT11_ReadByte();

    DATA_PDIR |= DATA_PMASK; // Back to output
    DATA_PIN = 1;

    // Store raw bytes for debug
    dht11_raw[0] = ucharRH_data_H;
    dht11_raw[1] = ucharRH_data_L;
    dht11_raw[2] = ucharT_data_H;
    dht11_raw[3] = ucharT_data_L;
    dht11_raw[4] = ucharcheckdata;

    // Verify checksum
    tmp = ucharT_data_H + ucharT_data_L + ucharRH_data_H + ucharRH_data_L;
    if (tmp == ucharcheckdata)
    {
      // Apply calibration: offset -3.4°C = -34 in x10 units
      // Convert to signed x10, apply offset, clamp, split back
      {
        int16 t10 = (int16)(ucharT_data_H) * 10 + ucharT_data_L;
        t10 -= 34;  // TEMP_CALIBRATION: offset -3.4°C
        if (t10 < 0) t10 = 0;
        wendu_shi = (uchar)(t10 / 10);
        wendu_ge  = (uchar)(t10 % 10);
      }
      shidu_shi  = ucharRH_data_H;
      shidu_ge   = ucharRH_data_L;
    }
    else
    {
      wendu_shi = 0; wendu_ge = 0;
      shidu_shi = 0; shidu_ge = 0;
    }
  }
  else
  {
    DATA_PDIR |= DATA_PMASK;
    DATA_PIN = 1;
    wendu_shi = 0; wendu_ge = 0;
    shidu_shi = 0; shidu_ge = 0;
  }
}
