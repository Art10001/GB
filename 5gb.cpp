#include <iostream>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
#include <deque>
#include <algorithm>
#include <SDL2/SDL.h>
#include <portaudio.h>

const int SAMPLE_RATE = 44100;
const float AMPLITUDE = 0.5f;

// Window dimensions
const int WINDOW_WIDTH = 1000;
const int WINDOW_HEIGHT = 600;

// Piano key dimensions
const int WHITE_KEY_WIDTH = 40;
const int WHITE_KEY_HEIGHT = 150;
const int BLACK_KEY_WIDTH = 24;
const int BLACK_KEY_HEIGHT = 100;

// Staff dimensions
const int STAFF_X = 50;
const int STAFF_Y = 250;
const int STAFF_WIDTH = 900;
const int STAFF_HEIGHT = 200;
const int LINE_SPACING = 12;  // Reduced from 20 to make staff lines closer
const int NOTE_RADIUS = 8;    // Slightly smaller notes to fit the tighter spacing

// Note frequencies for three channels
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

// Wave channel notes (one octave lower than channel 1)
std::unordered_map<SDL_Keycode, float> CHANNEL3_NOTES = {
    {SDLK_1, 130.81f},  // C3
    {SDLK_2, 146.83f},  // D3
    {SDLK_3, 164.81f},  // E3
    {SDLK_4, 174.61f},  // F3
    {SDLK_5, 196.00f},  // G3
    {SDLK_6, 220.00f},  // A3
    {SDLK_7, 246.94f}   // B3
};

// Map note frequencies to staff positions
std::unordered_map<float, int> NOTE_POSITIONS = {
    {261.63f, 10},  // C4
    {293.66f, 9},   // D4
    {329.63f, 8},   // E4
    {349.23f, 7},   // F4
    {392.00f, 6},   // G4
    {440.00f, 5},   // A4
    {493.88f, 4},   // B4
    {523.25f, 3},   // C5
    {587.33f, 2},   // D5
    {659.26f, 1},   // E5
    {698.46f, 0},   // F5
    {783.99f, -1},  // G5
    {880.00f, -2},  // A5
    {987.77f, -3}   // B5
};

// Piano key information
struct PianoKey {
    SDL_Rect rect;
    SDL_Color color;
    SDL_Color activeColor;
    bool isBlack;
    SDL_Keycode keycode;
    int channel;  // 1, 2, or 3
    bool isActive;
    float frequency;
};

// Note types
enum NoteType {
    EIGHTH_NOTE,  // Default, shorter note
    QUARTER_NOTE  // Longer note
};

// Note on the staff
struct StaffNote {
    float frequency;
    int position;  // Relative to middle line
    int x;
    int channel;   // 1, 2, or 3
    bool isPlaying;
    NoteType type; // Eighth or quarter note
};

// Sound generation state
class ChannelState {
public:
    std::mutex lock;
    bool active = false;
    float frequency = 0.0f;
    float phase = 0.0f;
};

// Wave channel has a different waveform
class WaveChannelState : public ChannelState {
public:
    // Wave pattern (16 samples of 4-bit each)
    std::vector<float> waveform = {
        0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 0.8f, 0.6f,
        0.0f, -0.2f, -0.4f, -0.6f, -0.8f, -1.0f, -0.8f, -0.6f
    };
    size_t wavePos = 0;
};

ChannelState CH1;
ChannelState CH2;
WaveChannelState CH3;
std::vector<float> WAV_BUFFER;
std::atomic<bool> QUIT_FLAG(false);
std::vector<PianoKey> pianoKeys;
std::vector<StaffNote> staffNotes;
bool isPlacingNote = false;
int currentChannel = 1;
float currentFrequency = 0.0f;
NoteType currentNoteType = EIGHTH_NOTE;
int scrollOffset = 0;
bool isPlayingSequence = false;
int playbackPosition = 0;
std::deque<StaffNote> playbackQueue;

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

