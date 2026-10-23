#pragma once
// Windows host tests stub the Android random-name helper; settings code is unchanged.
#ifdef _WIN32
unsigned int arc4random_uniform(unsigned int upper);
#endif
