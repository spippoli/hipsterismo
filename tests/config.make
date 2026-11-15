# Unit tests for the Mosaic module. Run from this folder:
#   make && make run
# The test app has no window and exits with the number of failed tests.

OF_ROOT = ../../../..

# Compile the app's modules (src/mosaic, src/cli: every folder under src/)
# straight from its sources, but not src/ itself, which holds the app's
# main.cpp and ofApp. OF supports a single external source path, so this is
# ../src with its top folder excluded rather than a list of folders.
PROJECT_EXTERNAL_SOURCE_PATHS = ../src
PROJECT_EXCLUSIONS = ../src