// Initialize piano keys
void initPianoKeys() {
    // Channel 1 - White keys (lower octave)
    int x = 50;
    
    // C4 (A key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},  // White
        {200, 200, 255, 255},  // Light blue when active
        false,
        SDLK_a,
        1,
        false,
        261.63f
    });
    x += WHITE_KEY_WIDTH;
    
    // D4 (S key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {200, 200, 255, 255},
        false,
        SDLK_s,
        1,
        false,
        293.66f
    });
    x += WHITE_KEY_WIDTH;
    
    // E4 (D key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {200, 200, 255, 255},
        false,
        SDLK_d,
        1,
        false,
        329.63f
    });
    x += WHITE_KEY_WIDTH;
    
    // F4 (F key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {200, 200, 255, 255},
        false,
        SDLK_f,
        1,
        false,
        349.23f
    });
    x += WHITE_KEY_WIDTH;
    
    // G4 (G key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {200, 200, 255, 255},
        false,
        SDLK_g,
        1,
        false,
        392.00f
    });
    x += WHITE_KEY_WIDTH;
    
    // A4 (H key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {200, 200, 255, 255},
        false,
        SDLK_h,
        1,
        false,
        440.00f
    });
    x += WHITE_KEY_WIDTH;
    
    // B4 (J key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {200, 200, 255, 255},
        false,
        SDLK_j,
        1,
        false,
        493.88f
    });
    x += WHITE_KEY_WIDTH;
    
    // Channel 2 - White keys (higher octave)
    // C5 (Z key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {255, 200, 200, 255},  // Light red when active
        false,
        SDLK_z,
        2,
        false,
        523.25f
    });
    x += WHITE_KEY_WIDTH;
    
    // D5 (X key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {255, 200, 200, 255},
        false,
        SDLK_x,
        2,
        false,
        587.33f
    });
    x += WHITE_KEY_WIDTH;
    
    // E5 (C key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {255, 200, 200, 255},
        false,
        SDLK_c,
        2,
        false,
        659.26f
    });
    x += WHITE_KEY_WIDTH;
    
    // F5 (V key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {255, 200, 200, 255},
        false,
        SDLK_v,
        2,
        false,
        698.46f
    });
    x += WHITE_KEY_WIDTH;
    
    // G5 (B key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {255, 200, 200, 255},
        false,
        SDLK_b,
        2,
        false,
        783.99f
    });
    x += WHITE_KEY_WIDTH;
    
    // A5 (N key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {255, 200, 200, 255},
        false,
        SDLK_n,
        2,
        false,
        880.00f
    });
    x += WHITE_KEY_WIDTH;
    
    // B5 (M key)
    pianoKeys.push_back({
        {x, 50, WHITE_KEY_WIDTH, WHITE_KEY_HEIGHT},
        {255, 255, 255, 255},
        {255, 200, 200, 255},
        false,
        SDLK_m,
        2,
        false,
        987.77f
    });
    
    // Add black keys (these are just for visual representation)
    // We'll position them between the white keys
    
    // Channel 1 black keys
    int blackX = 50 + WHITE_KEY_WIDTH - BLACK_KEY_WIDTH/2;
    
    // C#4/Db4 (between A and S)
    pianoKeys.push_back({
        {blackX, 50, BLACK_KEY_WIDTH, BLACK_KEY_HEIGHT},
        {40, 40, 40, 255},
        {100, 100, 150, 255},
        true,
        SDLK_UNKNOWN,  // No key assigned
        1,
        false,
        0.0f
    });
    blackX += WHITE_KEY_WIDTH;
    
    // D#4/Eb4 (between S and D)
    pianoKeys.push_back({
        {blackX, 50, BLACK_KEY_WIDTH, BLACK_KEY_HEIGHT},
        {40, 40, 40, 255},
        {100, 100, 150, 255},
        true,
        SDLK_UNKNOWN,
        1,
        false,
        0.0f
    });
    blackX += WHITE_KEY_WIDTH * 2;  // Skip one white key (E to F has no black key)
    
    // F#4/Gb4 (between F and G)
    pianoKeys.push_back({
        {blackX, 50, BLACK_KEY_WIDTH, BLACK_KEY_HEIGHT},
        {40, 40, 40, 255},
        {100, 100, 150, 255},
        true,
        SDLK_UNKNOWN,
        1,
        false,
        0.0f
    });
    blackX += WHITE_KEY_WIDTH;
    
    // G#4/Ab4 (between G and H)
    pianoKeys.push_back({
        {blackX, 50, BLACK_KEY_WIDTH, BLACK_KEY_HEIGHT},
        {40, 40, 40, 255},
        {100, 100, 150, 255},
        true,
        SDLK_UNKNOWN,
        1,
        false,
        0.0f
    });
    blackX += WHITE_KEY_WIDTH;
    
    // A#4/Bb4 (between H and J)
    pianoKeys.push_back({
        {blackX, 50, BLACK_KEY_WIDTH, BLACK_KEY_HEIGHT},
        {40, 40, 40, 255},
        {100, 100, 150, 255},
        true,
        SDLK_UNKNOWN,
        1,
        false,
        0.0f
    });
    
    // Channel 2 black keys
    blackX += WHITE_KEY_WIDTH * 2;  // Skip to next octave
    
    // C#5/Db5 (between Z and X)
    pianoKeys.push_back({
        {blackX, 50, BLACK_KEY_WIDTH, BLACK_KEY_HEIGHT},
        {40, 40, 40, 255},
        {150, 100, 100, 255},
        true,
        SDLK_UNKNOWN,
        2,
        false,
        0.0f
    });
    blackX += WHITE_KEY_WIDTH;
    
    // D#5/Eb5 (between X and C)
    pianoKeys.push_back({
        {blackX, 50, BLACK_KEY_WIDTH, BLACK_KEY_HEIGHT},
        {40, 40, 40, 255},
        {150, 100, 100, 255},
        true,
        SDLK_UNKNOWN,
        2,
        false,
        0.0f
    });
    blackX += WHITE_KEY_WIDTH * 2;  // Skip one white key (E to F has no black key)
    
    // F#5/Gb5 (between V and B)
    pianoKeys.push_back({
        {blackX, 50, BLACK_KEY_WIDTH, BLACK_KEY_HEIGHT},
        {40, 40, 40, 255},
        {150, 100, 100, 255},
        true,
        SDLK_UNKNOWN,
        2,
        false,
        0.0f
    });
    blackX += WHITE_KEY_WIDTH;
    
    // G#5/Ab5 (between B and N)
    pianoKeys.push_back({
        {blackX, 50, BLACK_KEY_WIDTH, BLACK_KEY_HEIGHT},
        {40, 40, 40, 255},
        {150, 100, 100, 255},
        true,
        SDLK_UNKNOWN,
        2,
        false,
        0.0f
    });
    blackX += WHITE_KEY_WIDTH;
    
    // A#5/Bb5 (between N and M)
    pianoKeys.push_back({
        {blackX, 50, BLACK_KEY_WIDTH, BLACK_KEY_HEIGHT},
        {40, 40, 40, 255},
        {150, 100, 100, 255},
        true,
        SDLK_UNKNOWN,
        2,
        false,
        0.0f
    });
}

