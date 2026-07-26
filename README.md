# R6Config

## What is it?
R6Config is a simple tool for importing/exporting configs.

It's made so sharing configs is easier across devices, accounts and different people.

## How does it work?
It works by exporting the selected profile's GameSettings.ini file, compressing it, encoding it and outputting it as a readable string that you can copy and share.

## Why?
This app was made because of the lack of config shareability in R6 compared to other titles like CS2.

## Dependencies
This project uses [WebView](https://github.com/webview/webview), [minIni](https://github.com/compuphase/minIni) and [zlib](https://github.com/madler/zlib).

## Building
To build, just run:
```bash
cmake -G "Visual Studio 18 2026" -B build -S .
cmake --build build --config Release
```
And the output will be in `/build/bin/Release/R6Config.exe`

If you don't want to build, you have the latest builds below.

## Downloads
You can get the latest version [here](https://github.com/NotStrahinja/R6Config/releases/latest).

## Screenshots

<img width="432" height="305" alt="image" src="https://github.com/user-attachments/assets/d6d40f4e-422c-45b2-ad2d-402dd9d75a50" />
