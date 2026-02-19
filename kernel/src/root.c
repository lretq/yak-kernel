#include <yak/init.h>
#include <yak/status.h>
#include <yak/fs/vfs.h>

void mount_root()
{
	EXPECT(vfs_mount("/", "tmpfs"));
}

INIT_DEPS(rootfs, tmpfs);
INIT_ENTAILS(rootfs);
INIT_NODE(rootfs, mount_root);
