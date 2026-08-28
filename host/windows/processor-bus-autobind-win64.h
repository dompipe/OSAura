#ifndef OSAURA_WINDOWS_PROCESSOR_BUS_AUTOBIND64_H
#define OSAURA_WINDOWS_PROCESSOR_BUS_AUTOBIND64_H

/*
 * WSJX64 process-start binding state.
 * The CRT startup hook installs the shared processor bus before main().
 */
int osaura_windows_processor_bus64_autobind_status(void);

#endif
