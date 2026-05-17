#include "i2c_scanner.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i2c_scanner, CONFIG_LOG_DEFAULT_LEVEL);

#define I2C_SCAN_NODE DT_ALIAS(i2c0)

/*
 * I2C diagnostic pin selection:
 *   - Smart Tag board: uses DT nodes i2c_diag_sda / i2c_diag_scl (P1.10/P1.11)
 *   - DK fallback:     hardcoded gpio1 pins 7/6 (P1.07/P1.06)
 */
#if DT_NODE_EXISTS(DT_NODELABEL(i2c_diag_sda)) && \
    DT_NODE_EXISTS(DT_NODELABEL(i2c_diag_scl))

static const struct gpio_dt_spec diag_sda =
    GPIO_DT_SPEC_GET(DT_NODELABEL(i2c_diag_sda), gpios);
static const struct gpio_dt_spec diag_scl =
    GPIO_DT_SPEC_GET(DT_NODELABEL(i2c_diag_scl), gpios);

static void log_bus_line_state(void)
{
    if (!gpio_is_ready_dt(&diag_sda) || !gpio_is_ready_dt(&diag_scl)) {
        LOG_WRN("I2C diag GPIO device not ready");
        return;
    }

    (void)gpio_pin_configure_dt(&diag_sda, GPIO_INPUT);
    (void)gpio_pin_configure_dt(&diag_scl, GPIO_INPUT);

    int sda = gpio_pin_get_dt(&diag_sda);
    int scl = gpio_pin_get_dt(&diag_scl);

    if (sda < 0 || scl < 0) {
        LOG_WRN("Failed to read I2C line level(s): SDA=%d SCL=%d", sda, scl);
        return;
    }

    LOG_INF("I2C line state before scan: SDA=%d SCL=%d", sda, scl);

    if (sda == 0 || scl == 0) {
        LOG_WRN("I2C bus line stuck low (check pull-ups/short/wiring)");
    }
}

#elif DT_NODE_HAS_STATUS(DT_NODELABEL(gpio1), okay)
/* Fallback: hardcoded DK pins (P1.07 SDA, P1.06 SCL) */
static const struct device *const diag_gpio = DEVICE_DT_GET(DT_NODELABEL(gpio1));

static void log_bus_line_state(void)
{
    if (!device_is_ready(diag_gpio)) {
        LOG_WRN("I2C diag GPIO device not ready");
        return;
    }

    (void)gpio_pin_configure(diag_gpio, 7, GPIO_INPUT);
    (void)gpio_pin_configure(diag_gpio, 6, GPIO_INPUT);

    int sda = gpio_pin_get(diag_gpio, 7);
    int scl = gpio_pin_get(diag_gpio, 6);

    if (sda < 0 || scl < 0) {
        LOG_WRN("Failed to read I2C line level(s): SDA=%d SCL=%d", sda, scl);
        return;
    }

    LOG_INF("I2C line state before scan: SDA=%d SCL=%d", sda, scl);

    if (sda == 0 || scl == 0) {
        LOG_WRN("I2C bus line stuck low (check pull-ups/short/wiring)");
    }
}

#else
static void log_bus_line_state(void)
{
    LOG_WRN("I2C line diagnostics disabled (no diag pins available)");
}
#endif

#if DT_NODE_HAS_STATUS(I2C_SCAN_NODE, okay)

static constexpr uint16_t I2C_SCAN_ADDR_FIRST = 0x08;
static constexpr uint16_t I2C_SCAN_ADDR_LAST  = 0x77;
static constexpr int I2C_SCAN_ADDR_COUNT = I2C_SCAN_ADDR_LAST - I2C_SCAN_ADDR_FIRST + 1;

static int probe_address(const struct device *bus, uint16_t addr)
{
    uint8_t dummy = 0;
    /* Zero-length write probe keeps scanner lightweight for boot diagnostics. */
    return i2c_write(bus, &dummy, 0U, addr);
}

#endif

int i2c_scanner_run_once(void)
{
#if !DT_NODE_HAS_STATUS(I2C_SCAN_NODE, okay)
    LOG_ERR("I2C alias 'i2c0' is missing or disabled in devicetree");
    return -ENODEV;
#else
    const struct device *bus = DEVICE_DT_GET(I2C_SCAN_NODE);
    int found = 0;
    int timeout_like = 0;
    int nack_like = 0;
    int other_err = 0;

    if (!device_is_ready(bus)) {
        LOG_ERR("I2C bus '%s' is not ready", bus->name);
        return -ENODEV;
    }

    log_bus_line_state();
    LOG_INF("I2C scan start on %s", bus->name);

    const uint32_t start_ms = k_uptime_get_32();

    for (uint16_t addr = I2C_SCAN_ADDR_FIRST; addr <= I2C_SCAN_ADDR_LAST; ++addr) {
        const int rc = probe_address(bus, addr);

        if (rc == 0) {
            LOG_INF("I2C device found at 0x%02X", addr);
            ++found;
        } else if (rc == -ENXIO || rc == -EADDRNOTAVAIL) {
            ++nack_like;
        } else if (rc == -EIO || rc == -ETIMEDOUT) {
            ++timeout_like;
        } else {
            ++other_err;
        }
    }

    LOG_INF("I2C scan complete: %d device(s) found in %u ms",
            found, (unsigned int)(k_uptime_get_32() - start_ms));

    if (found == 0) {
        const int total_errors = timeout_like + nack_like + other_err;

        if (total_errors == I2C_SCAN_ADDR_COUNT) {
            LOG_INF("No responder detected across all addresses (expected when no device is connected)");
        } else {
            LOG_WRN("No I2C ACK received. Check SDA/SCL wiring, GND, and 3.3V pull-ups.");
        }

        LOG_INF("Probe errors: nack=%d timeout_or_bus=%d other=%d",
                nack_like, timeout_like, other_err);
    }

    return found;
#endif
}
