// The AI ally's settings (#194) through goldeneye-vr.ini: vr_settings.cpp's
// save and load lines, cut by test_ally_ui.py, round trip and clamp. Its rows
// (Rules > AI ally) are the settings table's: port/tests/test_settings_table.py
// checks when they can change, test_launcher_frame.py that the page fits.
#include <cassert>
#include <cstdio>
#include <cstring>

int VrAlly, VrAllyWeapon, VrAllyAggression=1, VrAllyHealth=100, VrAllyDamage=100, VrAllyInvincible;
/* INSERT_SETTINGS */

int main() {
    VrAlly=VrAllyInvincible=1; VrAllyWeapon=7; VrAllyAggression=2;
    VrAllyHealth=333; VrAllyDamage=250;
    FILE *f=tmpfile(); assert(f); saveAlly(f); rewind(f);
    VrAlly=VrAllyInvincible=VrAllyWeapon=VrAllyAggression=VrAllyHealth=VrAllyDamage=0;
    char line[256]; while (fgets(line,sizeof(line),f)) loadAlly(line); fclose(f);
    assert(VrAlly==1 && VrAllyInvincible==1 && VrAllyWeapon==7 && VrAllyAggression==2
           && VrAllyHealth==333 && VrAllyDamage==250);
    loadAlly("AllyWeapon=-1"); loadAlly("AllyAggression=99");
    loadAlly("AllyHealth=99999"); loadAlly("AllyDamage=-50");
    assert(VrAllyWeapon==0 && VrAllyAggression==2 && VrAllyHealth==1000 && VrAllyDamage==25);
    puts("PASS: companion settings round trip through goldeneye-vr.ini and clamp out-of-range values");
}
