#ifndef GEVR_PAUSE_MENU_H
#define GEVR_PAUSE_MENU_H
#ifdef __cplusplus
extern "C" {
#endif
enum { GEVR_PAUSE_MATCH, GEVR_PAUSE_RULES, GEVR_PAUSE_PLAYER, GEVR_PAUSE_AUDIO };
enum { GEVR_PAUSE_STEP, GEVR_PAUSE_CHOICE, GEVR_PAUSE_TOGGLE, GEVR_PAUSE_SLIDER };
typedef struct {
    int id, kind, editable, selected, count;
    char label[64], value[128];
} GevrPauseField;
/* Integer-only bridge: the original game headers define their own bool. */
int gevrNativePauseOpen(void);
void gevrNativePauseResume(void);
void gevrNativePauseRender(void);
int gevrPauseReadField(int tab, int index, GevrPauseField *field);
const char *gevrPauseChoice(int id, int index);
void gevrPauseSelect(int id, int index);
void gevrPauseStep(int id, int direction);
void gevrPauseVolume(int id, int percent);
int gevrPauseActionAvailable(const char *label);
void gevrPauseAction(const char *label);
void gevrPauseLocalVitals(int *health, int *armour);
void gevrPausePlayerStats(int slot, int *points, int *kills, int *losses);
int gevrPauseObjective(int index, char *text, unsigned size);
void gevrFeedPauseInput(int opening);
#ifdef __cplusplus
}
#endif
#endif
