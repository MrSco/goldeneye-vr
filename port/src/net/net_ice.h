#ifndef GEVR_NET_ICE_H
#define GEVR_NET_ICE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void netIceStartHost(void);
bool netIceStartClient(const char *join_id, const char *turn_user, const char *turn_password);
bool netIceAddHostPeer(const char *join_id, const char *offer, const char *turn_user, const char *turn_password);
bool netIceApplyAnswer(const char *join_id, const char *answer);
bool netIceTakeDescription(const char *join_id, char *out, size_t capacity);
const char *netIceStatus(const char *join_id);
void netIcePoll(void);
void netIceStop(void);
int netIcePeerCount(void);
void netIceForgetPeer(const char *virtual_ip);

#ifdef __cplusplus
}
#endif

#endif
