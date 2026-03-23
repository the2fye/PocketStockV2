#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

/* ================= DISPLAY ================= */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C
#define SDA_PIN 4
#define SCL_PIN 5
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/* ================= BUTTONS ================= */
#define BTN_UP     6
#define BTN_DOWN   7
#define BTN_SELECT 8

/* ================= WIFI ================= */
const char* ssid = "WhiteSky-PublicWiFi";

/* ================= STOCKS ================= */
String stocks[] = {
  "AAPL","MSFT","NVDA","AMD","GOOGL","META","TSLA",
  "XOM","CVX","BP","SHEL","COP"
};
const int NUM_STOCKS = sizeof(stocks) / sizeof(stocks[0]);
int stockIndex = 0;
int stockScrollOffset = 0;
const int VISIBLE_STOCKS = 6;

/* ================= FINNHUB ================= */
const char* apiKey = "d1qhgl9r01qo4qd79j00d1qhgl9r01qo4qd79j0g";

/* ================= UI STATES ================= */
enum UIState {
  MAIN_MENU,
  STOCK_LIST,
  STOCK_VIEW,
  SETTINGS_MENU,
  SETTINGS_WIFI,
  SETTINGS_ABOUT,
  GAME_PONG
};
UIState uiState = MAIN_MENU;

/* ================= MENUS ================= */
int mainMenuIndex = 0;
int settingsIndex = 0;

/* ================= TIMING ================= */
unsigned long lastFetch = 0;
const unsigned long fetchInterval = 3000;

/* ================= LONG PRESS ================= */
bool selectHeld = false;
unsigned long selectPressTime = 0;
const unsigned long LONG_PRESS_MS = 700;

/* ================= SPARKLINE ================= */
#define GRAPH_X 112
#define GRAPH_Y 12
#define GRAPH_W 14
#define GRAPH_H 40
#define HISTORY_SIZE 20
float priceHistory[HISTORY_SIZE];
int historyIndex = 0;
bool historyFilled = false;

/* ================= TIME ================= */
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -5 * 3600;
const int daylightOffset_sec = 3600;

/* ================= PONG ================= */
int paddleY = 24;
int enemyPaddleY = 24;
int ballX, ballY;
int ballVX, ballVY;
int pongScore = 0;
int pongHighScore = 0;
const int BALL_SPEED_X = 2;
const int BALL_SPEED_Y = 2;

/* ================= BOOT LOGO ================= */
void showBootScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.drawRect(8, 18, 28, 28, SSD1306_WHITE);
  display.drawLine(12, 40, 18, 34, SSD1306_WHITE);
  display.drawLine(18, 34, 24, 36, SSD1306_WHITE);
  display.drawLine(24, 36, 30, 26, SSD1306_WHITE);
  display.fillTriangle(28, 26, 30, 22, 32, 26, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(42, 20); display.println("Pocket");
  display.setCursor(42, 38); display.println("Stock");

  display.setTextSize(1);
  display.setCursor(34, 56); display.println("Market Pulse");
  display.display();
  delay(2000);
}

/* ================= HELPERS ================= */

bool isMarketOpen(struct tm* t) {
  int w = t->tm_wday, h = t->tm_hour, m = t->tm_min;
  if (w == 0 || w == 6) return false;
  if (h < 9 || h > 16) return false;
  if (h == 9 && m < 30) return false;
  if (h == 16 && m > 0) return false;
  return true;
}

void drawTimeAndMarket() {
  struct tm t;
  if (!getLocalTime(&t)) return;

  char buf[6];
  strftime(buf, sizeof(buf), "%H:%M", &t);
  display.setCursor(88, 0);
  display.print(buf);

  int x = 108, y = 10;
  if (isMarketOpen(&t)) {
    display.drawRect(x, y, 10, 10, SSD1306_WHITE);
    display.fillTriangle(x+5,y+2,x+8,y+6,x+2,y+6,SSD1306_WHITE);
  } else {
    display.fillCircle(x+5,y+5,4,SSD1306_WHITE);
    display.fillCircle(x+7,y+5,4,SSD1306_BLACK);
  }
}

/* ================= SPARKLINE ================= */

void resetHistory() {
  historyIndex = 0;
  historyFilled = false;
}

void addPrice(float p) {
  priceHistory[historyIndex++] = p;
  if (historyIndex >= HISTORY_SIZE) {
    historyIndex = 0;
    historyFilled = true;
  }
}

