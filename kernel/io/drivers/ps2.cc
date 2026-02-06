#include <stdint.h>
#include <uacpi/resources.h>
#include <yak/dpc.h>
#include <yak/ringbuffer.h>
#include <yak/spinlock.h>
#include <yak/hint.h>
#include <yak/irq.h>
#include <yak/log.h>
#include <yak/init.h>
#include <yak/tty.h>
#include <yak/io/acpi/AcpiDevice.hh>
#include <yak/io/acpi/AcpiPersonality.hh>
#include <yak/io/pci/Pci.hh>
#include <yak/io/base.hh>
#include <yak/io/Device.hh>
#include <yak/io/Dictionary.hh>
#include <yak/io/String.hh>
#include <yak/io/pci/PciPersonality.hh>

#include "../arch/x86_64/src/asm.h"

#define RINGBUF_SIZE 256

extern "C" struct tty *console_tty;

static const char codes[128] = {
	'\0', '\e', '1',  '2',	'3',  '4',  '5',  '6',	'7',  '8',  '9',  '0',
	'-',  '=',  '\b', '\t', 'q',  'w',  'e',  'r',	't',  'y',  'u',  'i',
	'o',  'p',  '[',  ']',	'\n', '\0', 'a',  's',	'd',  'f',  'g',  'h',
	'j',  'k',  'l',  ';',	'\'', '`',  '\0', '\\', 'z',  'x',  'c',  'v',
	'b',  'n',  'm',  ',',	'.',  '/',  '\0', '\0', '\0', ' ',  '\0', '\0',
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'
};

static const char codes_shifted[128] = {
	'\0', '\e', '!',  '@',	'#',  '$',  '%',  '^',	'&',  '*',  '(',  ')',
	'_',  '+',  '\b', '\t', 'Q',  'W',  'E',  'R',	'T',  'Y',  'U',  'I',
	'O',  'P',  '{',  '}',	'\n', '\0', 'A',  'S',	'D',  'F',  'G',  'H',
	'J',  'K',  'L',  ':',	'"',  '~',  '\0', '|',	'Z',  'X',  'C',  'V',
	'B',  'N',  'M',  '<',	'>',  '?',  '\0', '\0', '\0', ' ',  '\0', '\0',
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'
};

class Ps2Kbd final : public Device {
	IO_OBJ_DECLARE(Ps2Kbd);

    public:
	int probe([[maybe_unused]] Device *provider) override
	{
		return 100;
	}

	static uacpi_iteration_decision cb(void *user, uacpi_resource *resource)
	{
		Ps2Kbd *self = (Ps2Kbd *)user;
		switch (resource->type) {
		case UACPI_RESOURCE_TYPE_IRQ:
			self->gsi_ = resource->irq.irqs[0];
			break;
		case UACPI_RESOURCE_TYPE_FIXED_IO:
			if (self->data_port_ == (uint16_t)-1) {
				self->data_port_ = resource->fixed_io.address;
			} else {
				self->cmd_port_ = resource->fixed_io.address;
			}
			break;
		case UACPI_RESOURCE_TYPE_IO:
			if (self->data_port_ == (uint16_t)-1) {
				self->data_port_ = resource->io.minimum;
			} else {
				self->cmd_port_ = resource->io.minimum;
			}
			break;
		}
		return UACPI_ITERATION_DECISION_CONTINUE;
	}

	void tty_write_str(struct tty *tty, const char *s)
	{
		while (*s) {
			tty_input(tty, *s++);
		}
	}

