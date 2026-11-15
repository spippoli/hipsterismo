#include "CommandLine.h"

#include <filesystem>

namespace {

CommandLine error(const std::string& message) {
    CommandLine result;
    result.action = CommandLine::ERROR;
    result.error = message;
    return result;
}

std::string joined(std::span<const char* const> names) {
    std::string list;
    for (const char* name : names) {
        if (!list.empty()) list += "|";
        list += name;
    }
    return list;
}

} // namespace

CommandLine parseCommandLine(const std::vector<std::string>& args,
                             std::span<const char* const> modeNames) {
    CommandLine result;
    if (args.empty()) {
        return result;
    }
    if (args.size() == 1 && (args[0] == "-h" || args[0] == "--help")) {
        result.action = CommandLine::HELP;
        return result;
    }

    std::vector<std::string> positional;
    for (std::size_t i = 0; i < args.size(); i++) {
        const std::string& arg = args[i];
        if (arg == "--mode") {
            if (result.mode) {
                return error("--mode given more than once");
            }
            if (i + 1 == args.size()) {
                return error("--mode needs a value: " + joined(modeNames));
            }
            const std::string& name = args[++i];
            for (std::size_t mode = 0; mode < modeNames.size(); mode++) {
                if (name == modeNames[mode]) result.mode = int(mode);
            }
            if (!result.mode) {
                return error("unknown mode '" + name + "', expected " + joined(modeNames));
            }
        } else if (arg.size() > 1 && arg[0] == '-') {
            return error("unknown option " + arg);
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.empty()) {
        return error("missing IMAGE");
    }
    if (positional.size() > 2) {
        return error("too many arguments, from '" + positional[2] + "'");
    }
    if (positional[0].empty()) {
        return error("empty IMAGE");
    }
    result.action = CommandLine::STILL;
    result.image = positional[0];
    if (positional.size() == 2) {
        result.output = positional[1];
    } else {
        std::filesystem::path image(result.image);
        result.output = (image.parent_path() / (image.stem().string() + "-mosaic.png")).string();
    }
    return result;
}

std::string usage(const std::string& program, std::span<const char* const> modeNames) {
    return "usage: " + program + " [--mode " + joined(modeNames) + "] [IMAGE [OUTPUT]]\n"
           "  without arguments, play the video or webcam in a window\n"
           "  with IMAGE, save its Mosaic to OUTPUT (default IMAGE-mosaic.png) and exit;\n"
           "  --mode picks the vertex mode, else vertex_mode from settings.xml is used\n";
}
