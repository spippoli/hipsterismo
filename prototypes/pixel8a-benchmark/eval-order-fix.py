# Throwaway: writes a copy of Adaptive.cpp whose triangulate() calls vertex()
# for the two ends of an edge in an explicit order. The original passes both
# calls as arguments of one constructor, whose evaluation order C++ leaves
# unspecified: GCC goes right to left, clang left to right, so the CDT gets
# its vertices in a different order. GCC's order keeps the desktop golden.
#   python3 eval-order-fix.py IN OUT
import sys

s = open(sys.argv[1]).read()
old = "edges.push_back(CDT::Edge(vertex(chain.points[i]), vertex(chain.points[i + 1])));"
new = ("{ CDT::VertInd b = vertex(chain.points[i + 1]); CDT::VertInd a = vertex(chain.points[i]); "
       "edges.push_back(CDT::Edge(a, b)); }")
assert s.count(old) == 1, "pattern not found once"
open(sys.argv[2], "w").write(s.replace(old, new))
