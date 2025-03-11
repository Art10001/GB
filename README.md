The same as my YM2612 project, but now for the Gameboy.
The python implementations either require root (`keyboard`) or are busted due to thread issues.
The C++ file works, and uses SDL2 (SDL2-devel in Void), and Portaudio. You can read it if I missed a dependency.
It both plays to your speaker and logs to a wav. Currently, it works with the 2 Pulse channels the GB has, playing notes.
4gb.cpp includes a staff. The notes appear as circles. There is eighth/fourth separation thanks to the N key.
Sadly, the base long note is irritating.
5gb.cpp tried to fix that by making the base tone shorter (partial success), made the staff wider by adding 2 bars, as well as longer. Added a wave channel too. However, the wave channel seems to be wip at this time.

Tools used: OpenRouter chat, Claude 3.7 Sonnet (thinking variant)
