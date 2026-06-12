#ifndef __DHT11_H__
#define __DHT11_H__

extern void DHT11(void);

extern unsigned char wendu_shi, wendu_ge;
extern unsigned char shidu_shi, shidu_ge;

// Raw bytes for debug
extern unsigned char dht11_raw[5];

#endif
