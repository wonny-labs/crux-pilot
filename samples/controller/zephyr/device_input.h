#ifndef CONTROLLER_DEVICE_INPUT_H
#define CONTROLLER_DEVICE_INPUT_H

/* Native sample integration; one worker owns init/read. No platform API. */
int controller_device_init(void);
int controller_device_read(int *pressed);

#endif