// Update piano key state based on keyboard input
void updatePianoKeyState(SDL_Keycode keycode, bool isPressed) {
    for (auto& key : pianoKeys) {
        if (key.keycode == keycode) {
            key.isActive = isPressed;
            if (isPressed && !key.isBlack) {
                currentFrequency = key.frequency;
                currentChannel = key.channel;
                isPlacingNote = true;
            }
            break;
        }
    }
}

// Add a note to the staff
void addNoteToStaff(int x, float frequency, int channel) {
    if (frequency <= 0) return;
    
    auto it = NOTE_POSITIONS.find(frequency);
    if (it != NOTE_POSITIONS.end()) {
        int position = it->second;
        staffNotes.push_back({frequency, position, x + scrollOffset, channel, false, currentNoteType});
    }
}

// Remove a note from the staff
void removeNoteFromStaff(int x, int y) {
    int clickX = x + scrollOffset;
    int staffCenterY = STAFF_Y + STAFF_HEIGHT / 2;
    
    for (auto it = staffNotes.begin(); it != staffNotes.end(); ++it) {
        int noteY = staffCenterY - it->position * LINE_SPACING / 2;
        if (abs(it->x - clickX) < NOTE_RADIUS * 2 && abs(y - noteY) < NOTE_RADIUS * 2) {
            staffNotes.erase(it);
            break;
        }
    }
}

