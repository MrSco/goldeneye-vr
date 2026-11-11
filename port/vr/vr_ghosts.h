/* The launcher's Ghosts tab (docs/speedrun.md): the ghost boards on the lobby
 * Worker, reached through the Friends client, and the ghost race's files. */
#ifndef VR_GHOSTS_H
#define VR_GHOSTS_H
#include "imgui/imgui.h"
#include <string>
#include <vector>

/* the launcher's Ghosts tab, inside its page */
void gevrGhostsPage(const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad);
/* a FriendsClient event of the ghost boards (GHOSTS_*, GHOST|, GHOST_*), from vr_friends.cpp */
void gevrGhostsEvent(const std::vector<std::string> &f);
#endif
