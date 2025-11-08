# ESP32-Flipper
ESP32 Flipper is a touchscreen-driven toolkit for wireless security testing on ESP32. It features fast, multi-channel WiFi deauthentication attacks, WiFi and BLE scanning, device spoofing, and SD card logging—all controlled from a user-friendly menu interface. Ideal for learning and authorized penetration testing.

Overview
This project is an wireless attack and scanning suite for the ESP32 platform, featuring a touchscreen UI for seamless control. It is designed for penetration testers, wireless researchers, and enthusiasts who require high-speed deauthentication, multi-channel packet injection, BLE operations, and interactive logging—all integrated with SD card support for persistent scanning logs.This project is inspired from Flipper Zero device and is built purely for learning and educational purpose.

Features:

    1. Multi-Channel Burst WiFi Deauth/Disassociation
        1. Sends high-rate deauth/disassoc packets across all major WiFi channels (2.4 GHz, channels 1–13) for maximum disruption (2000+ packets/sec).
        2. Multi-channel hopping and true ESP32 packet injection using esp_wifi_80211_tx.
        
    2. WiFi Network Scanner
        1. Asynchronous scanning and listing of all visible WiFi networks.
        2. Results displayed in a scrollable, intuitive menu.
        
    3. Bluetooth Low Energy (BLE) Tools
        1. BLE device scanner: Finds and logs all advertising BLE devices nearby.
        2. BLE advertiser: Toggles spoofed BLE advertisements.
        3. BLE beaconing: Emits custom BLE beacons for tracking and protocol fuzzing.
        
    4. Intuitive Touchscreen UI
        1. Designed for 2.8" 320x240 displays.
        2. Multi-level menu with smooth navigation using tap, scroll, and selection highlights.
        
    5. SD Card Logging
        1. All scan results saved in /scanlog.txt.
        2. Persistent tracking of discovered devices for later analysis.
        
    6. Safety and Responsiveness
        1. Any touch event will instantly stop a running deauth attack.
        2. UI and attack logic are non-blocking and highly responsive.
        
Hardware Requirements:

    1. ESP32 Dev Board
    2. 2.8" TFT ILI9341 Touch Screen Display
    3. Mini Bread Board
    4. SD Card 
    5. Header Pins
    6. Connecting wires
