#include <cstddef>
#include <assert.h>
#include <new>
#include <yak/heap.h>
#include <yak/panic.h>
#include <yak/macro.h>
#include <yak/hint.h>

extern "C" {
void *__dso_handle = &__dso_handle;

void __cxa_pure_virtual()
{
	panic("__cxa_pure_virtual");
}

void __cxa_deleted_virtual()
{
	panic("__cxa_deleted_virtual");
}

void __cxa_atexit()
{
}
}

[[nodiscard]] void *operator new(std::size_t size) { return kmalloc(size); }
[[nodiscard]] void *operator new(std::size_t size, const std::nothrow_t &) noexcept { return kmalloc(size); }

[[nodiscard]] void *operator new(std::size_t size, std::align_val_t) { return kmalloc(size); }
[[nodiscard]] void *operator new(std::size_t size, std::align_val_t, const std::nothrow_t &) noexcept { return kmalloc(size); }

[[nodiscard]] void *operator new[](std::size_t size) { return kmalloc(size); }
[[nodiscard]] void *operator new[](std::size_t size, const std::nothrow_t &) noexcept { return kmalloc(size); }

[[nodiscard]] void *operator new[](std::size_t size, std::align_val_t) { return kmalloc(size); }
[[nodiscard]] void *operator new[](std::size_t size, std::align_val_t, const std::nothrow_t &) noexcept { return kmalloc(size); }

void operator delete(void *ptr) noexcept { kfree(ptr, 0); }
void operator delete(void *ptr, const std::nothrow_t &) noexcept { kfree(ptr, 0); }

void operator delete(void *ptr, std::align_val_t) noexcept { kfree(ptr, 0); }
void operator delete(void *ptr, std::align_val_t, const std::nothrow_t &) noexcept { kfree(ptr, 0); }

void operator delete(void *ptr, std::size_t) noexcept { kfree(ptr, 0); }
void operator delete(void *ptr, std::size_t, const std::nothrow_t &) noexcept { kfree(ptr, 0); }

void operator delete(void *ptr, std::size_t, std::align_val_t) noexcept { kfree(ptr, 0); }
void operator delete(void *ptr, std::size_t, std::align_val_t, const std::nothrow_t &) noexcept { kfree(ptr, 0); }

void operator delete[](void *ptr) noexcept { kfree(ptr, 0); }
void operator delete[](void *ptr, const std::nothrow_t &) noexcept { kfree(ptr, 0); }

void operator delete[](void *ptr, std::align_val_t) noexcept { kfree(ptr, 0); }
void operator delete[](void *ptr, std::align_val_t, const std::nothrow_t &) noexcept { kfree(ptr, 0); }

void operator delete[](void *ptr, std::size_t) noexcept { kfree(ptr, 0); }
void operator delete[](void *ptr, std::size_t, const std::nothrow_t &) noexcept { kfree(ptr, 0); }

void operator delete[](void *ptr, std::size_t, std::align_val_t) noexcept { kfree(ptr, 0); }
void operator delete[](void *ptr, std::size_t, std::align_val_t, const std::nothrow_t &) noexcept { kfree(ptr, 0); }
