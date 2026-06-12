#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>

// --- 硬件引脚定义 ---
#define TFT_CS    10
#define TFT_RST   8
#define TFT_DC    9
#define TFT_MOSI  11
#define TFT_SCLK  12
#define BLK_PIN   13

#define ENC_CLK   15
#define ENC_DT    16
#define ENC_SW    17

// 实例化驱动对象
Adafruit_ST7735 tft = Adafruit_ST7735(&SPI, TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// --- 全局变量 ---
volatile int volume = 50;  
int lastVolume = -1;
unsigned long lastSpectrumTime = 0; // 频谱刷新计时器

// 优化点1：记录上一帧频谱柱的高度，用于按需刷新
int lastBarHeights[15] = {0};

// 编码器硬件中断服务函数（含防抖）
void IRAM_ATTR readEncoder() {
  static unsigned long lastInterruptTime = 0;
  unsigned long now = millis();
  // 5ms 防抖：忽略 5ms 内的连续触发
  if (now - lastInterruptTime < 5) return;
  lastInterruptTime = now;

  static bool lastState = HIGH;
  bool currentState = digitalRead(ENC_CLK);
  
  if (currentState != lastState && currentState == LOW) {
    if (digitalRead(ENC_DT) == HIGH) {
      volume += 5;
    } else {
      volume -= 5;
    }
    if (volume > 100) volume = 100;
    if (volume < 0) volume = 0;
  }
  lastState = currentState;
}

void setup() {
  Serial.begin(115200);

  // 1. 基础硬件初始化
  pinMode(BLK_PIN, OUTPUT);
  digitalWrite(BLK_PIN, HIGH);
  
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), readEncoder, CHANGE);

  // 2. SPI 与屏幕初始化
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1); // 160宽 x 128高
  tft.fillScreen(ST77XX_BLACK);

  // 3. 挂载完整版中文字库 (GB2312)
  u8g2.begin(tft);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312); // 12像素高清中文字体
  u8g2.setFontMode(1);                    // 透明背景模式
  
  // ==========================================
  // 绘制静态 UI 框架 (划分四个核心区域)
  // ==========================================
  
  // 区域 1：顶部状态栏 (0-20)
  tft.drawRect(0, 0, 160, 128, ST77XX_GREEN);       // 极简全局外框
  tft.drawLine(0, 20, 160, 20, ST77XX_GREEN);       // 状态栏分割线
  u8g2.setForegroundColor(ST77XX_GREEN);
  u8g2.setCursor(5, 15);
  u8g2.print("深空通信终端 V1.0");
  
  // 区域 2：歌名与播放状态预留 (25-45)
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(5, 38);
  u8g2.print("播放: 暂无音频输入");

  // 区域 3：歌词与 AI 对话大面积预留 (50-85)
  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.setCursor(5, 60);
  u8g2.print(">> 系统待命...");
  u8g2.setCursor(5, 75);
  u8g2.print(">> 预留多行歌词/AI字幕区域");
  
  // 分割线，将底部控制区隔开
  tft.drawLine(0, 110, 160, 110, 0x03E0); // 深绿色分割线
}

void loop() {
  unsigned long currentMillis = millis();

  // ==========================================
  // 动态更新 1：极细音量条 (仅在变化时局部刷新)
  // ==========================================
  if (volume != lastVolume) {
    // 擦除旧音量数字 (区域：x=35, y=114, 宽=25, 高=12)
    tft.fillRect(35, 114, 25, 12, ST77XX_BLACK);
    
    // 打印中文"音量"和数值
    u8g2.setForegroundColor(ST77XX_YELLOW);
    u8g2.setCursor(5, 125);
    u8g2.print("音量:");
    
    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(35, 116);
    tft.print(volume);

    // 绘制极细进度条 (高度仅占 4 像素，内部 2 像素)
    int barWidth = map(volume, 0, 100, 0, 88); 
    tft.drawRect(65, 117, 90, 4, ST77XX_WHITE);                // 进度条外框
    tft.fillRect(66, 118, 88, 2, ST77XX_BLACK);                // 擦除旧进度
    
    // 按下按键变红，否则为绿
    uint16_t barColor = (digitalRead(ENC_SW) == LOW) ? ST77XX_RED : ST77XX_GREEN;
    tft.fillRect(66, 118, barWidth, 2, barColor);              // 填充新进度

    lastVolume = volume;
  }

  // ==========================================
  // 动态更新 2：赛博朋克伪音频频谱 (仅变化柱条刷新)
  // ==========================================
  if (currentMillis - lastSpectrumTime > 80) {
    lastSpectrumTime = currentMillis;
    
    // 在 x=5 到 x=155 之间画 15 根频谱柱，底部 y=105
    for(int i = 0; i < 15; i++) {
      int max_height = map(volume, 0, 100, 2, 20);
      int h = random(1, max_height + 1);
      
      // 优化点：仅当高度变化时才重绘
      if (h != lastBarHeights[i]) {
        // 局部刷新：先用黑色擦除这一根柱子的最大可能高度，再画新柱子
        tft.fillRect(5 + i * 10, 105 - 20, 8, 20, ST77XX_BLACK); 
        tft.fillRect(5 + i * 10, 105 - h, 8, h, ST77XX_CYAN);
        lastBarHeights[i] = h;   // 更新记录
      }
    }
  }
}