#include <stdint.h>
#include <yak/panic.h>

#if UINT32_MAX == UINTPTR_MAX
#define STACK_CHK_GUARD 0xe2dee396
#else
#define STACK_CHK_GUARD 0x595e9fbd94fda766
#endif

[[gnu::used]]
uintptr_t __stack_chk_guard = STACK_CHK_GUARD;

[[gnu::noreturn, gnu::cold, gnu::used]]
void __stack_chk_fail(void)
{
	panic("stack smashing detected\n");
	__builtin_trap();
}
