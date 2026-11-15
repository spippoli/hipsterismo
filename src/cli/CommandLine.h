#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

// What the command line asks for:
//
//   hipsterismo                                    play in a window
//   hipsterismo [--mode MODE] IMAGE [OUTPUT]       save the Mosaic of IMAGE and exit
//   hipsterismo -h | --help                        print the usage and exit
//
// MODE is one of the vertex mode names (vertexModeNames in Mosaic.h). OUTPUT
// defaults to IMAGE-mosaic.png, next to IMAGE. Paths are returned as given
// (relative ones stay relative).
struct CommandLine {
    enum Action { WINDOW, STILL, HELP, ERROR };
    Action action = WINDOW;

    // STILL: the vertex mode given with --mode, as an index into the mode
    // names, or nothing when --mode was not given.
    std::optional<int> mode;
    std::string image;
    std::string output;

    // ERROR: what was wrong with the arguments.
    std::string error;

    // The process exit code when the app stops right away: 0 for HELP, 2
    // (bad arguments) for ERROR.
    int exitCode() const { return action == ERROR ? 2 : 0; }
};

// Parses the arguments after the program name. A pure function: it does not
// touch the file system or openFrameworks.
CommandLine parseCommandLine(const std::vector<std::string>& args,
                             std::span<const char* const> modeNames);

// The usage message, listing the mode names.
std::string usage(const std::string& program, std::span<const char* const> modeNames);
