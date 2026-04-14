import asyncio
import wave
import struct
import time
import os
from bleak import BleakClient, BleakScanner

SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
CHAR_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"

# IMA ADPCM Decoder Variables
index_table = [-1, -1, -1, -1, 2, 4, 6, 8]
stepsize_table = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
]

valprev = 0
index = 0

def decode_nibble(nibble):
    global valprev, index
    step = stepsize_table[index]
    
    sign = nibble & 8
    delta = nibble & 7
    
    vpdiff = step >> 3
    if delta & 4: vpdiff += step
    if delta & 2: vpdiff += (step >> 1)
    if delta & 1: vpdiff += (step >> 2)
    
    if sign:
        valprev -= vpdiff
    else:
        valprev += vpdiff
        
    if valprev > 32767: valprev = 32767
    elif valprev < -32768: valprev = -32768
    
    index += index_table[delta]
    if index < 0: index = 0
    elif index > 88: index = 88
    
    return valprev

pcm_data = bytearray()
last_receive_time = 0
is_recording_active = False
file_counter = 1

def save_wav_file():
    global pcm_data, file_counter
    if len(pcm_data) > 0:
        filename = f"output_{file_counter}.wav"
        filepath = os.path.abspath(filename)
        print(f"\n[Save] Auto-saving {len(pcm_data)} bytes to: {filepath}")
        with wave.open(filename, "wb") as wav_file:
            wav_file.setnchannels(1)
            wav_file.setsampwidth(2) # 16-bit
            wav_file.setframerate(16000)
            wav_file.writeframes(pcm_data)
        print(f"Saved successfully. You can now pass this file to Whisper.")
        file_counter += 1
        pcm_data.clear() # Reset buffer for next recording
    else:
        print("\n[Save] No audio data to save.")

def notification_handler(sender, data):
    global pcm_data, last_receive_time, is_recording_active
    
    # Mark that we are currently receiving data
    last_receive_time = time.time()
    if not is_recording_active:
        is_recording_active = True
        print("\n--- Started receiving audio ---")
        
    # data is a bytearray of compressed ADPCM
    for byte in data:
        # High nibble
        nibble1 = (byte >> 4) & 0x0F
        # Low nibble
        nibble2 = byte & 0x0F
        
        sample1 = decode_nibble(nibble1)
        sample2 = decode_nibble(nibble2)
        
        # Pack as 16-bit little endian
        pcm_data.extend(struct.pack("<h", sample1))
        pcm_data.extend(struct.pack("<h", sample2))
        
    print(f"Receiving... Buffer size: {len(pcm_data)} bytes", end='\r')

async def main():
    global last_receive_time, is_recording_active
    
    print("Searching for Cardputer... (Scanning for 10 seconds)")
    devices = await BleakScanner.discover(timeout=10.0)
    
    if not devices:
        print("No BLE devices found at all! Please check:")
        print("1. Your Mac's Bluetooth is turned on.")
        print("2. Your Terminal or Trae IDE has Bluetooth permissions (System Settings -> Privacy & Security -> Bluetooth).")
        return

    target_address = None
    print(f"Found {len(devices)} devices. Looking for Cardputer...")
    for d in devices:
        if d.name:
            print(f" - Discovered: {d.name} ({d.address})")
            if "Cardputer" in d.name:
                target_address = d.address
                break
            
    if not target_address:
        print("\nDevice not found! Make sure M5Cardputer is powered on and advertising.")
        print("If it's on, try resetting the Cardputer (press the reset button).")
        return

    print(f"Connecting to {target_address}...")
    async with BleakClient(target_address) as client:
        print("Connected! Hold SPACE on Cardputer to talk.")
        print("(Audio will auto-save 1 second after you release SPACE. Press Ctrl+C to quit entirely)")
        await client.start_notify(CHAR_UUID, notification_handler)
        
        try:
            # Keep script running and monitor for silence (end of recording)
            while True:
                await asyncio.sleep(0.5)
                
                # If we were recording, but haven't received data for >1.0 seconds, consider it done
                if is_recording_active and (time.time() - last_receive_time > 1.0):
                    is_recording_active = False
                    print("\n--- Finished receiving audio ---")
                    save_wav_file()
                    print("\nReady for next recording! Hold SPACE to talk again.")
                    
        except KeyboardInterrupt:
            print("\nStopping script...")
            
        await client.stop_notify(CHAR_UUID)
        
        # Save any remaining data on exit
        if is_recording_active or len(pcm_data) > 0:
            save_wav_file()

if __name__ == "__main__":
    asyncio.run(main())
