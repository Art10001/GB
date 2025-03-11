import numpy as np
import sounddevice as sd
import scipy.io.wavfile as wav
import threading
from time import sleep
from pynput import keyboard

# Define musical note frequencies (Hz) for two channels
CHANNEL1_NOTES = {
    'a': 261.63,  # C4
    's': 293.66,  # D4
    'd': 329.63,  # E4
    'f': 349.23,  # F4
    'g': 392.00,  # G4
    'h': 440.00,  # A4
    'j': 493.88,  # B4
}

CHANNEL2_NOTES = {
    'z': 523.25,  # C5
    'x': 587.33,  # D5
    'c': 659.26,  # E5
    'v': 698.46,  # F5
    'b': 783.99,  # G5
    'n': 880.00,  # A5
    'm': 987.77,  # B5
}

# Audio configuration
SAMPLE_RATE = 44100
AMPLITUDE = 0.5
WAV_BUFFER = []
QUIT_FLAG = False

# Sound generation state
class ChannelState:
    def __init__(self):
        self.lock = threading.Lock()
        self.active = False
        self.frequency = 0.0
        self.phase = 0.0

CH1 = ChannelState()
CH2 = ChannelState()

def generate_square_wave(frequency, phase, n_samples):
    phase_increment = (frequency * 2 * np.pi) / SAMPLE_RATE
    phases = (phase + np.arange(n_samples) * phase_increment) % (2 * np.pi)
    wave = np.where(phases < np.pi, AMPLITUDE, -AMPLITUDE)
    new_phase = (phase + n_samples * phase_increment) % (2 * np.pi)
    return wave, new_phase

def audio_callback(outdata, frames, time, status):
    global CH1, CH2, WAV_BUFFER
    if status:
        print(status)
    
    # Generate silence by default
    output = np.zeros(frames, dtype=np.float32)
    
    # Process Channel 1
    with CH1.lock:
        if CH1.active and CH1.frequency > 0:
            wave, new_phase = generate_square_wave(CH1.frequency, CH1.phase, frames)
            output += wave
            CH1.phase = new_phase
    
    # Process Channel 2
    with CH2.lock:
        if CH2.active and CH2.frequency > 0:
            wave, new_phase = generate_square_wave(CH2.frequency, CH2.phase, frames)
            output += wave
            CH2.phase = new_phase
    
    # Prevent clipping and store in buffer
    output = np.clip(output, -1.0, 1.0)
    WAV_BUFFER.extend(output.tolist())
    
    # Send to sound device
    outdata[:] = output.reshape(-1, 1)

# Keyboard event handlers
def on_press(key):
    global QUIT_FLAG
    try:
        if hasattr(key, 'char'):
            key_char = key.char.lower()
            
            # Channel 1 note handling
            if key_char in CHANNEL1_NOTES:
                with CH1.lock:
                    CH1.active = True
                    CH1.frequency = CHANNEL1_NOTES[key_char]
                    
            # Channel 2 note handling
            elif key_char in CHANNEL2_NOTES:
                with CH2.lock:
                    CH2.active = True
                    CH2.frequency = CHANNEL2_NOTES[key_char]
            
            # Quit handling
            elif key_char == 'q':
                QUIT_FLAG = True
    except Exception as e:
        # Handle cases where key might not have expected attributes
        if key == keyboard.Key.esc:
            QUIT_FLAG = True

def on_release(key):
    try:
        if hasattr(key, 'char'):
            key_char = key.char.lower()
            
            # Check if it's a Channel 1 key
            if key_char in CHANNEL1_NOTES:
                with CH1.lock:
                    CH1.active = False
                    
            # Check if it's a Channel 2 key
            elif key_char in CHANNEL2_NOTES:
                with CH2.lock:
                    CH2.active = False
    except Exception:
        pass
    
    # Don't stop listener here - we'll handle that in the main loop
    return not QUIT_FLAG

def main():
    global QUIT_FLAG, WAV_BUFFER
    
    # Setup audio stream
    stream = sd.OutputStream(
        samplerate=SAMPLE_RATE,
        channels=1,
        callback=audio_callback,
        blocksize=1024
    )

    print("Game Boy Audio Simulator")
    print("Channel 1 keys: A-S-D-F-G-H-J")
    print("Channel 2 keys: Z-X-C-V-B-N-M")
    print("Press Q or ESC to quit...")

    # Start audio stream
    stream.start()

    # Start keyboard listener in a non-blocking way
    listener = keyboard.Listener(on_press=on_press, on_release=on_release)
    listener.start()

    # Main loop
    try:
        while not QUIT_FLAG:
            sleep(0.1)
    except KeyboardInterrupt:
        QUIT_FLAG = True
    finally:
        # Clean shutdown
        if listener.is_alive():
            listener.stop()
        stream.stop()
        stream.close()

        # Save to WAV file
        if WAV_BUFFER:
            audio_data = np.array(WAV_BUFFER, dtype=np.float32)
            audio_data = np.clip(audio_data, -1.0, 1.0)
            audio_data = (audio_data * 32767).astype(np.int16)
            wav.write('gameboy_audio.wav', SAMPLE_RATE, audio_data)
            print("Audio saved to gameboy_audio.wav")
        else:
            print("No audio data recorded")

if __name__ == "__main__":
    main()
