import numpy as np
import sounddevice as sd
import scipy.io.wavfile as wav
import keyboard
import threading
from time import sleep

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
    np.clip(output, -1.0, 1.0, out=output)
    WAV_BUFFER.extend(output.tolist())
    
    # Send to sound device
    outdata[:] = output.reshape(-1, 1)

def handle_key_event(channel, notes_dict, event):
    if event.event_type == 'down':
        freq = notes_dict.get(event.name, None)
        if freq is not None:
            with channel.lock:
                channel.active = True
                channel.frequency = freq
    elif event.event_type == 'up':
        if event.name in notes_dict:
            with channel.lock:
                channel.active = False

# Setup keyboard listeners
keyboard.hook(lambda e: handle_key_event(CH1, CHANNEL1_NOTES, e), suppress=False)
keyboard.hook(lambda e: handle_key_event(CH2, CHANNEL2_NOTES, e), suppress=False)

# Start audio stream
stream = sd.OutputStream(
    samplerate=SAMPLE_RATE,
    channels=1,
    callback=audio_callback,
    blocksize=1024
)
stream.start()

print("Game Boy Audio Simulator")
print("Channel 1 keys: A-S-D-F-G-H-J")
print("Channel 2 keys: Z-X-C-V-B-N-M")
print("Hold Q to quit...")

# Main loop
while not QUIT_FLAG:
    if keyboard.is_pressed('q'):
        QUIT_FLAG = True
    sleep(0.01)

# Cleanup
stream.stop()
stream.close()
keyboard.unhook_all()

# Save to WAV file
if WAV_BUFFER:
    audio_data = np.array(WAV_BUFFER, dtype=np.float32)
    audio_data = np.clip(audio_data, -1.0, 1.0)
    audio_data = (audio_data * 32767).astype(np.int16)
    wav.write('gameboy_audio.wav', SAMPLE_RATE, audio_data)
    print("Audio saved to gameboy_audio.wav")
else:
    print("No audio data recorded")