// Start playback of the staff notes
void startPlayback() {
    if (staffNotes.empty()) return;
    
    isPlayingSequence = true;
    playbackPosition = scrollOffset;
    
    // Sort notes by x position for playback
    std::sort(staffNotes.begin(), staffNotes.end(), 
              [](const StaffNote& a, const StaffNote& b) { return a.x < b.x; });
    
    // Reset all notes to not playing
    for (auto& note : staffNotes) {
        note.isPlaying = false;
    }
    
    // Clear the playback queue
    playbackQueue.clear();
}

// Update playback state
void updatePlayback() {
    if (!isPlayingSequence) return;
    
    // Check if we've reached the end of the staff
    if (playbackPosition > STAFF_WIDTH + scrollOffset) {
        isPlayingSequence = false;
        
        // Stop all sound
        {
            std::lock_guard<std::mutex> lock1(CH1.lock);
            CH1.active = false;
        }
        {
            std::lock_guard<std::mutex> lock2(CH2.lock);
            CH2.active = false;
        }
        {
            std::lock_guard<std::mutex> lock3(CH3.lock);
            CH3.active = false;
        }
        return;
    }
    
    // Check for notes at the current position
    for (auto& note : staffNotes) {
        if (abs(note.x - playbackPosition) < 5 && !note.isPlaying) {
            // Add to playback queue
            playbackQueue.push_back(note);
            note.isPlaying = true;
        }
    }
    
    // Process playback queue
    if (!playbackQueue.empty()) {
        StaffNote& note = playbackQueue.front();
        
        // Play the note
        if (note.channel == 1) {
            std::lock_guard<std::mutex> lock(CH1.lock);
            CH1.active = true;
            CH1.frequency = note.frequency;
        } else if (note.channel == 2) {
            std::lock_guard<std::mutex> lock(CH2.lock);
            CH2.active = true;
            CH2.frequency = note.frequency;
        } else if (note.channel == 3) {
            std::lock_guard<std::mutex> lock(CH3.lock);
            CH3.active = true;
            CH3.frequency = note.frequency;
        }
        
        // For quarter notes, we'll keep them in the queue longer
        if (note.type == EIGHTH_NOTE || playbackQueue.size() > 4) {
            playbackQueue.pop_front();
        } else {
            // Move to the back of the queue to extend play time for quarter notes
            StaffNote quarterNote = playbackQueue.front();
            playbackQueue.pop_front();
            playbackQueue.push_back(quarterNote);
        }
    }
    
    // Advance playback position
    playbackPosition += 2;
}

// Render the piano keyboard
void renderPiano(SDL_Renderer* renderer) {
    // First draw all white keys
    for (const auto& key : pianoKeys) {
        if (!key.isBlack) {
            SDL_SetRenderDrawColor(
                renderer,
                key.isActive ? key.activeColor.r : key.color.r,
                key.isActive ? key.activeColor.g : key.color.g,
                key.isActive ? key.activeColor.b : key.color.b,
                key.isActive ? key.activeColor.a : key.color.a
            );
            SDL_RenderFillRect(renderer, &key.rect);
            
            // Draw border
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderDrawRect(renderer, &key.rect);
        }
    }
    
    // Then draw all black keys (so they appear on top)
    for (const auto& key : pianoKeys) {
        if (key.isBlack) {
            SDL_SetRenderDrawColor(
                renderer,
                key.isActive ? key.activeColor.r : key.color.r,
                key.isActive ? key.activeColor.g : key.color.g,
                key.isActive ? key.activeColor.b : key.color.b,
                key.isActive ? key.activeColor.a : key.color.a
            );
            SDL_RenderFillRect(renderer, &key.rect);
        }
    }
}

