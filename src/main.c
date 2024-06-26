/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <soc.h>

// sht3x
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
struct sensor_value temp, press, humidity;
#define SHT31_I2C_ADDRESS 0x44 // SHT31 的 I2C 地址

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <bluetooth/services/lbs.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define RUN_STATUS_LED DK_LED1
#define CON_STATUS_LED DK_LED2
#define RUN_LED_BLINK_INTERVAL 1000

#define USER_LED DK_LED3

#define USER_BUTTON DK_BTN1_MSK

static bool app_button_state;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LBS_VAL),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err)
	{
		printk("Connection failed (err %u)\n", err);
		return;
	}

	printk("Connected\n");

	dk_set_led_on(CON_STATUS_LED);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason %u)\n", reason);

	dk_set_led_off(CON_STATUS_LED);
}

#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
static void security_changed(struct bt_conn *conn, bt_security_t level,
							 enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err)
	{
		printk("Security changed: %s level %u\n", addr, level);
	}
	else
	{
		printk("Security failed: %s level %u err %d\n", addr, level,
			   err);
	}
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_LBS_SECURITY_ENABLED)
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed conn: %s, reason %d\n", addr, reason);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif

static void app_led_cb(bool led_state)
{
	dk_set_led(USER_LED, led_state);
}

static bool app_button_cb(void)
{
	return app_button_state;
}

static struct bt_lbs_cb lbs_callbacs = {
	.led_cb = app_led_cb,
	.button_cb = app_button_cb,
};

static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	if (has_changed & USER_BUTTON)
	{
		uint32_t user_button_state = button_state & USER_BUTTON;

		bt_lbs_send_button_state(user_button_state);
		app_button_state = user_button_state ? true : false;
	}
}

static int init_button(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err)
	{
		printk("Cannot init buttons (err: %d)\n", err);
	}

	return err;
}

void main(void)
{
	int16_t temperature, humidity;
	uint8_t read_buf[6]; // 根据传感器的数据格式调整大小
	static const struct device *twi_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
	// const struct device *const dev = DEVICE_DT_GET_ONE(sensirion_sht3xd);
	int rc;
	if (!device_is_ready(twi_dev))
	{
		printk("error. twi_dev is not ready\n");
	}
	else
	{
		printk("twi_dev is ready\n");
	}

	int blink_status = 0;
	int err;

	printk("Starting Bluetooth Peripheral LBS example\n");

	err = dk_leds_init();
	if (err)
	{
		printk("LEDs init failed (err %d)\n", err);
		return;
	}

	err = init_button();
	if (err)
	{
		printk("Button init failed (err %d)\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_BT_LBS_SECURITY_ENABLED))
	{
		err = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (err)
		{
			printk("Failed to register authorization callbacks.\n");
			return;
		}

		err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (err)
		{
			printk("Failed to register authorization info callbacks.\n");
			return;
		}
	}

	err = bt_enable(NULL);
	if (err)
	{
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS))
	{
		settings_load();
	}

	err = bt_lbs_init(&lbs_callbacs);
	if (err)
	{
		printk("Failed to init LBS (err:%d)\n", err);
		return;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
						  sd, ARRAY_SIZE(sd));
	if (err)
	{
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");

	for (;;)
	{
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		// k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL)); // you can try changing this value
		//  rc = sensor_sample_fetch(twi_dev);
		uint8_t cmd[] = {0x24, 0x00}; // 高重复性，时钟拉低
		uint8_t rx_buf[6];			  // 接收缓冲区

		// 发送测量命令
		if (i2c_write(twi_dev, cmd, sizeof(cmd), SHT31_I2C_ADDRESS) != 0)
		{
			printk("Failed to write measurement command\n");
			return;
		}

		// 等待测量完成
		k_sleep(K_MSEC(20)); // 等待足够的测量时间

		// 读取测量结果
		if (i2c_read(twi_dev, rx_buf, sizeof(rx_buf), SHT31_I2C_ADDRESS) != 0)
		{
			printk("Failed to read data\n");
			return;
		}

		// 计算并显示温度和湿度
		int temp = (rx_buf[0] << 8) | rx_buf[1];
		int humidity = (rx_buf[3] << 8) | rx_buf[4];

		printk("Temperature: %.2f C\n", -45 + (175 * temp / 65535.0));
		printk("Humidity: %.2f %%RH\n", 100 * humidity / 65535.0);
		// printk("Temperature: %d.%06d C\n", temp.val1, temp.val2);
		// printk("Humidity: %d.%06d %%RH\n", humidity.val1, humidity.val2);
		//  k_msleep(delay_time);
	}
}
