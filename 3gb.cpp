#include <iostream>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
#include <SDL2/SDL.h>
#include <portaudio.h>

const int SAMPLE_RATE = 44100;
const float AMPLITUDE = 0.5f;

// Note frequencies for two channels
std::unordered_map<SDL_Keycode, float> CHANNEL1_NOTES = {
    {SDLK_a, 261.63f},  // C4
    {SDLK_s, 293.66f},  // D4
    {SDLK_d, 329.63f},  // E4
    {SDLK_f, 349.23f},  // F4
    {SDLK_g, 392.00f},  // G4
    {SDLK_h, 440.00f},  // A4
    {SDLK_j, 493.88f}   // B4
};

std::unordered_map<SDL_Keycode, float> CHANNEL2_NOTES = {
    {SDLK_z, 523.25f},  // C5
    {SDLK_x, 587.33f},  // D5
    {SDLK_c, 659.26f},  // E5
    {SDLK_v, 698.46f},  // F5
    {SDLK_b, 783.99f},  // G5
    {SDLK_n, 880.00f},  // A5
    {SDLK_m, 987.77f}   // B5
};

// Sound generation state
class ChannelState {
public:
    std::mutex lock;
    bool active = false;
    float frequency = 0.0f;
    float phase = 0.0f;
};

ChannelState CH1;
ChannelState CH2;
std::vector<float> WAV_BUFFER;
std::atomic<bool> QUIT_FLAG(false);

// WAV file helpers
struct WavHeader {
    // RIFF Header
    char riff_header[4] = {'R', 'I', 'F', 'F'};
    uint32_t wav_size = 0;    // File size - 8
    char wave_header[4] = {'W', 'A', 'V', 'E'};
    
    // Format Header
    char fmt_header[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_chunk_size = 16;
    uint16_t audio_format = 1;  // PCM
    uint16_t num_channels = 1;  // Mono
    uint32_t sample_rate = SAMPLE_RATE;
    uint32_t byte_rate = SAMPLE_RATE * 2;  // SampleRate * NumChannels * BitsPerSample/8
    uint16_t block_align = 2;              // NumChannels * BitsPerSample/8
    uint16_t bits_per_sample = 16;         // 16 bits
    
    // Data Header
    char data_header[4] = {'d', 'a', 't', 'a'};
    uint32_t data_bytes = 0;               // NumSamples * NumChannels * BitsPerSample/8
};

void saveWav(const std::string& filename, const std::vector<float>& buffer) {
    std::vector<int16_t> pcm_data(buffer.size());
    
    // Convert float samples to 16-bit PCM
    for (size_t i = 0; i < buffer.size(); ++i) {
        float clipped = std::max(-1.0f, std::min(1.0f, buffer[i]));
        pcm_data[i] = static_cast<int16_t>(clipped * 32767.0f);
    }
    
    // Create header
    WavHeader header;
    header.data_bytes = pcm_data.size() * sizeof(int16_t);
    header.wav_size = header.data_bytes + sizeof(WavHeader) - 8;
    
    // Write to file
    std::ofstream outfile(filename, std::ios::binary);
    if (outfile.is_open()) {
        outfile.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
        outfile.write(reinterpret_cast<const char*>(pcm_data.data()), header.data_bytes);
        outfile.close();
        std::cout << "Audio saved to " << filename << std::endl;
    } else {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
    }
}

// Audio callback function for PortAudio
static int audioCallback(const void* inputBuffer, void* outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo* timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void* userData) {
    (void) inputBuffer; // Unused parameter
    (void) timeInfo;    // Unused parameter
    (void) statusFlags; // Unused parameter
    (void) userData;    // Unused parameter
    
    float* out = (float*)outputBuffer;
    
    // Generate silence by default
    for (unsigned int i = 0; i < framesPerBuffer; i++) {
        out[i] = 0.0f;
    }
    
    // Process Channel 1
    {
        std::lock_guard<std::mutex> lock(CH1.lock);
        if (CH1.active && CH1.frequency > 0) {
            for (unsigned int i = 0; i < framesPerBuffer; i++) {
                CH1.phase += 2.0f * M_PI * CH1.frequency / SAMPLE_RATE;
                if (CH1.phase > 2.0f * M_PI) {
                    CH1.phase -= 2.0f * M_PI;
                }
                out[i] += (CH1.phase < M_PI) ? AMPLITUDE : -AMPLITUDE;
            }
        }
    }
    
    // Process Channel 2
    {
        std::lock_guard<std::mutex> lock(CH2.lock);
        if (CH2.active && CH2.frequency > 0) {
            for (unsigned int i = 0; i < framesPerBuffer; i++) {
                CH2.phase += 2.0f * M_PI * CH2.frequency / SAMPLE_RATE;
                if (CH2.phase > 2.0f * M_PI) {
                    CH2.phase -= 2.0f * M_PI;
                }
                out[i] += (CH2.phase < M_PI) ? AMPLITUDE : -AMPLITUDE;
            }
        }
    }
    
    // Clip and add to WAV buffer
    for (unsigned int i = 0; i < framesPerBuffer; i++) {
        out[i] = std::max(-1.0f, std::min(1.0f, out[i]));
        WAV_BUFFER.push_back(out[i]);
    }
    
    return paContinue;
}

int main() {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    
    // Create a small window for key events
    SDL_Window* window = SDL_CreateWindow(
        "Game Boy Audio Simulator", 
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
        400, 200, 
        SDL_WINDOW_SHOWN
    );
    
    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }
    
