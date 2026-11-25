#include <cassert>

#include "scene.h"

using namespace esphome::hatch_scenes;

static Scenes saved(std::initializer_list<int> slots) {
  Scenes scenes{};
  for (int slot : slots) scenes[slot - 1].saved = true;
  return scenes;
}

int main() {
  assert(first_empty(saved({})) == 1);
  assert(first_empty(saved({1, 2, 4})) == 3);
  assert(first_empty(saved({1, 2, 3, 4, 5, 6, 7, 8, 9})) == 0);

  const Scenes some = saved({2, 5, 9});
  // From no slot: forward starts at the first saved slot, backward at the last.
  assert(adjacent_saved(some, 0, 1) == 2);
  assert(adjacent_saved(some, 0, -1) == 9);
  // Empty slots are skipped, and both directions wrap.
  assert(adjacent_saved(some, 2, 1) == 5);
  assert(adjacent_saved(some, 9, 1) == 2);
  assert(adjacent_saved(some, 2, -1) == 9);
  assert(adjacent_saved(some, 5, -1) == 2);
  // From a deleted slot, carry on from where it was.
  assert(adjacent_saved(some, 3, 1) == 5);
  assert(adjacent_saved(some, 3, -1) == 2);
  // One saved slot is its own neighbour; none saved means no slot.
  assert(adjacent_saved(saved({4}), 4, 1) == 4);
  assert(adjacent_saved(saved({}), 0, 1) == 0);
  assert(adjacent_saved(saved({}), 0, -1) == 0);
  return 0;
}