void drawSparkline() {
  int count = historyFilled ? HISTORY_SIZE : historyIndex;
  if (count < 2) return;

  float minP = priceHistory[0], maxP = priceHistory[0];
  for (int i = 1; i < count; i++) {
    minP = min(minP, priceHistory[i]);
    maxP = max(maxP, priceHistory[i]);
  }
  if (minP == maxP) maxP += 1;

  for (int i = 1; i < count; i++) {
    int x0 = GRAPH_X + (i-1)*GRAPH_W/(count-1);
    int x1 = GRAPH_X + i*GRAPH_W/(count-1);
    int y0 = map(priceHistory[i-1], minP, maxP, GRAPH_Y+GRAPH_H, GRAPH_Y);
    int y1 = map(priceHistory[i],   minP, maxP, GRAPH_Y+GRAPH_H, GRAPH_Y);
    display.drawLine(x0,y0,x1,y1,SSD1306_WHITE);
  }
}

/* ================= PONG ================= */

void resetPong() {
  pongHighScore = max(pongHighScore, pongScore);
  pongScore = 0;
  paddleY = 24;
  enemyPaddleY = 24;
  ballX = SCREEN_WIDTH/2;
  ballY = SCREEN_HEIGHT/2;
  ballVX = BALL_SPEED_X;
  ballVY = BALL_SPEED_Y;
}

void updatePong() {
  ballX += ballVX;
  ballY += ballVY;

  if (ballY <= 1 || ballY >= SCREEN_HEIGHT-2) ballVY *= -1;

  if (ballX <= 6 && ballY >= paddleY && ballY <= paddleY+16) {
    ballVX = BALL_SPEED_X;
    pongScore++;
  }

  if (ballX >= SCREEN_WIDTH-8 && ballY >= enemyPaddleY && ballY <= enemyPaddleY+16)
    ballVX = -BALL_SPEED_X;

  enemyPaddleY += (ballY > enemyPaddleY+8) ? 1 : -1;
  enemyPaddleY = constrain(enemyPaddleY,0,SCREEN_HEIGHT-16);

  if (ballX < 0) resetPong();
  if (ballX > SCREEN_WIDTH) {
    ballX = SCREEN_WIDTH/2;
    ballY = SCREEN_HEIGHT/2;
  }
}

void drawPong() {
  display.clearDisplay();
  display.drawRect(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,SSD1306_WHITE);
  display.setCursor(18,2);
  display.print("Score:");
  display.print(pongScore);
  display.print(" Hi:");
  display.print(pongHighScore);
  display.fillRect(2,paddleY,3,16,SSD1306_WHITE);
  display.fillRect(SCREEN_WIDTH-5,enemyPaddleY,3,16,SSD1306_WHITE);
  display.fillCircle(ballX,ballY,2,SSD1306_WHITE);
  display.display();
}

/* ================= UI DRAW ================= */

void drawMainMenu() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0,12);
  display.print(mainMenuIndex==0?"> ":"  "); display.println("Stocks");
  display.setCursor(0,36);
  display.print(mainMenuIndex==1?"> ":"  "); display.println("Settings");
  display.display();
}

void drawStockList() {
  display.clearDisplay();
  display.setTextSize(1);
  for (int i=0;i<VISIBLE_STOCKS;i++) {
    int idx = stockScrollOffset+i;
    if (idx>=NUM_STOCKS) break;
    display.setCursor(0,i*10);
    display.print(idx==stockIndex?"> ":"  ");
    display.println(stocks[idx]);
  }
  display.display();
}

void drawSettingsMenu() {
  const char* items[]={"WiFi Info","About","Pong","Back"};
  display.clearDisplay();
  display.setTextSize(1);
  for(int i=0;i<4;i++){
    display.setCursor(0,i*10);
    display.print(settingsIndex==i?"> ":"  ");
    display.println(items[i]);
  }
  display.display();
}

void drawWiFiInfo() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0); display.println("WiFi Info");
  display.drawLine(0,9,127,9,SSD1306_WHITE);
  display.setCursor(0,14); display.print("SSID: "); display.println(WiFi.SSID());
  display.setCursor(0,24); display.print("RSSI: "); display.print(WiFi.RSSI()); display.println(" dBm");
  display.setCursor(0,34); display.println("IP:");
  display.setCursor(0,44); display.println(WiFi.localIP());
  display.display();
}

void drawAbout() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0); display.println("About");
  display.drawLine(0,9,127,9,SSD1306_WHITE);
  display.setCursor(0,14); display.println("PocketStock");
  display.setCursor(0,24); display.println("ESP32-C3");
  display.setCursor(0,34); display.println("Finnhub API");
  display.setCursor(0,44); display.println("v1.0");
  display.display();
}

/* ================= STOCK VIEW ================= */

