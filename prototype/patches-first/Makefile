# PROTOTYPE, throw away. `make` builds the generator, renders data/*.js from
# tests/fixtures/fingers.png, and prints where index.html is.
# CDT_DIR can point at an existing checkout of artem-ogre/CDT 2.0.2.
CDT_DIR ?= CDT
FIXTURE := ../../tests/fixtures/fingers.png

all: data/.done
	@echo "open $(CURDIR)/index.html"

CDT:
	git clone --depth 1 --branch 2.0.2 https://github.com/artem-ogre/CDT.git CDT

generate: generate.cpp | $(CDT_DIR)
	g++ -std=c++20 -O3 -DNDEBUG -I$(CDT_DIR)/CDT/include $< $$(pkg-config --cflags --libs opencv4) -o $@

data/.done: generate $(FIXTURE)
	mkdir -p data
	./generate $(FIXTURE) data
	touch $@

clean:
	rm -rf generate data

.PHONY: all clean
