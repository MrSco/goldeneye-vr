#ifndef GEVR_NET_ALLY_H
#define GEVR_NET_ALLY_H
struct netbuf;
struct GevrAllyState;
void netAllyWriteState(struct netbuf *b, const struct GevrAllyState *state);
int netAllyReadState(struct netbuf *b, struct GevrAllyState *state);
void netAllyReset(void);
void netAllyHostTick(void);
void netAllySend(int slot);
void netAllyReceive(struct netbuf *b);
#endif
