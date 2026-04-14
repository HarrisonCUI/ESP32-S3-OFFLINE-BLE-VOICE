# ESP32-S3 Offline BLE Voice
*(Project specifically adapted for M5Stack Cardputer-Adv)*

A high-efficiency, low-latency offline voice streaming solution utilizing BLE (Bluetooth Low Energy) and ADPCM compression. It captures audio on an ESP32-S3 device (M5Stack Cardputer), compresses it using 4-bit IMA ADPCM, and streams it to a PC receiver script over BLE GATT notifications. The PC script decodes the stream and automatically saves it to `.wav` files, ready to be processed by speech recognition models like Whisper.

## Features
- **Ultra-low Latency & Low Power:** Uses BLE instead of Classic Bluetooth or WiFi.
- **Efficient Bandwidth Usage:** 4:1 compression ratio using IMA ADPCM (16-bit PCM -> 4-bit).
- **M5Cardputer Integration:** Uses the built-in microphone, TCA8418 keyboard chip, and IPS display.
- **Real-time Waveform Display:** Beautiful dynamic audio waveform visualization on the device screen.
- **Remote Commands:** Press 'X' on the Cardputer to remotely open the Terminal app on your Mac!
- **Local Whisper Transcription:** Automatically runs OpenAI's Whisper model completely offline to transcribe your voice into text accurately.

## Project Structure
- `src/main.cpp` - PlatformIO Arduino firmware for ESP32-S3 (M5Cardputer).
- `platformio.ini` - PlatformIO configuration file.
- `receiver/` - PC-side Python scripts and dependencies.

## Usage

### 1. Firmware Setup (Device)
Install PlatformIO and flash the firmware:
```bash
pio run -t upload
```

### 2. PC Receiver Setup
Ensure you have `ffmpeg` installed on your Mac, as Whisper requires it to process audio:
```bash
brew install ffmpeg
```

Navigate to the receiver directory, install dependencies, and run the script:
```bash
cd receiver
pip install -r requirements.txt
python receiver.py
```

### 3. Interaction
1. Once the Python script is running and connected, the Cardputer screen will say "Connected".
2. **Press 'X'** on the Cardputer keyboard to automatically open the Terminal app on your Mac.
3. **Hold the SPACE bar** on the Cardputer to start recording and transmitting audio. The screen will display a live waveform.
4. Release the SPACE bar to stop.
5. The Python script will automatically detect the pause, transcribe the audio using local Whisper, and print the recognized text to the terminal!