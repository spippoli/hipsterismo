#include "ofMain.h"
#include "ofApp.h"
#include "cli/CommandLine.h"

#include <filesystem>
#include <iostream>

//========================================================================
// hipsterismo                                  play fingers.mov (or the webcam) in a window
// hipsterismo [--mode MODE] IMAGE [OUTPUT]     save the Mosaic of IMAGE and exit
//                                              (OUTPUT defaults to IMAGE-mosaic.png)
// See CommandLine.h for the arguments.
int main(int argc, char* argv[]) {
    CommandLine commandLine = parseCommandLine(std::vector<std::string>(argv + 1, argv + argc), vertexModeNames);
    switch (commandLine.action) {
        case CommandLine::ERROR:
            std::cerr << argv[0] << ": " << commandLine.error << "\n" << usage(argv[0], vertexModeNames);
            return commandLine.exitCode();
        case CommandLine::HELP:
            std::cout << usage(argv[0], vertexModeNames);
            return commandLine.exitCode();
        default:
            break;
    }

    auto app = std::make_shared<ofApp>();
    ofGLFWWindowSettings settings;
    settings.setSize(1280, 720);

    if (commandLine.action == CommandLine::STILL) {
        // openFrameworks resolves relative paths against bin/data, so make
        // them absolute against the directory the user ran the command from.
        app->stillInput = std::filesystem::absolute(commandLine.image).string();
        app->stillOutput = std::filesystem::absolute(commandLine.output).string();
        app->stillMode = commandLine.mode;
        // Rendering needs a GL context but no window on screen.
        settings.visible = false;
        settings.setSize(64, 64);
    }

    ofCreateWindow(settings);
    return ofRunApp(app);
}
