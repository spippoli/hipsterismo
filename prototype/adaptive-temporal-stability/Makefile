# PROTOTYPE, throw away (#23). `make` builds the generator, runs adaptive over
# bin/data/fingers.mov and a noisy still Frame, encodes data/*.mp4 with ffmpeg,
# and prints where index.html is. Needs OpenCV 4 (pkg-config opencv4) and ffmpeg.
# CDT_DIR can point at an existing checkout of artem-ogre/CDT 2.0.2.
CDT_DIR ?= CDT
VIDEO := ../../bin/data/fingers.mov
RUNS := video-raw video-smooth noise-raw noise-smooth

all: data/.done
	@echo "open $(CURDIR)/index.html"

CDT:
	git clone --depth 1 --branch 2.0.2 https://github.com/artem-ogre/CDT.git CDT

stability: stability.cpp | $(CDT_DIR)
	g++ -std=c++20 -O3 -DNDEBUG -I$(CDT_DIR)/CDT/include $< $$(pkg-config --cflags --libs opencv4) -o $@

data/.done: stability $(VIDEO)
	mkdir -p data
	./stability $(VIDEO) data
	for r in $(RUNS); do \
	  ffmpeg -y -loglevel error -framerate 30 -i data/frames-$$r/%04d.png \
	    -c:v libx264 -pix_fmt yuv420p -crf 23 -g 15 data/$$r.mp4 && rm -r data/frames-$$r; \
	done
	touch $@

clean:
	rm -rf stability data

.PHONY: all clean
