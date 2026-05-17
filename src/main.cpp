#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

int main(void)
{
	LOG_INF("=== BLE & NFC Emulator Firmware ===");
	LOG_INF("Board : %s", CONFIG_BOARD);
	LOG_INF("Zephyr: %s", KERNEL_VERSION_STRING);

	return 0;
}