#include <ultra64.h>
#include "chrobjdata.h"

void init_player_gait_object(void) {
  /*
   * RootNode is a ModelNode *, and player_gait_hdr is a ModelNode, so this
   * needs no cast at all. The original wrote (int)&player_gait_hdr, which is
   * exact with 32-bit pointers but on LP64 truncates the address to its low
   * half: modelInitRwData was handed 0x39ad53c0 and faulted immediately.
   */
  player_gait_object_header.RootNode = &player_gait_hdr;
  return;
}
