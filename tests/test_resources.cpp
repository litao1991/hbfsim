#include "hbfsim/core.h"

#include <cassert>

int main() {
  hbfsim::LinkResource link(10.0, 100);
  assert(link.reserve(0, 100) == 110);
  assert(link.reserve(0, 100) == 120);
  assert(link.free_at() == 20);

  hbfsim::DataFabric fabric(2, 20.0, 100);
  assert(fabric.reserve(0, 100, 0) == 110);
  assert(fabric.reserve(0, 100, 1) == 115);
  assert(fabric.reserve(0, 100, 0) == 120);
}
