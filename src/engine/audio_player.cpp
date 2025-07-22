/*
#include <miniaudio.h>
#include <iostream>
*/
/*
namespace smol {
    void play_sound(const char* filepath) {
      ma_result result;
       ma_engine engine;

        result = ma_engine_init(NULL, &engine);
        if (result != MA_SUCCESS) {
            std::cerr << "Failed to initialize audio engine\n";
            return;
        }

        result = ma_engine_play_sound(&engine, filepath, NULL);
        if (result != MA_SUCCESS) {
            std::cerr << "Failed to play sound\n";
        }

        std::cout << "Playing: " << filepath << std::endl;
        ma_engine_uninit(&engine);
    }
}
    */