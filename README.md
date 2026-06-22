# KeyMorph
> Universal Keyboard Remapper for Windows - Made by VryxTech

KeyMorph is an ultra-lightweight, high-performance, and open-source keyboard remapper for Windows. It allows you to easily change key functions at a low-level without bloating your system.

## ✨ Features
* **Windows Only:** Designed specifically for Windows OS using native low-level hooks.
* **Ultra-Lightweight:** Only **300 KB** executable size.
* **Extremely Low Memory Footprint:** Consumes only **1.9 MB of RAM**.
* **Zero CPU Usage:** Efficient hooks ensure 0% CPU usage while idle.
* **No Admin Privileges Required:** Runs perfectly without Administrator rights or system restarts.
* **Deep System Remapping:** Supports modifier keys like **Ctrl**, Shift, and Alt.

## 🎮 Gaming & App Compatibility (Scan Code Support)
Unlike traditional remappers that only change virtual key codes (VK_CODE), KeyMorph intercepts and remaps keys at the **Scan Code** level. 

Why does this matter?
* **Full Game Compatibility:** Works flawlessly in competitive games, DirectX/OpenGL titles, and anti-cheat environments that read raw hardware inputs.
* **No Input Lag:** Processing inputs at a low level ensures zero added latency, making it perfect for fast-paced gaming.
* **Software Independent:** Works globally across all apps, emulators, and IDEs without needing specific profiles.

## 🚀 How to Compile
This project is built using MinGW (GCC) on Windows. Open your terminal/command prompt in the project directory and run:

`g++ -o KeyMorph.exe KeyMorph.cpp -O2 -s -Wl,--dynamicbase -Wl,--nxcompat -static -lgdi32 -luser32 -lshell32 -ldwmapi -mwindows`
