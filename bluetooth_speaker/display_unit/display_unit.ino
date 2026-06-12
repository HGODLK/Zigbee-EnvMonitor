#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <U8g2_for_Adafruit_GFX.h>

// --- S3 引脚定义 ---
#define TFT_CS    10
#define TFT_RST   8
#define TFT_DC    9
#define TFT_MOSI  11
#define TFT_SCLK  12
#define BLK_PIN   13
#define ENC_CLK   15
#define ENC_DT    16
#define ENC_SW    17

Adafruit_ST7735 tft = Adafruit_ST7735(&SPI, TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// --- 真实状态变量 ---
volatile int volume = 80;
int lastVolume = -1;
String currentSong = "等待蓝牙接入...";
String currentArtist = "";
String lastSong = "";
String lastArtist = "";
bool initialDraw = true;

// --- 旋钮硬件中断 ---
void IRAM_ATTR readEncoder() {
  static bool lastState = HIGH;
  bool currentState = digitalRead(ENC_CLK);
  if (currentState != lastState && currentState == LOW) {
    if (digitalRead(ENC_DT) == HIGH) volume += 5; else volume -= 5;
    if (volume > 127) volume = 127; if (volume < 0) volume = 0;
  }
  lastState = currentState;
}

// --- 纯粹的工业风音量条 ---
void drawRealVolume(int vol) {
  tft.fillRect(0, 105, 160, 23, ST77XX_BLACK); // 清空底部区域
  
  // 外框
  tft.drawRect(10, 110, 140, 10, ST77XX_WHITE);
  
  // 填充实体
  int fillWidth = map(vol, 0, 127, 0, 136);
  if (fillWidth > 0) {
    tft.fillRect(12, 112, fillWidth, 6, ST77XX_WHITE);
  }
  
  // 纯数字反馈
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(70, 105);
  u8g2.print("VOL");
}

void setup() {
  Serial1.begin(115200, SERIAL_8N1, 5, 4); // 双芯真实数据桥梁

  pinMode(BLK_PIN, OUTPUT); digitalWrite(BLK_PIN, HIGH);
  pinMode(ENC_CLK, INPUT_PULLUP); pinMode(ENC_DT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), readEncoder, CHANGE);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  
  u8g2.begin(tft);
}

void loop() {
  // --- 模块 A：监听物理旋钮并发送指令 ---
  if (volume != lastVolume || initialDraw) {
    Serial1.print("V"); Serial1.println(volume); // 把真实指令发给经典款
    drawRealVolume(volume);
    lastVolume = volume;
  }

  // --- 模块 B：监听经典款发来的真实数据 ---
  if (Serial1.available()) {
    String msg = Serial1.readStringUntil('\n');
    msg.trim();
    if (msg.startsWith("T:")) currentSong = msg.substring(2);
    else if (msg.startsWith("A:")) currentArtist = msg.substring(2);
  }

  // --- 模块 C：极简文本渲染 ---
  if (currentSong != lastSong || currentArtist != lastArtist || initialDraw) {
    tft.fillRect(0, 0, 160, 95, ST77XX_BLACK); // 只清空文字区
    
    // 1. 曲名渲染 (支持简单的超长截断/换行显示)
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setForegroundColor(ST77XX_WHITE);
    
    // 粗略判断长度，如果太长就分两行
    if (currentSong.length() > 18) {
      u8g2.setCursor(5, 30);
      u8g2.print(currentSong.substring(0, 18)); // 第一行
      u8g2.setCursor(5, 55);
      u8g2.print(currentSong.substring(18));    // 第二行溢出部分
    } else {
      u8g2.setCursor(5, 40);
      u8g2.print(currentSong); // 短歌名居中显示
    }

    // 2. 歌手渲染 (青色，在曲名下方)
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.setForegroundColor(ST77XX_CYAN);
    u8g2.setCursor(5, 85);
    u8g2.print(currentArtist);

    lastSong = currentSong;
    lastArtist = currentArtist;
    initialDraw = false;
  }

  delay(20);
}