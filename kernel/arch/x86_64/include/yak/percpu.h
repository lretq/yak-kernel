#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define __percpu __attribute__((section(".percpu"))) __seg_gs

#ifdef __cplusplus
}
#endif