    // Initialize PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // Open audio stream
    PaStream* stream;
    err = Pa_OpenDefaultStream(
        &stream,
        0,                 // No input channels
        1,                 // 1 output channel
        paFloat32,         // 32-bit floating point output
        SAMPLE_RATE,       // Sample rate
        256,               // Frames per buffer
        audioCallback,     // Callback function
        nullptr            // No user data
    );
    
    if (err != paNoError) {
        std::cerr << "Error opening PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // Start audio stream
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "Error starting PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream);
        Pa_Terminate();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    std::cout << "Game Boy Audio Simulator" << std::endl;
    std::cout << "Channel 1 keys: A-S-D-F-G-H-J" << std::endl;
    std::cout << "Channel 2 keys: Z-X-C-V-B-N-M" << std::endl;
    std::cout << "Press Q or ESC to quit..." << std::endl;
    
    // Main event loop
    SDL_Event event;
    while (!QUIT_FLAG) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                QUIT_FLAG = true;
            } else if (event.type == SDL_KEYDOWN) {
                SDL_Keycode keycode = event.key.keysym.sym;
                
                // Check for quit keys
                if (keycode == SDLK_q || keycode == SDLK_ESCAPE) {
                    QUIT_FLAG = true;
                }
                
                // Channel 1 note handling
                auto it1 = CHANNEL1_NOTES.find(keycode);
                if (it1 != CHANNEL1_NOTES.end()) {
                    std::lock_guard<std::mutex> lock(CH1.lock);
                    CH1.active = true;
                    CH1.frequency = it1->second;
                }
                
                // Channel 2 note handling
                auto it2 = CHANNEL2_NOTES.find(keycode);
                if (it2 != CHANNEL2_NOTES.end()) {
                    std::lock_guard<std::mutex> lock(CH2.lock);
                    CH2.active = true;
                    CH2.frequency = it2->second;
                }
            } else if (event.type == SDL_KEYUP) {
                SDL_Keycode keycode = event.key.keysym.sym;
                
                // Channel 1 note handling
                auto it1 = CHANNEL1_NOTES.find(keycode);
                if (it1 != CHANNEL1_NOTES.end()) {
                    std::lock_guard<std::mutex> lock(CH1.lock);
                    CH1.active = false;
                }
                
                // Channel 2 note handling
                auto it2 = CHANNEL2_NOTES.find(keycode);
                if (it2 != CHANNEL2_NOTES.end()) {
                    std::lock_guard<std::mutex> lock(CH2.lock);
                    CH2.active = false;
                }
            }
        }
        
        // Small delay to prevent high CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Cleanup
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    // Save to WAV file
    if (!WAV_BUFFER.empty()) {
        saveWav("gameboy_audio.wav", WAV_BUFFER);
    } else {
        std::cout << "No audio data recorded" << std::endl;
    }
    
    return 0;
}
