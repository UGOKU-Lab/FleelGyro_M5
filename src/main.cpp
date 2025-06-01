#include <M5StickCPlus2.h>
#include <M5GFX.h>  
#include "UGOKU_Pad_Controller.hpp" 
#include "Wire.h"
#define I2C_DEV_ADDR 0x52

#define Ef5 622
#define Bf4 466
#define Af4 415
#define Ef4 311

UGOKU_Pad_Controller controller;      // BLEコントローラオブジェクト
uint8_t CH;                           // BLEチャンネル
uint8_t VAL;                          // BLEボタン値

uint32_t rpm;
uint16_t motor_val;
uint8_t md_set;

bool isConnected = false;             // BLE接続状態

const int buzzerPin = 2;  // G2 ピンにブザー

const int analogPin = 36;
const float R1 = 2000.0;
const float R2 = 220.0;
const float voltageDividerRatio = R2 / (R1 + R2);
const float adcMax = 4095.0;
const float vRef = 3.3;

int temp = 23;

//仮GPIOコントロール
const int BUTTON_SET_LOW = 26;   // G26
const int BUTTON_SET_HIGH = 0;  // G0


bool prevBtnHigh = HIGH;
bool prevBtnLow = HIGH;
uint8_t setControlState = 0;
int prevValCh0 = HIGH;
int prevValCh1 = HIGH;
int prevValCh2 = HIGH;

// UART通信用（追加） ← 追加
//bool prevSentControlState = !setControlState;  // 初回は必ず送信 ← 追加
uint8_t prevSentControlState = 0xFF;
String uartRxBuffer = "";                      // STM32からの文字列受信用 ← 追加

// M5GFX用スプライト
LGFX_Sprite sprite = LGFX_Sprite(&M5.Lcd);
const int spriteWidth = 240;
const int spriteHeight = 135;
uint16_t SFGreen = sprite.color565(0, 255, 180);

void playXPSound() {
  float qn = 0.3;

  tone(buzzerPin, Ef5); delay(qn * 3 / 4 * 1500);     
  tone(buzzerPin, Ef4); delay(qn * 500);     
  tone(buzzerPin, Bf4); delay(qn * 1000);        
  tone(buzzerPin, Af4); delay(qn * 1500);   
  tone(buzzerPin, Ef5); delay(qn * 1000);         
  tone(buzzerPin, Bf4); delay(qn * 2000);     
  noTone(buzzerPin); 
}

void showWelcomeScreen() {
  M5.Lcd.fillRect(0, 0, 240, 45, 0x0010);
  M5.Lcd.fillRect(0, 18, 240, 120, 0x03BF);
  M5.Lcd.fillRect(0, 117, 240, 45, 0x0010);
  M5.Lcd.drawLine(0, 18, 240, 18, 0x7DDF);
  M5.Lcd.drawLine(0, 117, 240, 117, 0xF800);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE, 0x03BF);
  M5.Lcd.setCursor(120, 135/2 - 16/2);
  M5.Lcd.print("welcome");
}

float calibrateVoltage(float rawVoltage) {
  return rawVoltage + 1.2;
}

// バッテリー表示（スプライト描画用）
void drawBattery(float voltage, float percentage) {
  int margin = 2;
  int x = 40, y = 75;
  int w = 35, h = 50;
  int usableHeight = h - margin * 2;
  int levelHeight = map((int)percentage, 0, 100, 0, usableHeight);

  // 色決定
  uint16_t fillColor = GREEN;
  if (percentage < 20) fillColor = RED;
  else if (percentage < 60) fillColor = YELLOW;

  // バッテリー枠と端子
  sprite.drawRect(x, y, w, h, WHITE);
  sprite.drawRect(x + 8, y - 6, 19, 7, WHITE);

  // バッテリー中身
  if (levelHeight > 0) {
    int fillY = y + h - margin - levelHeight;
    sprite.fillRect(x + margin, fillY, w - margin * 2, levelHeight, fillColor);
  }

  // 注意マーク（10%未満）
  if (percentage < 10.0) {
    sprite.setTextSize(4);
    sprite.setTextColor(RED, BLACK);
    sprite.setCursor(x + 7, y + 11);
    sprite.print("!");
    sprite.setTextColor(WHITE, BLACK); // 元に戻す
  }
}

void onDeviceConnect() {
  Serial.println("Device connected!");
  isConnected = true;
}

void onDeviceDisconnect() {
  Serial.println("Device disconnected!");
  isConnected = false;
}

void scanI2C() {
  byte error, address;
  int nDevices = 0;
  
  Serial.println("Scanning...");
  
  for(address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println(" !");
      nDevices++;
    }
  }
  
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");
}

void setup() {
  M5.begin();
  Serial.begin(115200);
  //Serial2.begin(38400, SERIAL_8N1, 33, 32);  // ← UART初期化（STM32と通信）
  Wire.begin(32,33);  //sda,scl,frequency
  M5.Lcd.setRotation(3);
  showWelcomeScreen();
  analogReadResolution(12);
  playXPSound();

  sprite.createSprite(spriteWidth, spriteHeight);
  sprite.setTextColor(SFGreen, BLACK);

  pinMode(BUTTON_SET_HIGH, INPUT_PULLUP);
  pinMode(BUTTON_SET_LOW, INPUT_PULLUP);
  
  //pinMode(CONTROL_PIN, OUTPUT);
  //digitalWrite(CONTROL_PIN, setControlState);

  controller.setup("GYRO");
  controller.setOnConnectCallback(onDeviceConnect);
  controller.setOnDisconnectCallback(onDeviceDisconnect);

  //void scanI2C();
}

