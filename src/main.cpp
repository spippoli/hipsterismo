#include "ofMain.h"
#include "ofApp.h"

#include <filesystem>
#include <iostream>

//========================================================================
// hipsterismo                    play fingers.mov (or the webcam) in a window
// hipsterismo IMAGE [OUTPUT]     save the Mosaic of IMAGE and exit
//                                (OUTPUT defaults to IMAGE-mosaic.png)
int main(int argc, char* argv[]) {
    auto app = std::make_shared<ofApp>();
    ofGLFWWindowSettings settings;
    settings.setSize(1280, 720);

    if (argc > 1) {
        std::string first = argv[1];
        if (argc > 3 || first == "-h" || first == "--help") {
            std::cerr << "usage: " << argv[0] << " [IMAGE [OUTPUT]]\n"
                      << "  without arguments, play the video or webcam in a window\n"
                      << "  with IMAGE, save its Mosaic to OUTPUT (default IMAGE-mosaic.png) and exit\n";
            return argc > 3 ? 2 : 0;
        }
        // openFrameworks resolves relative paths against bin/data, so make
        // them absolute against the directory the user ran the command from.
        std::filesystem::path input = std::filesystem::absolute(first);
        std::filesystem::path output = argc == 3
            ? std::filesystem::absolute(argv[2])
            : input.parent_path() / (input.stem().string() + "-mosaic.png");
        app->stillInput = input.string();
        app->stillOutput = output.string();
        // Rendering needs a GL context but no window on screen.
        settings.visible = false;
        settings.setSize(64, 64);
    }

    ofCreateWindow(settings);
    return ofRunApp(app);
}
