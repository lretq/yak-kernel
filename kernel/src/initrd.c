#define pr_fmt(fmt) "initrd: " fmt

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <nanoprintf.h>
#include <yak/panic.h>
#include <yak/fs/vfs.h>
#include <yak/status.h>
#include <yak/macro.h>
#include <yak/log.h>

#include <yak/elf.h>
#include <yak/init.h>

INIT_STAGE(user);

enum [[gnu::packed]] tar_file_type {
	TAR_REG = '0',
	TAR_AREG = '\0',
	TAR_LNK = '1',
	TAR_SYM = '2',
	TAR_CHR = '3',
	TAR_BLK = '4',
	TAR_DIR = '5',
	TAR_FIFO = '6',
	TAR_CONT = '7',
};

struct [[gnu::packed]] tar_header {
	char filename[100];
	// octal:
	char mode[8];
	char uid[8];
	char gid[8];
	char filesize[12];
	char mtime[12];
	// not octal:
	uint64_t chksum;
	uint8_t filetype;
	char linkname[100];
	char magic[6]; /* ustar then \0 */
	uint16_t ustar_version;
	char user_name[32];
	char group_name[32];
	uint64_t device_major;
	uint64_t device_minor;
	char filename_prefix[155];

	char _padding[12];
};

_Static_assert(sizeof(struct tar_header) == 512,
	       "Tar header size is incorrect");

static uint64_t decode_octal(char *data, size_t size)
{
	uint8_t *currentPtr = (uint8_t *)data + size;
	uint64_t sum = 0;
	uint64_t currentMultiplier = 1;

	/* find last \0 or space */
	uint8_t *checkPtr = currentPtr;
	for (; checkPtr >= (uint8_t *)data; checkPtr--) {
		if (*checkPtr == 0 || *checkPtr == ' ')
			currentPtr = checkPtr - 1;
	}
	/* decode the octal number */
	for (; currentPtr >= (uint8_t *)data; currentPtr--) {
		sum += (uint64_t)((*currentPtr) - 48) * currentMultiplier;
		currentMultiplier *= 8;
	}
	return sum;
}

#define PATH_MAX 4096

static char pathbuf[PATH_MAX];
static char pathbuf2[PATH_MAX];

static char long_name[PATH_MAX] = { 0 };
static char long_linkname[PATH_MAX] = { 0 };
static bool have_long_name = false;
static bool have_long_linkname = false;

bool is_zero_block(const void *p)
{
	const unsigned char *b = p;
	for (int i = 0; i < 512; i++)
		if (b[i] != 0)
			return false;
	return true;
}

static char hardlink_copy_buffer[1024 * 16];

static void hardlink_copy(struct vattr *attr, char *path, struct vnode *src_vn)
{
	struct vnode *vn;
	EXPECT(vfs_create(path, VREG, attr, &vn));
	guard_ref_adopt(vn, vnode);

	size_t offset = 0;
	size_t remaining = src_vn->filesize;

	while (remaining > 0) {
		size_t to_copy = (remaining > sizeof(hardlink_copy_buffer)) ?
					 sizeof(hardlink_copy_buffer) :
					 remaining;
		size_t delta;

		EXPECT(VOP_READ(src_vn, offset, hardlink_copy_buffer, to_copy,
				&delta));
		EXPECT(VOP_WRITE(vn, offset, hardlink_copy_buffer, to_copy,
				 &delta));

		offset += to_copy;
		remaining -= to_copy;
	}
}

void initrd_unpack_tar(const char *path, const char *data, size_t len)
{
	pr_debug("begin unpack ...\n");
	struct vnode *vn;
	size_t zero_filled = 0;

	size_t pos = 0;

	while (pos <= len) {
		if (zero_filled >= 2)
			break;

		struct tar_header *hdr = (struct tar_header *)(data + pos);
		pos += sizeof(struct tar_header);

		if (is_zero_block(hdr)) {
			zero_filled++;
			continue;
		}

		char *filename = hdr->filename;
		char *nameprefix = hdr->filename_prefix;
		char *linkname = hdr->linkname;

		if (have_long_name) {
			filename = long_name;
			nameprefix = "";
			have_long_name = false;
		}

		if (have_long_linkname) {
			linkname = long_linkname;
			have_long_linkname = false;
		}

		npf_snprintf(pathbuf, sizeof(pathbuf), "%s/%s%s", path,
			     nameprefix, filename);

		zero_filled = 0;

#define HDR_OFLD(f) decode_octal((hdr)->f, sizeof((hdr)->f))
		struct vattr attr;
		attr.mode = HDR_OFLD(mode);
		attr.uid = HDR_OFLD(uid);
		attr.gid = HDR_OFLD(gid);
		time_t mtime = HDR_OFLD(mtime);
		attr.mtime = (struct timespec){ .tv_sec = mtime, .tv_nsec = 0 };
		attr.atime = attr.mtime;

		switch (hdr->filetype) {
		case 'L': { // GNU long filename
			size_t size = HDR_OFLD(filesize);

			if (size >= sizeof(long_name))
				panic("tar long filename too long\n");

			memcpy(long_name, data + pos, size);
			long_name[size - 1] =
				'\0'; // tar guarantees NUL, but be safe

			pos += ALIGN_UP(size, 512);

			have_long_name = true;
			break;
		}
		case 'K': { // GNU long linkname
			size_t size = HDR_OFLD(filesize);

			if (size >= sizeof(long_linkname))
				panic("tar long linkname too long\n");

			memcpy(long_linkname, data + pos, size);
			long_linkname[size - 1] = '\0';

			pos += ALIGN_UP(size, 512);

			have_long_linkname = true;
			break;
		}
		case TAR_LNK: {
			npf_snprintf(pathbuf2, sizeof(pathbuf2), "%s/%s", path,
				     linkname);

			struct vnode *src_vn;
			EXPECT(vfs_open(pathbuf2, NULL, 0, &src_vn));
			guard_ref_adopt(src_vn, vnode);

			status_t rv = vfs_link(src_vn, pathbuf, NULL);
			if (rv == YAK_NOT_SUPPORTED) {
				pr_warn("fall back to copying for hardlinks\n");
				hardlink_copy(&attr, pathbuf, src_vn);
			} else {
				EXPECT(rv);
			}
			break;
		}
		case TAR_SYM:
			//pr_debug("create sym %s -> %s\n", pathbuf, hdr->linkname);
			EXPECT(vfs_symlink(pathbuf, linkname, &attr, NULL,
					   &vn));
			vnode_deref(vn);
			break;

		case TAR_AREG:
		case TAR_REG:
			//pr_debug("create file %s\n", pathbuf);
			EXPECT(vfs_create(pathbuf, VREG, &attr, &vn));

			size_t size = HDR_OFLD(filesize);

			size_t written = -1;
			EXPECT(VOP_WRITE(vn, 0, (data + pos), size, &written));

			vnode_deref(vn);

			pos += ALIGN_UP(size, 512);
			break;

		case TAR_DIR:
			//pr_debug("create dir %s\n", pathbuf);
			EXPECT(vfs_create(pathbuf, VDIR, &attr, &vn));
			vnode_deref(vn);

			break;

		default:
			pr_warn("unhandled TAR type: %c! File: %s\n",
				hdr->filetype, pathbuf);
			break;
		}
	}

#undef HDR_OFLD

	pr_debug("unpack complete\n");
}
