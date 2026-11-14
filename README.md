# hipsterismo

This simple application in openframeworks takes a mov file or the webcam feed and processes it to create polygon graphics.
It can also process a still image from the command line:

```sh
bin/hipsterismo photo.jpg              # writes photo-mosaic.png
bin/hipsterismo photo.jpg result.png   # writes result.png
```

The code depends on the following addons:

* ofxCv 
* ofxDelaunay 
* ofxGui
* ofxMSACore
* ofxMSAInterpolator
* ofxOpenCv
* ofxXmlSettings

