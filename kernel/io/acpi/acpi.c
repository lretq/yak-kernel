#include <uacpi/uacpi.h>
#include <uacpi/event.h>
#include <yak/log.h>
#include <yak/init.h>
#include <yak/status.h>
#include <yio/acpi.h>
#include <yio/acpi/event.h>

void acpi_init()
{
	uacpi_status uret = uacpi_initialize(0);
	if (uacpi_unlikely_error(uret)) {
		pr_error("uacpi_initialize error: %s\n",
			 uacpi_status_to_string(uret));
		goto err;
	}

	uret = uacpi_namespace_load();
	if (uacpi_unlikely_error(uret)) {
		pr_error("uacpi_namespace_load error: %s\n",
			 uacpi_status_to_string(uret));
		goto err;
	}

	uret = uacpi_namespace_initialize();
	if (uacpi_unlikely_error(uret)) {
		pr_error("uacpi_namespace_initialize: %s\n",
			 uacpi_status_to_string(uret));
		goto err;
	}

	acpi_init_events();

	uret = uacpi_finalize_gpe_initialization();
	if (uacpi_unlikely_error(uret)) {
		pr_error("uacpi_finalize_gpe_initialization: %s\n",
			 uacpi_status_to_string(uret));
		goto err;
	}

	pr_info("acpi: init done\n");
	return;

err:
	panic("ACPI failed to initialize.");
}

INIT_STAGE(acpi);
INIT_ENTAILS(acpi, acpi);
INIT_DEPS(acpi, pci_access_stage);
INIT_NODE(acpi, acpi_init);