	void handler()
	{
		bool extended = false;

		while (true) {
			auto state = spinlock_lock_interrupts(&buf_lock);
			if (!ringbuffer_available(&buf_)) {
				spinlock_unlock_interrupts(&buf_lock, state);
				break;
			}

			uint8_t sc;
			ringbuffer_get(&buf_, &sc, 1);

			spinlock_unlock_interrupts(&buf_lock, state);

			if (sc == 0x2a || sc == 0xaa || sc == 0x36 ||
			    sc == 0xb6) {
				shifted = (sc & 0x80) == 0;
				continue;
			}

			if (sc == 0xe0) {
				extended = true;
				continue;
			}

			// Left Ctrl
			if (!extended && sc == 0x1D) {
				ctrl = true;
				continue;
			}
			if (!extended && sc == 0x9D) {
				ctrl = false;
				continue;
			}

			// Right Ctrl
			if (extended && sc == 0x1D) {
				ctrl = true;
				extended = false;
				continue;
			}
			if (extended && sc == 0x9D) {
				ctrl = false;
				extended = false;
				continue;
			}

			if (sc == 0x38 && !extended) { // Left Alt press/release
				alt = !(sc & 0x80);
				continue;
			} else if (sc == 0xB8 &&
				   !extended) { // Left Alt release
				alt = false;
				continue;
			} else if (extended &&
				   sc == 0x38) { // Right Alt (AltGr)
				alt = !(sc & 0x80);
				extended = false;
				continue;
			} else if (extended &&
				   sc == 0xB8) { // Right Alt release
				alt = false;
				extended = false;
				continue;
			}

			// XXX: key release ignored
			if (sc & 0x80)
				continue;

			if (extended) {
				// Arrow keys / extended keys
				switch (sc) {
				case 0x48:
					tty_write_str(console_tty, "\e[A");
					break; // Up
				case 0x50:
					tty_write_str(console_tty, "\e[B");
					break; // Down
				case 0x4B:
					tty_write_str(console_tty, "\e[D");
					break; // Left
				case 0x4D:
					tty_write_str(console_tty, "\e[C");
					break; // Right
				default:
					break; // add more extended keys if needed
				}
				extended = false;
			} else {
				// Normal keys
				char c = shifted ? codes_shifted[sc] :
						   codes[sc];

				if (ctrl && c >= 'a' && c <= 'z') {
					pr_debug("ctrl pressed\n");
					c = c - 'a' + 1; // Ctrl + letter
				}

				if (alt) {
					pr_debug("alt pressed\n");
					tty_input(console_tty, 0x1B); // ESC
				}

				if (c != '\0') {
					tty_input(console_tty, c);
				}
			}
		}
	}

	static void dpc_handler(__unused struct dpc *dpc, void *arg)
	{
		auto kbd = (Ps2Kbd *)arg;
		kbd->handler();
	}

	static int ps2_irq_handler(void *arg)
	{
		auto kbd = (Ps2Kbd *)arg;

		if ((inb(kbd->cmd_port_) & 0x1) == 0) {
			return IRQ_NACK;
		}

		spinlock_lock_noipl(&kbd->buf_lock);
		while (inb(kbd->cmd_port_) & 0x1) {
			auto sc = inb(kbd->data_port_);

			if (0 == ringbuffer_put(&kbd->buf_, &sc, 1)) {
				spinlock_unlock_noipl(&kbd->buf_lock);
				pr_warn("dropping scancode\n");
				return IRQ_ACK;
			}
		}
		spinlock_unlock_noipl(&kbd->buf_lock);

		dpc_enqueue(&kbd->dpc_, arg);

		return IRQ_ACK;
	}

	bool start(Device *provider) override
	{
		if (!Device::start(provider))
			return false;

		pr_info("start ps2 keyboard driver\n");
		auto acpidev = provider->safe_cast<AcpiDevice>();
		auto node = acpidev->node_;

		dpc_init(&dpc_, dpc_handler);

		spinlock_init(&buf_lock);
		ringbuffer_init(&buf_, RINGBUF_SIZE);

		uacpi_resources *kb_res;
		uacpi_status ret = uacpi_get_current_resources(node, &kb_res);
		if (uacpi_unlikely_error(ret)) {
			pr_error("unable to retrieve PS2K resources: %s",
				 uacpi_status_to_string(ret));
			return false;
		}

		uacpi_for_each_resource(kb_res, cb, this);

		pr_info("ps2 kbd cmd port: 0x%x, data port: 0x%x, gsi: %d\n",
			cmd_port_, data_port_, gsi_);

		irq_object_init(&irqobj_, ps2_irq_handler, this);
		irq_alloc_ipl(&irqobj_, IPL_DEVICE, IRQ_MIN_IPL,
			      { .trigger = pin_config::PIN_TRG_LEVEL,
				.polarity = pin_config::PIN_POL_LOW });
		arch_program_intr(gsi_, irqobj_.slot->vector, false);

		uacpi_free_resources(kb_res);

		pr_info("ps2 ready :3\n");

		return true;
	}

	void stop(Device *provider) override
	{
		(void)provider;
	};

    private:
	irq_object irqobj_;
	dpc dpc_;
	uint8_t gsi_ = -1;
	uint16_t data_port_ = -1;
	uint16_t cmd_port_ = -1;

	struct spinlock buf_lock;
	struct ringbuffer buf_;

	bool ctrl = false;
	bool alt = false;
	bool shifted = false;
};

IO_OBJ_DEFINE(Ps2Kbd, Device);

AcpiPersonality ps2kbdPers = AcpiPersonality(&Ps2Kbd::classInfo, "PNP0303");

void ps2_register()
{
	auto &reg = IoRegistry::getRegistry();
	//reg.dumpTree();
	reg.registerPersonality(&ps2kbdPers);
}

INIT_ENTAILS(ps2_drv);
INIT_DEPS(ps2_drv, io_stage);
INIT_NODE(ps2_drv, ps2_register);
