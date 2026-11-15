#include "CDT.h"
#include <cstdio>
int countB();
int main() {
    std::vector<CDT::V2d<double>> v{{0, 0}, {4, 0}, {4, 3}, {0, 3}, {1, 1}, {3, 2}};
    std::vector<CDT::Edge> e{CDT::Edge(4, 5)};
    CDT::Triangulation<double> t;
    t.insertVertices(v);
    t.insertEdges(e);
    t.eraseSuperTriangle();
    std::printf("tris %zu, other TU %d\n", t.triangles.size(), countB());
}
