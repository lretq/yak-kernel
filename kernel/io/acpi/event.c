#define pr_fmt(fmt) "acpi: " fmt

#include <yak/dpc.h>
#include <yak/log.h>
#include <yak/status.h>
#include <yio/acpi/sleep.h>
#include <uacpi/event.h>

static struct dpc shutdown_dpc;

// TODO: replace with kicking off shutdown thread?
static void shutdown_cb([[maybe_unused]] struct dpc *dpc,
			[[maybe_unused]] void *arg)
{
	EXPECT(acpi_shutdown());
}

/*
 * This handler will be called by uACPI from an interrupt context,
 * whenever a power button press is detected.
 */
static uacpi_interrupt_ret
handle_power_button([[maybe_unused]] uacpi_handle ctx)
{
	dpc_enqueue(&shutdown_dpc, NULL);
	return UACPI_INTERRUPT_HANDLED;
}

static status_t power_button_init(void)
{
	dpc_init(&shutdown_dpc, shutdown_cb);

	uacpi_status ret = uacpi_install_fixed_event_handler(
		UACPI_FIXED_EVENT_POWER_BUTTON, handle_power_button,
		UACPI_NULL);
	if (uacpi_unlikely_error(ret)) {
		pr_error("failed to install power button event callback: %s",
			 uacpi_status_to_string(ret));
		return YAK_NOENT;
	}

	return YAK_SUCCESS;
}

void acpi_init_events()
{
	power_button_init();
	pr_info("registered events\n");
}
