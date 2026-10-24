#pragma once
// Poll only the upper physical PWR key on the 1.75C board (TCA9554 P4).
// The lower BOOT key remains untouched.
void garage_button_start();