void loop() {
  // --- 電圧測定・表示 ---
  int raw = analogRead(analogPin);
  float voltageAtPin = (raw / adcMax) * vRef;
  float batteryVoltage = calibrateVoltage(voltageAtPin / voltageDividerRatio);
  float percentage = constrain((batteryVoltage - 20.5) / (25.2 - 20.5) * 100.0, 0.0, 100.0);

  // スプライトに描画
  sprite.fillSprite(BLACK);
  sprite.setTextColor(SFGreen, BLACK); 

  // 回転数右寄
  sprite.setTextSize(4);
  char rpmStr[10];
  sprintf(rpmStr, "%d", rpm);
  
  int textW = sprite.textWidth(rpmStr);  // 表示幅を計算
  int x = 160 + 24 * 3 - textW - 6;     // 右端から詰めて配置
  
  sprite.setCursor(x, 17);               // Y位置はそのままでOK
  sprite.print(rpmStr);

  // 回転数の描画（スプライトに）
  sprite.setTextSize(3);    
  sprite.setCursor(130 + (24*4 - 18*3) , 50);  
  sprite.printf("RPM");

  // 巻き線温度
  sprite.setFont(&fonts::Font0);  
  sprite.setTextSize(4);       
  sprite.setCursor(130 + (24*4 - (24*2 + 18 + 10)), 88);           
  sprite.printf("%d", temp);
  sprite.drawCircle(130 + (24*4 - (24*2 + 18 + 10)) + 18*2 + 16, 88 + 9, 3, SFGreen);
  sprite.setTextSize(3); 
  sprite.setCursor(130 + (24*4 - (24*2 + 18 + 5)) + 48 + 5,88+6);  
  sprite.printf("C");

  // 数字表示（左寄せ調整）
  sprite.setTextSize(3);
  sprite.setCursor(30, 10);
  sprite.printf("%3.0f%%", percentage);
  sprite.setCursor(30-18, 40);
  sprite.printf("%.1fV", batteryVoltage);

  // バッテリー表示
  sprite.setTextSize(1);
  drawBattery(batteryVoltage, percentage);

  // 画面に表示
  sprite.pushSprite(0, 0);

  if (batteryVoltage < 5.0) {  // 電源切れ時の「ありえない低い電圧」で判定
    M5.Lcd.fillScreen(BLACK);  // LCD消す（見た目対策）
    delay(100);                // 表示反映の時間
    esp_deep_sleep_start();    // 自動スリープ！
  }

  // --- GPIOボタン処理 ---
  bool currBtnHigh = digitalRead(BUTTON_SET_HIGH);
  bool currBtnLow = digitalRead(BUTTON_SET_LOW);
  if (prevBtnHigh == HIGH && currBtnHigh == LOW) {
    setControlState = 1;
  } else if (prevBtnLow == HIGH && currBtnLow == LOW) {
    setControlState = 0;
  }

  // --- BLEボタン処理 ---
  if(isConnected) {
    controller.read_data();
    CH = controller.get_ch();
    VAL = controller.get_val();

    if (CH == 0 && VAL == HIGH && prevValCh0 == LOW) {
      setControlState = 0;
    }else if(CH == 1 && VAL == HIGH && prevValCh1 == LOW){
      setControlState = 1;
    }else if(CH == 2 && VAL == HIGH && prevValCh2 == LOW){
      setControlState = 2;
    }

    if (CH == 0) prevValCh0 = VAL;
    if (CH == 1) prevValCh1 = VAL;
    if (CH == 2) prevValCh2 = VAL;

    controller.write_data(2, rpm / 100);  // 回転数の送信（あくまでBLE用）
  }

  prevBtnHigh = currBtnHigh;
  prevBtnLow = currBtnLow;

  Serial.printf("motor_val = %d\r\n",(int)motor_val);
  Serial.printf("md_set = %d\r\n",(int)md_set);

  //Write message to the slave
  Wire.beginTransmission(I2C_DEV_ADDR);
  for(int wc=0;wc<3;wc++){
    Wire.write(setControlState);
  }
  uint8_t error = Wire.endTransmission(true);
  Serial.printf("endTransmission: %u\n", error);

  //Read 16 bytes from the slave
  uint8_t bytesReceived = Wire.requestFrom(I2C_DEV_ADDR, 4);
  Serial.printf("requestFrom: %u\n", bytesReceived);
  if((bool)bytesReceived){ //If received more than zero bytes
    uint8_t temp[bytesReceived];
    Wire.readBytes(temp, bytesReceived);
    Serial.printf("rx data %#x, %#x, %#x, %#x\r\n",temp[0],temp[1],temp[2],temp[3]);
    rpm = temp[3]<<24 | temp[2]<<16 | temp[1]<<8 | temp[0];
    Serial.printf("I2C val :%d\r\n",(int)rpm);
  }  

  delay(30);
}
