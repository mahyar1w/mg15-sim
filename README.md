# MG15CF guitar amp simulator

Standalone C++20 / PortAudio amp simulator with clean and overdrive channels, independent channel volumes, and shared bass/middle/treble controls.

Signal chain: **amp -> EQ -> IR -> output trim**.

## Build and run

Requires CMake, a C++20 compiler, OpenGL, GLFW3, and PortAudio development packages. CMake fetches Dear ImGui v1.91.8.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/ampsim
```

Run tests:

```sh
ctest --test-dir build --output-on-failure
```

Select your audio interface and press Start. Processing is mono at 48 kHz with identical stereo outputs.
