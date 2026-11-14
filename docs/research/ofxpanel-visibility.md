# Showing and hiding parameters in an ofxPanel at runtime

Research for [#8](https://github.com/spippoli/hipsterismo/issues/8), part of map [#6](https://github.com/spippoli/hipsterismo/issues/6).

**Question.** In openFrameworks 0.12.x ofxGui, how can an `ofxPanel` show only the parameters that matter for the current mode (here `stable vertices`: `cell size`/`persistence` for stable, `interpolation resolution` for spline)? What happens to listeners, `loadFromFile`/`saveToFile` and the XML layout? The requirement is that every parameter is always saved, visible or not, with the same tags (`<panel><first_threshold>…`).

**Sources.** The ofxGui and core source of the local OF 0.12.1 install (`<OF_ROOT>`, `OF_VERSION_PATCH 1` in `libs/openFrameworks/utils/ofConstants.h`). Line numbers refer to that copy. Upstream: [ofxGui/src @ 0.12.1](https://github.com/openframeworks/openFrameworks/tree/0.12.1/addons/ofxGui/src). The [official ofxPanel docs](https://openframeworks.cc/documentation/ofxGui/ofxPanel/) list only the API and say nothing about hiding controls. A web search found no maintainer forum post on this, so every claim below comes from the source code.

## TL;DR

ofxGui has no per-control `setVisible()`. A panel draws, and sends mouse events to, exactly the controls in its `collection`. The supported way to show a subset is **`gui.clear()` and then `gui.add(param)` for each visible parameter**. But the panel serializes its *own* group, and after a rebuild that group holds only the visible parameters. So the panel's save/load buttons and `gui.saveToFile/loadFromFile` would stop covering hidden parameters. The fix: **do all persistence through the full `MosaicMaker::parameters` group** (`ofSerialize`/`ofDeserialize` on an `ofXml`), and hook the panel's `savePressedE`/`loadPressedE` so the header icons use it too. The XML then always contains every parameter, with the same tags as today. Do the rebuild in `update()` (set a dirty flag in the mode listener), not inside the toggle's own callback.

## How ofxGui decides what is shown and what is saved

- **No visibility flag.** `ofxBaseGui` has no hide/show API (`ofxBaseGui.h`). `ofxGuiGroup::render()` draws every element of `collection` unless the group is minimised (`ofxGuiGroup.cpp:355-359`). Mouse handlers loop over `collection` too (`:222-304`). Layout stacks `collection` vertically (`updateChildrenPositions`, `:488-520`).
- **`setup(group)`** calls `clear()`, creates one control per parameter (a slider for `int`, a toggle for `bool`, a nested `ofxGuiGroup` for a sub-group), and at the end assigns `parameters = _parameters` (`ofxGuiGroup.cpp:32-138`). `ofParameterGroup` is a handle to a shared `obj` (`ofParameter.h:298`), so after `setup` the panel's group *is* `MosaicMaker::parameters`.
- **`add(element)`** pushes to `collection` and also calls `parameters.add(element->getParameter())` (`:147-158`). `add(ofParameter<T>&)` creates the control, and the panel owns it in `ownedCollection` (`ofxGuiGroup.h:118-131`).
- **`clear()`** does `collection.clear(); parameters.clear();` (`:216-220`). `ofParameterGroup::clear()` *replaces* the handle's `obj` with a new empty one and keeps the name (`ofParameterGroup.cpp:49-53`). So it detaches the panel from `MosaicMaker::parameters` without emptying it. `clear()` does **not** free `ownedCollection`: old controls stay alive until the panel is destroyed. Each rebuild leaks a few controls, which is negligible for a key toggle. It also means a control whose callback triggered the rebuild is never destroyed under its own feet.
- **Serialization** is `ofSerialize(serializer, getParameter())` / `ofDeserialize(...)` on the panel's group (`ofxBaseGui.h:18-26`). The tag is the escaped name, with spaces turned into `_` (`ofParameter.cpp:7-31`). `saveToFile` first loads the existing XML, then serializes into it (`ofxBaseGui.cpp:231-247`). `ofSerialize` reuses a child with the same tag or appends one, and never removes tags (`ofXml.cpp:441-467`). `ofDeserialize` sets only the parameters present in both the group and the file (`ofXml.cpp:470+`).
- **Header icons.** The load/save icons call `loadFromFile(filename)`/`saveToFile(filename)` *unless* a listener of `loadPressedE`/`savePressedE` returns `true` (`ofxPanel.cpp:116-127`, `ofxPanel.h:26-27`). `ofEvent<void>` accepts `bool (T::*)()` listeners (`ofEvent.h:651`).

## Options compared

| Option | Shows only mode params? | XML with panel's own save/load | Notes |
|---|---|---|---|
| **A. `clear()` + re-`add()`** on one panel | Yes, exactly | **Subset only**: hidden params are not written or read (stale tags stay in the file and are not updated, missing ones are never created) | Fix with full-group persistence (below). Layout, order and colours are kept. The panel keeps its name `panel`. |
| **B. Nested `ofxGuiGroup` per mode, `minimize()`** | Header stays visible. Fully hidden only with `disableHeader()` + `minimize()` (then `render()` returns early and height is 0, `ofxGuiGroup.cpp:337, 489-491`) | **Changes the layout** to `<panel><stable><cell_size>…`, which breaks the requirement | Only works if the panel is built from a separate *display* group of references and persistence uses the flat group anyway. More moving parts than A. |
| **C. Separate panels** (common + one per mode, draw only the active one) | Yes | Each panel saves its own root tag, which breaks the layout | Undrawn panels ignore the mouse (`isGuiDrawing()`, `ofxBaseGui.cpp:162`), so this works. But it needs manual stacking and still needs full-group persistence. |
| **D. Re-`setup(subsetGroup)`** | Yes | Same problem as A (the panel's group becomes the subset) | Also resets position and width to the defaults/arguments. A is a lighter version of it. |

## Effects in detail (option A)

- **Listeners on parameters are unaffected.** Controls hold *references* to the same `ofParameter` values (`ofParameterGroup::add` stores `newReference()`, `ofParameterGroup.cpp:8-17`). Removing a control from the panel does not touch the parameter or its listeners, so `MosaicMaker` keeps seeing every change, including the `s` key setting `stableVertices` directly. The control listens to its value (`ofxSlider.cpp:59`, `ofxToggle.cpp:21`) and unregisters only in its destructor, so leaked old controls just mark themselves for redraw.
- **Listeners on the panel's group are lost.** After `clear()` the panel's group is a new object, so attach group-level listeners (`parameterChangedE`) to `mosaicMaker.parameters`, never to `gui.getParameter()`.
- **Re-adding to the panel's group adds a second parent.** `ofParameterGroup::add` calls `setParent` (`ofParameterGroup.cpp:16`), which appends to the parameter's `parents` list (`ofParameter.h:295`). This is harmless, and it is what `setup()` already does.
- **Calling `gui.add()` without a preceding `clear()`** right after `setup()` would append to `MosaicMaker::parameters` itself (the shared handle) and log "Adding another parameter with same name". Always `clear()` first.
- **Rebuilding from inside the toggle's callback.** `ofxGuiGroup::mousePressed` loops over `collection` by index (`:245-249`). Clearing and refilling it in the middle of the loop is not UB, because the indices stay valid and the old toggle is not freed. It is still fragile, so set a flag in the listener and rebuild in `update()`.
- **`loadFromFile`/`saveToFile` on the panel** cover only the visible subset after a rebuild (see the table), so stop using them directly. A `settings.xml` written through the full group always has all ten tags under `<panel>`. The current file has only three. `ofDeserialize` leaves the others at their defaults, and the first save adds them.

## Recommended approach

Use one `ofxPanel`, rebuilt with `clear()` + `add()` whenever `stable vertices` changes (deferred to `update()`), and persist through `mosaicMaker.parameters` everywhere: at startup and from the header icons. Sketch:

```cpp
// ofApp.h
ofEventListener modeListener, saveListener, loadListener;
bool panelDirty = true;
void rebuildPanel();
bool saveSettings();   // bool return: true suppresses ofxPanel's default save/load
bool loadSettings();

// ofApp.cpp
void ofApp::setup(){
    mosaicMaker.parameters.setName("panel");            // root tag stays <panel>
    gui.setup(mosaicMaker.parameters, "settings.xml", 0, 0);
    gui.setTextColor(ofColor(255,255,255));
    loadSettings();
    modeListener = mosaicMaker.stableVertices.newListener([this](bool &){ panelDirty = true; });
    saveListener = gui.savePressedE.newListener(this, &ofApp::saveSettings);
    loadListener = gui.loadPressedE.newListener(this, &ofApp::loadSettings);
}

void ofApp::update(){
    if (panelDirty) { rebuildPanel(); panelDirty = false; }
    // ... existing frame handling
}

void ofApp::rebuildPanel(){
    MosaicMaker & m = mosaicMaker;
    gui.clear();                      // detaches gui's group; m.parameters is untouched
    gui.add(m.threshold1);
    gui.add(m.threshold2);
    gui.add(m.blurIterations);
    gui.add(m.stableVertices);
    if (m.stableVertices) { gui.add(m.cellSize); gui.add(m.persistence); }
    else                  { gui.add(m.resolution); }
    gui.add(m.minArea);
    gui.add(m.maxArea);
    gui.add(m.findHoles);
}

bool ofApp::saveSettings(){           // every parameter, visible or not
    ofXml xml;
    if (ofFile("settings.xml").exists()) xml.load("settings.xml");
    ofSerialize(xml, mosaicMaker.parameters);
    xml.save("settings.xml");
    return true;
}

bool ofApp::loadSettings(){
    ofXml xml;
    if (xml.load("settings.xml")) ofDeserialize(xml, mosaicMaker.parameters);
    return true;
}
```

Notes on the sketch:

- The mode-to-parameters mapping (which parameters belong to which mode) is domain knowledge. It could live in `MosaicMaker` (for example a `visibleParameters()` returning the list), so `ofApp` doesn't hard-code it and the tests can check it.
- `gui.add(m.stableVertices)` creates the toggle that drives the rebuild. Its value change sets `panelDirty`, and the rebuild happens on the next `update()`.
- If ofApp should not leak controls on every toggle, it can own the controls itself (`ofxIntSlider cellSizeSlider; cellSizeSlider.setup(m.cellSize);`, then `gui.add(&cellSizeSlider)`). The panel then never owns them, and nothing accumulates. Declare them after `gui` so they are destroyed before it, and call `gui.clear()` in `exit()`.