void fetchAndDrawStockView() {
  HTTPClient http;
  String url="https://finnhub.io/api/v1/quote?symbol="+stocks[stockIndex]+"&token="+apiKey;
  http.begin(url);
  if(http.GET()==200){
    StaticJsonDocument<256> doc;
    deserializeJson(doc,http.getString());
    addPrice(doc["c"]);
    display.clearDisplay();
    display.setCursor(0,0); display.print(stocks[stockIndex]);
    drawTimeAndMarket();
    display.setTextSize(2);
    display.setCursor(0,18); display.print("$"); display.print((float)doc["c"],2);
    display.setTextSize(1);
    display.setCursor(0,52); display.print("Chg "); display.print((float)doc["d"],2);
    drawSparkline();
    display.display();
  }
  http.end();
}

/* ================= SETUP ================= */

void setup() {
  pinMode(BTN_UP,INPUT_PULLUP);
  pinMode(BTN_DOWN,INPUT_PULLUP);
  pinMode(BTN_SELECT,INPUT_PULLUP);

  Wire.begin(SDA_PIN,SCL_PIN);
  display.begin(SSD1306_SWITCHCAPVCC,OLED_ADDR);
  display.setTextColor(SSD1306_WHITE);

  showBootScreen();

  display.clearDisplay();
  display.setCursor(0,28);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid);
  while(WiFi.status()!=WL_CONNECTED) delay(100);

  configTime(gmtOffset_sec,daylightOffset_sec,ntpServer);
  drawMainMenu();
}

/* ================= LOOP ================= */

void loop() {

  /* ----- LONG PRESS BACK ----- */
  bool sel = digitalRead(BTN_SELECT)==LOW;
  if (sel && !selectHeld) {
    selectHeld=true;
    selectPressTime=millis();
  }
  if (!sel && selectHeld) {
    selectHeld=false;
    if (millis()-selectPressTime>=LONG_PRESS_MS) {
      if (uiState==STOCK_VIEW) { uiState=STOCK_LIST; drawStockList(); }
      else if (uiState==STOCK_LIST) { uiState=MAIN_MENU; drawMainMenu(); }
      else if (uiState==SETTINGS_WIFI || uiState==SETTINGS_ABOUT || uiState==GAME_PONG) {
        uiState=SETTINGS_MENU; drawSettingsMenu();
      }
      return;
    }
  }

  /* ----- STOCK VIEW UPDATE ----- */
  if (uiState==STOCK_VIEW && millis()-lastFetch>fetchInterval) {
    lastFetch=millis();
    fetchAndDrawStockView();
  }

  /* ----- PONG ----- */
  if (uiState==GAME_PONG) {
    if (!digitalRead(BTN_UP)) paddleY-=2;
    if (!digitalRead(BTN_DOWN)) paddleY+=2;
    paddleY=constrain(paddleY,0,SCREEN_HEIGHT-16);
    updatePong();
    drawPong();
    delay(12);
    return;
  }

  /* ----- UP ----- */
  if (!digitalRead(BTN_UP)) {
    if (uiState==MAIN_MENU && mainMenuIndex>0) { mainMenuIndex--; drawMainMenu(); }
    else if (uiState==STOCK_LIST && stockIndex>0) {
      stockIndex--;
      if (stockIndex<stockScrollOffset) stockScrollOffset--;
      drawStockList();
    }
    else if (uiState==SETTINGS_MENU && settingsIndex>0) { settingsIndex--; drawSettingsMenu(); }
    delay(150);
  }

  /* ----- DOWN ----- */
  if (!digitalRead(BTN_DOWN)) {
    if (uiState==MAIN_MENU && mainMenuIndex<1) { mainMenuIndex++; drawMainMenu(); }
    else if (uiState==STOCK_LIST && stockIndex<NUM_STOCKS-1) {
      stockIndex++;
      if (stockIndex>=stockScrollOffset+VISIBLE_STOCKS) stockScrollOffset++;
      drawStockList();
    }
    else if (uiState==SETTINGS_MENU && settingsIndex<3) { settingsIndex++; drawSettingsMenu(); }
    delay(150);
  }

  /* ----- SHORT PRESS SELECT ----- */
  if (!sel && millis()-selectPressTime<LONG_PRESS_MS && selectPressTime!=0) {
    selectPressTime=0;

    if (uiState==MAIN_MENU) {
      if (mainMenuIndex==0) { uiState=STOCK_LIST; stockScrollOffset=0; drawStockList(); }
      else { uiState=SETTINGS_MENU; settingsIndex=0; drawSettingsMenu(); }
    }
    else if (uiState==STOCK_LIST) {
      uiState=STOCK_VIEW;
      resetHistory();
      lastFetch=0;
    }
    else if (uiState==SETTINGS_MENU) {
      if (settingsIndex==0) { uiState=SETTINGS_WIFI; drawWiFiInfo(); }
      else if (settingsIndex==1) { uiState=SETTINGS_ABOUT; drawAbout(); }
      else if (settingsIndex==2) { uiState=GAME_PONG; resetPong(); }
      else { uiState=MAIN_MENU; drawMainMenu(); }
    }
  }
}
