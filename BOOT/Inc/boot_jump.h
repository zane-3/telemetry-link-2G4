#ifndef AEROCORE_BOOT_JUMP_H
#define AEROCORE_BOOT_JUMP_H

#include <stdbool.h>

bool boot_application_is_valid(void);
bool boot_should_stay_in_loader(void);
void boot_refresh_watchdog(void);
void boot_jump_to_application(void);

#endif
