#include <Arduino.h>
#include "BluetoothA2DPSink.h"

BluetoothA2DPSink a2dp_sink;

// --- 【核心安全锁】 ---
// 经过测试，Windows 音量 31（约占总音量 127 的 25%）是削波临界点。
// 我们在此设定硬件允许输出的绝对最大真实音量（0-127范围内）
const int MAX_SAFE_VOLUME = 35; // 设定在35，留一点安全冗余

// 蓝牙元数据回调
void avrc_metadata_callback(uint8_t id, const uint8_t *text) {
  if (id == ESP_AVRC_MD_ATTR_TITLE) {
    Serial2.print("T:"); Serial2.println((char*)text);
  }
  if (id == ESP_AVRC_MD_ATTR_ARTIST) {
    Serial2.print("A:"); Serial2.println((char*)text);
  }
}

void setup() {
  Serial2.begin(115200, SERIAL_8N1, 33, 32); 
  Serial2.setTimeout(10); 

  // I2S 音频引脚配置 (连紫板 PCM5102)
  i2s_pin_config_t my_pin_config = {
    .bck_io_num = 26,
    .ws_io_num = 25,
    .data_out_num = 22,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  a2dp_sink.set_pin_config(my_pin_config);
  a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
  
  a2dp_sink.start("DeepSpace_HiFi_Link"); 
  
  // 系统启动时先设定一个极低的默认安全音量
  a2dp_sink.set_volume(15); 
}

void loop() {
  // --- 防线 1：拦截 S3 屏幕发来的旋钮指令，进行等比例安全映射 ---
  if (Serial2.available()) {
    String cmd = Serial2.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("V")) {
      int reqVol = cmd.substring(1).toInt(); // 获取 S3 传来的 0-127
      
      // 【数学压缩】将屏幕的 0-127 等比例映射到真实的 0-MAX_SAFE_VOLUME
      int safeVol = map(reqVol, 0, 127, 0, MAX_SAFE_VOLUME);
      a2dp_sink.set_volume(safeVol); 
    }
  }

  // --- 防线 2：暴力压制 Windows 11 等系统的“绝对音量”强突 ---
  // 如果你在电脑上把音量拉到 100，这里会瞬间把它死死按回 35！
  if (a2dp_sink.get_volume() > MAX_SAFE_VOLUME) {
    a2dp_sink.set_volume(MAX_SAFE_VOLUME);
  }
  
  delay(10); 
}