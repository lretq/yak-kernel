#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <yak/hint.h>
#include <yak/panic.h>
#include <yak/macro.h>

typedef enum status {
	YAK_SUCCESS = 0,
	YAK_NOENT,
	YAK_NULL_DEREF,
	YAK_NOT_IMPLEMENTED,
	YAK_NOT_SUPPORTED,
	YAK_BUSY,
	YAK_OOM,
	YAK_TIMEOUT,
	YAK_CANCELLED,
	YAK_IO,
	YAK_INVALID_ARGS,
	YAK_UNKNOWN_FS,
	YAK_NODEV,
	YAK_NODIR,
	YAK_EXISTS,
	YAK_NOSPACE,
	YAK_EOF,
	YAK_MFILE, /* process has too many opened files */
	YAK_PERM_DENIED,
	YAK_BADF,
	YAK_NOTTY,
} status_t;

#define IS_OK(x) (likely((x) == YAK_SUCCESS))
#define IS_ERR(x) (unlikely((x) != YAK_SUCCESS))

#define IF_OK(expr) if (IS_OK((expr)))
#define IF_ERR(expr) if (IS_ERR((expr)))

#define EXPECT(expr)                                                          \
	do {                                                                  \
		status_t tmp_res = expr;                                      \
		IF_ERR(tmp_res)                                               \
		{                                                             \
			panic("%s:%d %s: unexpected failure: %s\n", __FILE__, \
			      __LINE__, __func__, status_str(tmp_res));       \
		}                                                             \
	} while (0)

const char *status_str(status_t status);
int status_errno(status_t status);

#ifdef __cplusplus
}
#endif
