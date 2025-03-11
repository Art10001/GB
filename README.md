The same as my YM2612 project, but now for the Gameboy.
The python implementations either require root (`keyboard`) or are busted due to thread issues.
The C++ file works, and uses SDL2 (SDL2-devel in Void), and Portaudio. You can read it if I missed a dependency.
It both plays to your speaker and logs to a wav. Currently, it works with the 2 Pulse channels the GB has, playing notes.

Tools used: OpenRouter chat, Claude 3.7 Sonnet (thinking variant)