// Render the musical staff
void renderStaff(SDL_Renderer* renderer) {
    // Draw staff background
    SDL_Rect staffRect = {STAFF_X, STAFF_Y, STAFF_WIDTH, STAFF_HEIGHT};
    SDL_SetRenderDrawColor(renderer, 255, 255, 240, 255);
    SDL_RenderFillRect(renderer, &staffRect);
    
    // Draw staff lines
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    int centerY = STAFF_Y + STAFF_HEIGHT / 2;
    
    // Draw 5 lines above and below center (more lines for better chord visibility)
    for (int i = -6; i <= 6; i += 2) {
        int y = centerY + i * LINE_SPACING / 2;
        SDL_RenderDrawLine(renderer, STAFF_X, y, STAFF_X + STAFF_WIDTH, y);
    }
    
    // Draw playback position line if playing
    if (isPlayingSequence) {
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        int x = STAFF_X + (playbackPosition - scrollOffset);
        SDL_RenderDrawLine(renderer, x, STAFF_Y, x, STAFF_Y + STAFF_HEIGHT);
    }
    
    // Draw notes
    for (const auto& note : staffNotes) {
        int x = STAFF_X + (note.x - scrollOffset);
        int y = centerY - note.position * LINE_SPACING / 2;
        
        // Skip notes outside the visible area
        if (x < STAFF_X - NOTE_RADIUS || x > STAFF_X + STAFF_WIDTH + NOTE_RADIUS) {
            continue;
        }
        
        // Draw note circle with color based on channel
        if (note.channel == 1) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);  // Blue for pulse 1
        } else if (note.channel == 2) {
            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);  // Red for pulse 2
        } else {
            SDL_SetRenderDrawColor(renderer, 0, 180, 0, 255);  // Green for wave
        }
        
        // Draw filled circle for the note
        for (int dy = -NOTE_RADIUS; dy <= NOTE_RADIUS; dy++) {
            for (int dx = -NOTE_RADIUS; dx <= NOTE_RADIUS; dx++) {
                if (dx*dx + dy*dy <= NOTE_RADIUS*NOTE_RADIUS) {
                    SDL_RenderDrawPoint(renderer, x + dx, y + dy);
                }
            }
        }
        
        // Draw stem
        if (note.position >= 0) {
            // Stem down for higher notes
            SDL_RenderDrawLine(renderer, x + NOTE_RADIUS, y, x + NOTE_RADIUS, y + 30);
        } else {
            // Stem up for lower notes
            SDL_RenderDrawLine(renderer, x - NOTE_RADIUS, y, x - NOTE_RADIUS, y - 30);
        }
    
        // Draw note head based on type
        if (note.type == QUARTER_NOTE) {
            // For quarter notes, draw an empty circle
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            for (int dy = -NOTE_RADIUS+2; dy <= NOTE_RADIUS-2; dy++) {
                for (int dx = -NOTE_RADIUS+2; dx <= NOTE_RADIUS-2; dx++) {
                    if (dx*dx + dy*dy <= (NOTE_RADIUS-2)*(NOTE_RADIUS-2)) {
                        SDL_RenderDrawPoint(renderer, x + dx, y + dy);
                    }
                }
            }
        }
        
        // Highlight if playing
        if (note.isPlaying) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
            for (int dy = -NOTE_RADIUS-2; dy <= NOTE_RADIUS+2; dy++) {
                for (int dx = -NOTE_RADIUS-2; dx <= NOTE_RADIUS+2; dx++) {
                    if (dx*dx + dy*dy <= (NOTE_RADIUS+2)*(NOTE_RADIUS+2) && 
                        dx*dx + dy*dy > NOTE_RADIUS*NOTE_RADIUS) {
                        SDL_RenderDrawPoint(renderer, x + dx, y + dy);
                    }
                }
            }
        }
    }
    
    // Draw current note being placed
    if (isPlacingNote && currentFrequency > 0) {
        auto it = NOTE_POSITIONS.find(currentFrequency);
        if (it != NOTE_POSITIONS.end()) {
            int position = it->second;
            int y = centerY - position * LINE_SPACING / 2;
            
            // Get mouse position
            int mouseX, mouseY;
            SDL_GetMouseState(&mouseX, &mouseY);
            
            // Constrain to staff area
            mouseX = std::max(STAFF_X, std::min(STAFF_X + STAFF_WIDTH, mouseX));
            
            // Draw ghost note
            if (currentChannel == 1) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 255, 128);
            } else {
                SDL_SetRenderDrawColor(renderer, 255, 0, 0, 128);
            }
            
            // Draw filled circle for the note
            for (int dy = -NOTE_RADIUS; dy <= NOTE_RADIUS; dy++) {
                for (int dx = -NOTE_RADIUS; dx <= NOTE_RADIUS; dx++) {
                    if (dx*dx + dy*dy <= NOTE_RADIUS*NOTE_RADIUS) {
                        SDL_RenderDrawPoint(renderer, mouseX + dx, y + dy);
                    }
                }
            }
        }
    }
    
    // Draw controls
    SDL_Rect playButton = {STAFF_X, STAFF_Y + STAFF_HEIGHT + 10, 100, 30};
    SDL_Rect clearButton = {STAFF_X + 120, STAFF_Y + STAFF_HEIGHT + 10, 100, 30};
    
    // Play button
    SDL_SetRenderDrawColor(renderer, 100, 200, 100, 255);
    SDL_RenderFillRect(renderer, &playButton);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(renderer, &playButton);
    
    // Clear button
    SDL_SetRenderDrawColor(renderer, 200, 100, 100, 255);
    SDL_RenderFillRect(renderer, &clearButton);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(renderer, &clearButton);
    
    // Draw scroll indicators
    SDL_Rect leftScroll = {STAFF_X - 30, STAFF_Y + STAFF_HEIGHT / 2 - 15, 20, 30};
    SDL_Rect rightScroll = {STAFF_X + STAFF_WIDTH + 10, STAFF_Y + STAFF_HEIGHT / 2 - 15, 20, 30};
    
    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    SDL_RenderFillRect(renderer, &leftScroll);
    SDL_RenderFillRect(renderer, &rightScroll);
    
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(renderer, &leftScroll);
    SDL_RenderDrawRect(renderer, &rightScroll);
}

