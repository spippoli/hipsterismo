#include "CDT.h"
int countB() {
    CDT::Triangulation<float> t;
    std::vector<CDT::V2d<float>> v{{0, 0}, {1, 0}, {0, 1}};
    t.insertVertices(v);
    t.eraseSuperTriangle();
    return int(t.triangles.size());
}
