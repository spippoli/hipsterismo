# PROTOTYPE, throw away (#22). `make` builds the generator and renders out/*.png
# and out/stats.tsv, then prints where index.html is.
# CDT_DIR can point at an existing checkout of artem-ogre/CDT 2.0.2.
# PHOTO is a real photo from the openFrameworks examples; its renders are not
# committed (see .gitignore), only the fingers ones.
CDT_DIR ?= CDT
OF_ROOT ?= ../../../../..
FINGERS := ../../tests/fixtures/fingers.png
PHOTO   ?= $(OF_ROOT)/examples/computer_vision/opencvHaarFinderExample/bin/data/test.jpg

# name:min:eps:blur. Fingers upscaled 4x: area x16, side x4.
F1 := ref:32:3:0.8
F4 := abs:32:3:0.8 eps-only:32:12:0.8 min-only:512:3:0.8 min-eps:512:12:0.8 all:512:12:3.2 \
      min8:8:3:0.8 min128:128:3:0.8 min1024:1024:3:0.8
# Photo 1024x680 vs 320x212: area x9.1 (min 32 -> 290), shorter side x2.83 (eps 3 -> 8.5, blur 0.8 -> 2.27).
P1 := ref:32:3:0.8
P3 := abs:32:3:0.8 min-only:290:3:0.8 min-eps:290:8.5:0.8 all:290:8.5:2.27 min1024:1024:3:0.8

all: out/.done
	@echo "open $(CURDIR)/index.html"

CDT:
	git clone --depth 1 --branch 2.0.2 https://github.com/artem-ogre/CDT.git CDT

generate: generate.cpp | $(CDT_DIR)
	g++ -std=c++20 -O3 -DNDEBUG -w -I$(CDT_DIR)/CDT/include $< $$(pkg-config --cflags --libs opencv4) -o $@

out/.done: generate $(FINGERS)
	rm -rf out && mkdir -p out
	./generate $(FINGERS) out f1 $(F1)
	./generate $(FINGERS)@4 out f4 $(F4)
	-./generate $(PHOTO)@0.3125 out p1 $(P1)
	-./generate $(PHOTO) out p3 $(P3)
	# stats.tsv as a script, since index.html opened from file:// cannot fetch it
	awk -F'\t' 'BEGIN{printf "window.STATS=["} {printf "%s[", (NR>1?",":""); for(i=1;i<=NF;i++) printf "%s\"%s\"", (i>1?",":""), $$i; printf "]"} END{print "];"}' out/stats.tsv > out/stats.js
	touch $@

clean:
	rm -rf generate out

.PHONY: all clean