// Render UI elements
void renderUI(SDL_Renderer* renderer) {
    // Draw channel selection indicator
    SDL_Rect channelRect = {WINDOW_WIDTH - 150, 20, 130, 30};
    
    if (currentChannel == 1) {
        SDL_SetRenderDrawColor(renderer, 200, 200, 255, 255);  // Light blue for pulse 1
    } else if (currentChannel == 2) {
        SDL_SetRenderDrawColor(renderer, 255, 200, 200, 255);  // Light red for pulse 2
    } else {
        SDL_SetRenderDrawColor(renderer, 200, 255, 200, 255);  // Light green for wave
    }
    
    SDL_RenderFillRect(renderer, &channelRect);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(renderer, &channelRect);
    
    // Draw note type selection
    SDL_Rect noteTypeRect = {WINDOW_WIDTH - 150, 60, 130, 30};
    
    if (currentNoteType == EIGHTH_NOTE) {
        SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 180, 180, 180, 255);
    }
    
    SDL_RenderFillRect(renderer, &noteTypeRect);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(renderer, &noteTypeRect);
    
    // Draw instructions
    SDL_Rect instructRect = {50, WINDOW_HEIGHT - 60, WINDOW_WIDTH - 100, 50};
    SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
    SDL_RenderFillRect(renderer, &instructRect);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(renderer, &instructRect);
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
    
    // Process Channel 1 (Pulse wave)
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
    
    // Process Channel 2 (Pulse wave)
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
    
    // Process Channel 3 (Wave channel)
    {
        std::lock_guard<std::mutex> lock(CH3.lock);
        if (CH3.active && CH3.frequency > 0) {
            for (unsigned int i = 0; i < framesPerBuffer; i++) {
                // Calculate how much to advance in the waveform
                float phaseIncrement = CH3.frequency / SAMPLE_RATE;
                CH3.phase += phaseIncrement;
                
                // Wrap phase if needed
                if (CH3.phase >= 1.0f) {
                    CH3.phase -= 1.0f;
                }
                
                // Map phase to waveform index (0 to 15)
                size_t index = static_cast<size_t>(CH3.phase * 16) % 16;
                
                // Add the sample from the waveform
                out[i] += CH3.waveform[index] * AMPLITUDE * 0.5f; // Slightly quieter
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

// Handle mouse clicks on UI elements
void handleMouseClick(int x, int y, bool isRightClick) {
    // Check if click is on the staff
    if (x >= STAFF_X && x <= STAFF_X + STAFF_WIDTH &&
        y >= STAFF_Y && y <= STAFF_Y + STAFF_HEIGHT) {
        
        if (isRightClick) {
            // Right click to remove note
            removeNoteFromStaff(x, y);
        } else if (isPlacingNote && currentFrequency > 0) {
            // Left click to place note
            addNoteToStaff(x - STAFF_X, currentFrequency, currentChannel);
            isPlacingNote = false;
        }
    }
    
    // Check if click is on play button
    if (x >= STAFF_X && x <= STAFF_X + 100 &&
        y >= STAFF_Y + STAFF_HEIGHT + 10 && y <= STAFF_Y + STAFF_HEIGHT + 40) {
        startPlayback();
    }
    
    // Check if click is on clear button
    if (x >= STAFF_X + 120 && x <= STAFF_X + 220 &&
        y >= STAFF_Y + STAFF_HEIGHT + 10 && y <= STAFF_Y + STAFF_HEIGHT + 40) {
        staffNotes.clear();
        isPlayingSequence = false;
    }
    
    // Check if click is on left scroll button
    if (x >= STAFF_X - 30 && x <= STAFF_X - 10 &&
        y >= STAFF_Y + STAFF_HEIGHT / 2 - 15 && y <= STAFF_Y + STAFF_HEIGHT / 2 + 15) {
        scrollOffset = std::max(0, scrollOffset - 50);
    }
    
    // Check if click is on right scroll button
    if (x >= STAFF_X + STAFF_WIDTH + 10 && x <= STAFF_X + STAFF_WIDTH + 30 &&
        y >= STAFF_Y + STAFF_HEIGHT / 2 - 15 && y <= STAFF_Y + STAFF_HEIGHT / 2 + 15) {
        scrollOffset += 50;
    }
    
    // Check if click is on channel selection
    if (x >= WINDOW_WIDTH - 150 && x <= WINDOW_WIDTH - 20 &&
        y >= 20 && y <= 50) {
        // Cycle through channels: 1 -> 2 -> 3 -> 1
        currentChannel = (currentChannel % 3) + 1;
    }
    
    // Check if click is on note type selection
    if (x >= WINDOW_WIDTH - 150 && x <= WINDOW_WIDTH - 20 &&
        y >= 60 && y <= 90) {
        currentNoteType = (currentNoteType == EIGHTH_NOTE) ? QUARTER_NOTE : EIGHTH_NOTE;
    }
}

int main() {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    
    // Create window
    SDL_Window* window = SDL_CreateWindow(
        "Game Boy Audio Composer", 
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
        WINDOW_WIDTH, WINDOW_HEIGHT, 
        SDL_WINDOW_SHOWN
    );
    
    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }
    
    // Create renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // Initialize piano keys
    initPianoKeys();
    
    // Initialize PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
        SDL_DestroyRenderer(renderer);
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
        SDL_DestroyRenderer(renderer);
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
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    std::cout << "Game Boy Audio Composer" << std::endl;
    std::cout << "Channel 1 (Pulse) keys: A-S-D-F-G-H-J" << std::endl;
    std::cout << "Channel 2 (Pulse) keys: Z-X-C-V-B-N-M" << std::endl;
    std::cout << "Channel 3 (Wave) keys: 1-2-3-4-5-6-7" << std::endl;
    std::cout << "Press a key to select a note, then click on the staff to place it" << std::endl;
    std::cout << "Right-click to remove notes" << std::endl;
    std::cout << "Press P to play the composition" << std::endl;
    std::cout << "Press C to clear the staff" << std::endl;
    std::cout << "Press TAB to cycle through channels" << std::endl;
    std::cout << "Press N to toggle between eighth and quarter notes" << std::endl;
    std::cout << "Press Q or ESC to quit..." << std::endl;
    
    // Main event loop
    SDL_Event event;
    Uint32 lastUpdateTime = SDL_GetTicks();
    
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
                
                // Check for play key
                if (keycode == SDLK_p) {
                    startPlayback();
                }
                
                // Check for clear key
                if (keycode == SDLK_c) {
                    staffNotes.clear();
                    isPlayingSequence = false;
                }
                
                // Check for channel switch
                if (keycode == SDLK_TAB) {
                    currentChannel = (currentChannel == 1) ? 2 : 1;
                }
                
                // Check for note type switch
                if (keycode == SDLK_n) {
                    currentNoteType = (currentNoteType == EIGHTH_NOTE) ? QUARTER_NOTE : EIGHTH_NOTE;
                }
                
                // Channel 1 note handling
                auto it1 = CHANNEL1_NOTES.find(keycode);
                if (it1 != CHANNEL1_NOTES.end()) {
                    std::lock_guard<std::mutex> lock(CH1.lock);
                    CH1.active = true;
                    CH1.frequency = it1->second;
                    updatePianoKeyState(keycode, true);
                }
                
                // Channel 2 note handling
                auto it2 = CHANNEL2_NOTES.find(keycode);
                if (it2 != CHANNEL2_NOTES.end()) {
                    std::lock_guard<std::mutex> lock(CH2.lock);
                    CH2.active = true;
                    CH2.frequency = it2->second;
                    updatePianoKeyState(keycode, true);
                }
                
                // Channel 3 (Wave) note handling
                auto it3 = CHANNEL3_NOTES.find(keycode);
                if (it3 != CHANNEL3_NOTES.end()) {
                    std::lock_guard<std::mutex> lock(CH3.lock);
                    CH3.active = true;
                    CH3.frequency = it3->second;
                    // No piano key to update for wave channel
                }
            } else if (event.type == SDL_KEYUP) {
                SDL_Keycode keycode = event.key.keysym.sym;
                
                // Channel 1 note handling
                auto it1 = CHANNEL1_NOTES.find(keycode);
                if (it1 != CHANNEL1_NOTES.end()) {
                    std::lock_guard<std::mutex> lock(CH1.lock);
                    CH1.active = false;
                    updatePianoKeyState(keycode, false);
                }
                
                // Channel 2 note handling
                auto it2 = CHANNEL2_NOTES.find(keycode);
                if (it2 != CHANNEL2_NOTES.end()) {
                    std::lock_guard<std::mutex> lock(CH2.lock);
                    CH2.active = false;
                    updatePianoKeyState(keycode, false);
                }
                
                // Channel 3 (Wave) note handling
                auto it3 = CHANNEL3_NOTES.find(keycode);
                if (it3 != CHANNEL3_NOTES.end()) {
                    std::lock_guard<std::mutex> lock(CH3.lock);
                    CH3.active = false;
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    handleMouseClick(event.button.x, event.button.y, false);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    handleMouseClick(event.button.x, event.button.y, true);
                }
            }
        }
        
        // Update playback at regular intervals
        Uint32 currentTime = SDL_GetTicks();
        if (currentTime - lastUpdateTime > 50) {  // 20 updates per second
            updatePlayback();
            lastUpdateTime = currentTime;
        }
        
        // Clear screen
        SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
        SDL_RenderClear(renderer);
        
        // Render piano
        renderPiano(renderer);
        
        // Render staff
        renderStaff(renderer);
        
        // Render UI
        renderUI(renderer);
        
        // Present renderer
        SDL_RenderPresent(renderer);
        
        // Small delay to prevent high CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Cleanup
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    SDL_DestroyRenderer(renderer);
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
