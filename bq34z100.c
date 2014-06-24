/*
 * BQ27x00 battery driver
 *
 * Copyright (C) 2008 Rodolfo Giometti <giometti@linux.it>
 * Copyright (C) 2008 Eurotech S.p.A. <info@eurotech.it>
 * Copyright (C) 2010-2011 Lars-Peter Clausen <lars@metafoo.de>
 * Copyright (C) 2011 Pali Roh��r <pali.rohar@gmail.com>
 *
 * Based on a previous work by Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

/*
 * Datasheets:
 * http://focus.ti.com/docs/prod/folders/print/bq27000.html
 * http://focus.ti.com/docs/prod/folders/print/bq27500.html
 * http://www.ti.com/product/bq27425-g1
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <asm/unaligned.h>


#define DRIVER_VERSION			"1.2.0"

/*changed for bq34z100*/

/* normal commands*/
#define BQ27x00_REG_CTRL		0x00 /*Control() */
#define BQ27x00_REG_SOC			0x02 /*StateOfCharge() */
#define BQ27x00_REG_RM			0x04 /*RemainingCapacity() */
#define BQ27x00_REG_FCC			0x06 /*FullChargeCapacity() */
#define BQ27x00_REG_VOLT		0x08 /*Voltage() */
#define BQ27x00_REG_AI			0x0A /*AverageCurrent() */
#define BQ27x00_REG_TEMP		0x0C /*Temperature() */
#define BQ27x00_REG_FLAGS		0x0E /*Flags() */

#define BQ27x00_REG_DATE		0x6B /*Mfr Date */
#define BQ27x00_REG_NAMEL		0x6D /*0X6D Mfr Name Length */
#define BQ27x00_REG_NAME		0x6E /* 0X6E - 0X78 Mfr Name */
#define BQ27x00_REG_CHEML		0x79 /* 0X79 Device Chemistry Length */
#define BQ27x00_REG_CHEM		0x7A /*0X7A - 0X7D Device Chemistry */
#define BQ27x00_REG_SERNUM		0x7E /*Serial Number */


/*extended commands*/
#define BQ27x00_REG_AR			0x10 /*AtRate() */
#define BQ27x00_REG_ARTTE		0x12 /*AtRateTimeToEmpty() */
#define BQ27x00_REG_NAC			0x14 /* Nominal available capacity */
#define BQ27x00_REG_FAC			0x16 /*FullAvailableCapacity() */
#define BQ27x00_REG_TTE			0x18 /*TimeToEmpty() */
#define BQ27x00_REG_TTF			0x1A /*TimeToFull() */
#define BQ27x00_REG_SI			0x1C /*StandbyCurrent() */
#define BQ27x00_REG_STTE		0x1E /*StandbyTimeToEmpty() */
#define BQ27x00_REG_MLI			0x20 /*MaxLoadCurrent() */
#define BQ27x00_REG_MLTTE		0x22 /*MaxLoadTimeToEmpty() */
#define BQ27x00_REG_AE			0x24 /* Available energy */
#define BQ27x00_REG_AP			0x26 /*AveragePower() */
#define BQ27x00_REG_TTECP		0x28 /*TTEatConstantPower() */
#define BQ27x00_REG_INTTEMP		0x2A /*Internal_Temp() */
#define BQ27x00_REG_CYCT		0x2C /* Cycle count total */
#define BQ27x00_REG_SOH			0x2E /*StateOfHealth() */
#define BQ27x00_REG_CHGV		0x30 /*ChargeVoltage() */
#define BQ27x00_REG_CHGI		0x32 /*ChargeCurrent() */
#define BQ27x00_REG_PCHG		0x34 /*PassedCharge */
#define BQ27x00_REG_DCAP		0x3C /* Design capacity */

/* flags bit definitions */
#define BQ27x00_FLAG_DSG			BIT(0) /* Discharging detected. True when set. */
#define BQ27x00_FLAG_SOCF			BIT(1) /* State-of-Charge Threshold Final reached. True when set. */
#define BQ27x00_FLAG_SOC1			BIT(2) /* State-of-Charge Threshold 1 reached. True when set. */
#define BQ27x00_FLAG_TDD			BIT(5) /* Tab Disconnect is detected. True when set. */
#define BQ27x00_FLAG_ISD			BIT(6) /* Internal Short is detected. True when set. */
#define BQ27x00_FLAG_OCVTAKEN			BIT(7) /* Cleared on entry to relax mode and set to 1 when OCV measurement is performed in relax mode. */
#define BQ27x00_FLAG_CHG			BIT(8) /* (Fast) charging allowed. True when set. */
#define BQ27x00_FLAG_FC				BIT(9) /* Full-charge is detected.True when set. */
#define BQ27x00_FLAG_CHG_INH			BIT(11) /* Charge Inhibit: unable to begin charging [Charge Inhibit Temp Low, Charge Inhibit Temp High]. True when set. */
#define BQ27x00_FLAG_BATLOW			BIT(12) /* Battery Low bit that indicates a low battery voltage condition. */
#define BQ27x00_FLAG_BATHIGH			BIT(13) /* Battery High bit that indicates a high battery voltage condition. */
#define BQ27x00_FLAG_OTD			BIT(14) /* Over-Temperature in Discharge condition is detected. True when set. */
#define BQ27x00_FLAG_OTC			BIT(15) /* Over-Temperature in Charge condition is detected. True when set. */


#define BQ27000_FLAG_CI			BIT(4) /* Capacity Inaccurate flag */

/*control command*/
#define CONTROL_CMD			BQ27x00_REG_CTRL

/*Control subcommand*/
#define DEV_TYPE_SUBCMD                 0x0001
#define FW_VER_SUBCMD                   0x0002
#define DF_VER_SUBCMD                   0x000C
#define ITENABLE_SUBCMD                 0x0021
#define RESET_SUBCMD                    0x0041


#define BQ27000_RS			20 /* Resistor sense */
#define BQ27x00_POWER_CONSTANT		(256 * 29200 / 1000)

struct bq27x00_device_info;
struct bq27x00_access_methods {
	int (*read)(struct bq27x00_device_info *di, u8 reg, bool single);
        int (*write)(struct bq27x00_device_info *di, u8 reg, u16 value,
                     bool single);
};

enum bq27x00_chip { BQ27000, BQ27500, BQ27425, BQ34Z100 };

struct bq27x00_reg_cache {
	int temperature;
	int time_to_empty;
	int time_to_empty_avg;
	int time_to_full;
	int charge_full;
	int cycle_count;
	int capacity;
	int energy;
	int flags;
	int power_avg;
	int health;
};

struct bq27x00_device_info {
	struct device 		*dev;
	int			id;
	enum bq27x00_chip	chip;

	struct bq27x00_reg_cache cache;
	int charge_design_full;

	unsigned long last_update;
	struct delayed_work work;

	struct power_supply	bat;

	struct bq27x00_access_methods bus;

	struct mutex lock;
};

static enum power_supply_property bq27x00_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
#if 0
	POWER_SUPPLY_PROP_CYCLE_COUNT,
#endif
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
};


static unsigned int poll_interval = 60;
module_param(poll_interval, uint, 0644);
MODULE_PARM_DESC(poll_interval, "battery poll interval in seconds - " \
				"0 disables polling");

/*
 * Common code for BQ27x00 devices
 */

static inline int bq27x00_read(struct bq27x00_device_info *di, u8 reg,
		bool single)
{
	return di->bus.read(di, reg, single);
}

static inline int bq27x00_write(struct bq27x00_device_info *di, u8 reg,
                u16 value, bool single)
{
        return di->bus.write(di, reg, value, single);
}


/*
 * Return the battery State-of-Charge for bq34z100
 * Or < 0 if something fails.
 */
static int bq27x00_battery_read_soc(struct bq27x00_device_info *di)
{
	int soc;

	soc = bq27x00_read(di, BQ27x00_REG_SOC, false);

	if (soc < 0)
		dev_dbg(di->dev, "error reading State-of-Charge\n");

	return soc;
}

/*
 * Return a battery charge value in uAh
 * Or < 0 if something fails.
 */
static int bq27x00_battery_read_charge(struct bq27x00_device_info *di, u8 reg)
{
	int charge;

	charge = bq27x00_read(di, reg, false);
	if (charge < 0) {
		dev_dbg(di->dev, "error reading charge register %02x: %d\n",
			reg, charge);
		return charge;
	}

	charge *= 1000;

	return charge;
}

/*
 * Return the battery Nominal available capaciy in uAh
 * Or < 0 if something fails.
 */
static inline int bq27x00_battery_read_nac(struct bq27x00_device_info *di)
{

	return bq27x00_battery_read_charge(di, BQ27x00_REG_NAC);
}

/*
 * Return the battery Last measured discharge in uAh
 * Or < 0 if something fails.
 */
static inline int bq27x00_battery_read_fcc(struct bq27x00_device_info *di)
{
	return bq27x00_battery_read_charge(di, BQ27x00_REG_FCC);
}

/*
 * Return the battery Initial last measured discharge in uAh
 * Or < 0 if something fails.
 */
static int bq27x00_battery_read_dcap(struct bq27x00_device_info *di)
{
	int ilmd;

	ilmd = bq27x00_read(di, BQ27x00_REG_DCAP, false);

	if (ilmd < 0) {
		dev_dbg(di->dev, "error reading initial last measured discharge\n");
		return ilmd;
	}

	ilmd *= 1000;

	return ilmd;
}

/*
 * Return the battery Available energy in uWh
 * Or < 0 if something fails.
 */
static int bq27x00_battery_read_energy(struct bq27x00_device_info *di)
{
	int ae;

	ae = bq27x00_read(di, BQ27x00_REG_AE, false);
	if (ae < 0) {
		dev_dbg(di->dev, "error reading available energy\n");
		return ae;
	}
		ae *= 1000;

	return ae;
}

/*
 * Return the battery temperature in tenths of degree Kelvin(Unit:0.1K)
 * Or < 0 if something fails.
 */
static int bq27x00_battery_read_temperature(struct bq27x00_device_info *di)
{
	int temp;

	temp = bq27x00_read(di, BQ27x00_REG_TEMP, false);
	if (temp < 0) {
		dev_err(di->dev, "error reading temperature\n");
		return temp;
	}


	return temp;
}

/*
 * Return the battery Cycle count total
 * Or < 0 if something fails.
 */
static int bq27x00_battery_read_cyct(struct bq27x00_device_info *di)
{
	int cyct;

	cyct = bq27x00_read(di, BQ27x00_REG_CYCT, false);
	if (cyct < 0)
		dev_err(di->dev, "error reading cycle count total\n");

	return cyct;
}

/*
 * Read a time register.Unit:second
 * Return < 0 if something fails.
 */
static int bq27x00_battery_read_time(struct bq27x00_device_info *di, u8 reg)
{
	int tval;

	tval = bq27x00_read(di, reg, false);
	if (tval < 0) {
		dev_dbg(di->dev, "error reading time register %02x: %d\n",
			reg, tval);
		return tval;
	}
	/*when the battery is not discharging, tval should be 65535 and should
		be not return error. */
	//if (tval == 65535)
	//	return -ENODATA;

	return tval * 60;
}

/*
 * Read a power avg register.
 * Return < 0 if something fails.
 */
static int bq27x00_battery_read_pwr_avg(struct bq27x00_device_info *di, u8 reg)
{
	int tval;

	tval = bq27x00_read(di, reg, false);
	if (tval < 0) {
		dev_err(di->dev, "error reading power avg rgister  %02x: %d\n",
			reg, tval);
		return tval;
	}

	return tval;
}

/*
 * Read flag register.
 * Return < 0 if something fails.
 */
static int bq27x00_battery_read_health(struct bq27x00_device_info *di)
{
	int tval;

	tval = bq27x00_read(di, BQ27x00_REG_FLAGS, false);
	if (tval < 0) {
		dev_err(di->dev, "error reading flag register:%d\n", tval);
		return tval;
	}

	if (tval & BQ27x00_FLAG_SOCF)
		tval = POWER_SUPPLY_HEALTH_DEAD;
	else if (tval & BQ27x00_FLAG_OTC)
		tval = POWER_SUPPLY_HEALTH_OVERHEAT;
	else
		tval = POWER_SUPPLY_HEALTH_GOOD;

	return tval;

}
/* FIXME:we should take care of the flags here.*/

/*we should put cache here as a gloable variable to share it with proc read function.
 * and may be should add a lock to access it serially.*/
struct bq27x00_reg_cache cache = {0, };

static void bq27x00_update(struct bq27x00_device_info *di)
{

	cache.flags = bq27x00_read(di, BQ27x00_REG_FLAGS, false);
	if (cache.flags >= 0) {
//		if (cache.flags & BQ27000_FLAG_CI) {
		if (0) {
			dev_info(di->dev, "battery is not calibrated! ignoring capacity values\n");
			cache.capacity = -ENODATA;
			cache.energy = -ENODATA;
			cache.time_to_empty = -ENODATA;
			cache.time_to_empty_avg = -ENODATA;
			cache.time_to_full = -ENODATA;
			cache.charge_full = -ENODATA;
			cache.health = -ENODATA;
		} else {
			cache.capacity = bq27x00_battery_read_soc(di);
			cache.energy = bq27x00_battery_read_energy(di);
			cache.time_to_empty = bq27x00_battery_read_time(di,BQ27x00_REG_TTE);
			cache.time_to_empty_avg = bq27x00_battery_read_time(di,BQ27x00_REG_TTECP);
			cache.time_to_full = bq27x00_battery_read_time(di,BQ27x00_REG_TTF);
			cache.charge_full = bq27x00_battery_read_fcc(di);
			cache.health = bq27x00_battery_read_health(di);
		}
		cache.temperature = bq27x00_battery_read_temperature(di);
		cache.cycle_count = bq27x00_battery_read_cyct(di);
		cache.power_avg = bq27x00_battery_read_pwr_avg(di, BQ27x00_REG_AP);

		/* We only have to read charge design full once */
		if (di->charge_design_full <= 0)
			di->charge_design_full = bq27x00_battery_read_dcap(di);
	}

	if (memcmp(&di->cache, &cache, sizeof(cache)) != 0) {
		di->cache = cache;
		power_supply_changed(&di->bat);
	}

	di->last_update = jiffies;
}

static void bq27x00_battery_poll(struct work_struct *work)
{
	struct bq27x00_device_info *di =
		container_of(work, struct bq27x00_device_info, work.work);

	bq27x00_update(di);

	if (poll_interval > 0) {
		/* The timer does not have to be accurate. */
#if 0
		set_timer_slack(&di->work.timer, poll_interval * HZ / 4);
#endif
		schedule_delayed_work(&di->work, poll_interval * HZ);
	}
}

/*
 * Return the battery average current in uA
 * Note that current can be negative signed as well
 * Or 0 if something fails.
 */
static int bq27x00_battery_current(struct bq27x00_device_info *di,
	union power_supply_propval *val)
{
	int curr;

	curr = bq27x00_read(di, BQ27x00_REG_AI, false);
	if (curr < 0) {
		dev_err(di->dev, "error reading current\n");
		return curr;
	}

	val->intval = (int)((s16)curr) * 1000;

	return 0;
}

static int bq27x00_battery_status(struct bq27x00_device_info *di,
	union power_supply_propval *val)
{
	int status;

	if (di->cache.flags & BQ27x00_FLAG_FC)
		status = POWER_SUPPLY_STATUS_FULL;
	else if (di->cache.flags & BQ27x00_FLAG_DSG)
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	else
		status = POWER_SUPPLY_STATUS_CHARGING;

	val->intval = status;

	return 0;
}

static int bq27x00_battery_capacity_level(struct bq27x00_device_info *di,
	union power_supply_propval *val)
{
	int level;

	if (di->cache.flags & BQ27x00_FLAG_FC)
		level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	else if (di->cache.flags & BQ27x00_FLAG_SOC1)
		level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else if (di->cache.flags & BQ27x00_FLAG_SOCF)
		level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	else
		level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;

	val->intval = level;

	return 0;
}

/*
 * Return the battery Voltage in millivolts(Unit:mV/1000)
 * Or < 0 if something fails.
 */
static int bq27x00_battery_voltage(struct bq27x00_device_info *di,
	union power_supply_propval *val)
{
	int volt;

	volt = bq27x00_read(di, BQ27x00_REG_VOLT, false);
	if (volt < 0) {
		dev_err(di->dev, "error reading voltage\n");
		return volt;
	}

	val->intval = volt * 1000;

	return 0;
}

static int bq27x00_simple_value(int value,
	union power_supply_propval *val)
{
	if (value < 0)
		return value;

	val->intval = value;

	return 0;
}

#define to_bq27x00_device_info(x) container_of((x), \
				struct bq27x00_device_info, bat);

static int bq27x00_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	int ret = 0;
	struct bq27x00_device_info *di = to_bq27x00_device_info(psy);

	mutex_lock(&di->lock);
	if (time_is_before_jiffies(di->last_update + 5 * HZ)) {
		cancel_delayed_work_sync(&di->work);
		bq27x00_battery_poll(&di->work.work);
	}
	mutex_unlock(&di->lock);

	if (psp != POWER_SUPPLY_PROP_PRESENT && di->cache.flags < 0)
		return -ENODEV;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = bq27x00_battery_status(di, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = bq27x00_battery_voltage(di, val);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = di->cache.flags < 0 ? 0 : 1;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = bq27x00_battery_current(di, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = bq27x00_simple_value(di->cache.capacity, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		ret = bq27x00_battery_capacity_level(di, val);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = bq27x00_simple_value(di->cache.temperature, val);
		/* change the unit into tenths of degree Celsius(0.1C)*/
		if (ret == 0)
			val->intval -= 2731;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		ret = bq27x00_simple_value(di->cache.time_to_empty, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		ret = bq27x00_simple_value(di->cache.time_to_empty_avg, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		ret = bq27x00_simple_value(di->cache.time_to_full, val);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = bq27x00_simple_value(bq27x00_battery_read_nac(di), val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = bq27x00_simple_value(di->cache.charge_full, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = bq27x00_simple_value(di->charge_design_full, val);
		break;
#if 0
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		ret = bq27x00_simple_value(di->cache.cycle_count, val);
		break;
#endif
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		ret = bq27x00_simple_value(di->cache.energy, val);
		break;
	case POWER_SUPPLY_PROP_POWER_AVG:
		ret = bq27x00_simple_value(di->cache.power_avg, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = bq27x00_simple_value(di->cache.health, val);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static void bq27x00_external_power_changed(struct power_supply *psy)
{
	struct bq27x00_device_info *di = to_bq27x00_device_info(psy);

	cancel_delayed_work_sync(&di->work);
	schedule_delayed_work(&di->work, 0);
}

static int bq27x00_powersupply_init(struct bq27x00_device_info *di)
{
	int ret;

	di->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties = bq27x00_battery_props;
	di->bat.num_properties = ARRAY_SIZE(bq27x00_battery_props);
	di->bat.get_property = bq27x00_battery_get_property;
	di->bat.external_power_changed = bq27x00_external_power_changed;

	INIT_DELAYED_WORK(&di->work, bq27x00_battery_poll);
	mutex_init(&di->lock);

	ret = power_supply_register(di->dev, &di->bat);
	if (ret) {
		dev_err(di->dev, "failed to register battery: %d\n", ret);
		return ret;
	}

	dev_info(di->dev, "support ver. %s enabled\n", DRIVER_VERSION);

	bq27x00_update(di);

	return 0;
}

static void bq27x00_powersupply_unregister(struct bq27x00_device_info *di)
{
	/*
	 * power_supply_unregister call bq27x00_battery_get_property which
	 * call bq27x00_battery_poll.
	 * Make sure that bq27x00_battery_poll will not call
	 * schedule_delayed_work again after unregister (which cause OOPS).
	 */
	poll_interval = 0;

	cancel_delayed_work_sync(&di->work);

	power_supply_unregister(&di->bat);

	mutex_destroy(&di->lock);
}


/* If the system has several batteries we need a different name for each
 * of them...
 */
#if 0
static DEFINE_IDR(battery_id);
static DEFINE_MUTEX(battery_mutex);
#endif

static int bq27x00_read_i2c(struct bq27x00_device_info *di, u8 reg, bool single)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	s32 data = 0;

	if (!client->adapter)
		return -ENODEV;

	if (single)
		data = i2c_smbus_read_byte_data(client, reg);
	else
		data = i2c_smbus_read_word_data(client, reg);
	
	if (data < 0)
		return -EIO;

	return data;
}

static int bq27x00_write_i2c(struct bq27x00_device_info *di, u8 reg, u16 value, bool single)
{
        struct i2c_client *client = to_i2c_client(di->dev);
        int ret;

	if (!client->adapter)
                return -ENODEV;

        if (single)
                ret = i2c_smbus_write_byte_data(client, reg, value);
        else
                ret = i2c_smbus_write_word_data(client, reg, value);

        if (ret < 0)
                return -EIO;

	return 0;

}

static int bq27x00_battery_reset(struct bq27x00_device_info *di)
{

         dev_info(di->dev, "Gas Gauge Reset\n");
 
         if(bq27x00_write(di, BQ27x00_REG_CTRL, RESET_SUBCMD, false) < 0)
		dev_err(di->dev, "Gas Gauge Reset error.\n");
 
         msleep(10);
 
         return 0;
}


static int bq27x00_battery_enable_it(struct bq27x00_device_info *di)
{

         dev_info(di->dev, "Goint to enable IT.\n");
 
         if(bq27x00_write(di, BQ27x00_REG_CTRL, ITENABLE_SUBCMD, false) < 0)
		dev_err(di->dev, "IT enable error.\n");
 
         msleep(10);
 
         return 0;
}

static int bq27x00_battery_read_fw_version(struct bq27x00_device_info *di)
{
	bq27x00_write_i2c(di, CONTROL_CMD, FW_VER_SUBCMD, false);

	msleep(10);

	return bq27x00_read_i2c(di, CONTROL_CMD, false);
}

static int bq27x00_battery_read_device_type(struct bq27x00_device_info *di)
{
	bq27x00_write_i2c(di, CONTROL_CMD, DEV_TYPE_SUBCMD, false);

	msleep(10);

	return bq27x00_read_i2c(di, CONTROL_CMD, false);
}

static int bq27x00_battery_read_dataflash_version(struct bq27x00_device_info *di)
{
	bq27x00_write_i2c(di, CONTROL_CMD, DF_VER_SUBCMD, false);

	msleep(10);

	return bq27x00_read_i2c(di, CONTROL_CMD, false);
}

static ssize_t show_firmware_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bq27x00_device_info *di = dev_get_drvdata(dev);
	int ver;

	ver = bq27x00_battery_read_fw_version(di);

	return sprintf(buf, "%d\n", ver);
}

static ssize_t show_dataflash_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bq27x00_device_info *di = dev_get_drvdata(dev);
	int ver;

	ver = bq27x00_battery_read_dataflash_version(di);

	return sprintf(buf, "%d\n", ver);
}

static ssize_t show_device_type(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bq27x00_device_info *di = dev_get_drvdata(dev);
	int dev_type;

	dev_type = bq27x00_battery_read_device_type(di);

	return sprintf(buf, "%d\n", dev_type);
}

static ssize_t show_reset(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bq27x00_device_info *di = dev_get_drvdata(dev);

	bq27x00_battery_reset(di);

	return sprintf(buf, "okay\n");
}


static ssize_t show_it_enable(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bq27x00_device_info *di = dev_get_drvdata(dev);

	bq27x00_battery_enable_it(di);

	return sprintf(buf, "it enabled\n");
}

static DEVICE_ATTR(fw_version, S_IRUGO, show_firmware_version, NULL);
static DEVICE_ATTR(df_version, S_IRUGO, show_dataflash_version, NULL);
static DEVICE_ATTR(device_type, S_IRUGO, show_device_type, NULL);
static DEVICE_ATTR(reset, S_IRUGO, show_reset, NULL);
static DEVICE_ATTR(it_enable, S_IRUGO, show_it_enable, NULL);

static struct attribute *bq27x00_attributes[] = {
	&dev_attr_fw_version.attr,
	&dev_attr_df_version.attr,
	&dev_attr_device_type.attr,
	&dev_attr_reset.attr,
	&dev_attr_it_enable.attr,
	NULL
};

static const struct attribute_group bq27x00_attr_group = {
	.attrs = bq27x00_attributes,
};

static int bq27x00_battery_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	char *name;
	struct bq27x00_device_info *di;
	int num;
	int retval = 0;

#if 0
	/* Get new ID for the new battery device */
	mutex_lock(&battery_mutex);
	num = idr_alloc(&battery_id, client, 0, 0, GFP_KERNEL);
	mutex_unlock(&battery_mutex);
#else
	num=0;
#endif

	if (num < 0)
		return num;

	name = kasprintf(GFP_KERNEL, "%s-%d", id->name, num);
	if (!name) {
		dev_err(&client->dev, "failed to allocate device name\n");
		retval = -ENOMEM;
		goto batt_failed_1;
	}

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		retval = -ENOMEM;
		goto batt_failed_2;
	}

	di->id = num;
	di->dev = &client->dev;
	di->chip = id->driver_data;
	di->bat.name = name;
	di->bus.read = &bq27x00_read_i2c;
	di->bus.write = &bq27x00_write_i2c;

	retval = bq27x00_powersupply_init(di);
	if (retval)
		goto batt_failed_3;

	i2c_set_clientdata(client, di);
/*
	bq27x00_battery_reset(di);
	msleep(10);
	bq27x00_battery_enable_it(di);
	msleep(10);
*/	

	dev_info(&client->dev,
			"Gas Guage fw version 0x%04x; df version 0x%04x\n",
			bq27x00_battery_read_fw_version(di), bq27x00_battery_read_dataflash_version(di));

	retval = sysfs_create_group(&client->dev.kobj, &bq27x00_attr_group);
	if (retval)
		dev_err(&client->dev, "could not create sysfs files\n");



	return 0;

batt_failed_3:
	kfree(di);
batt_failed_2:
	kfree(name);
batt_failed_1:

#if 0
	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, num);
	mutex_unlock(&battery_mutex);
#endif

	return retval;
}

static int bq27x00_battery_remove(struct i2c_client *client)
{
	struct bq27x00_device_info *di = i2c_get_clientdata(client);

	bq27x00_powersupply_unregister(di);

	kfree(di->bat.name);

#if 0
	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, di->id);
	mutex_unlock(&battery_mutex);
#endif

	kfree(di);

	return 0;
}
static const char *health_str[] = {
	"Dead",
	"Overheat",
	"Good"
};

static const char *status_str[] = {
	"Full",
	"Discharging",
	"Charging",
	"No Battery",
	"Battery",
	"AC"
};

static int
bbu_read_proc(char *buffer, char **start, off_t offset, int size, int *eof,
                void *data)
{
        int len = 0; /* Don't include the null byte. */
	char *p = buffer;
	int health = 0,status = 0;

/*bq34z100 is powered by battery,so when battery is absent,the communication with bq34z100
 * will be error and cache.flags will be set a negative value in bq27x00_read_i2c fuction. */
	if (cache.flags >= 0) {
		if (cache.flags & BQ27x00_FLAG_SOCF)
		//	health = POWER_SUPPLY_HEALTH_DEAD;
			health = 0;
		else if (cache.flags & (BQ27x00_FLAG_OTC | BQ27x00_FLAG_OTD))
		//	health = POWER_SUPPLY_HEALTH_OVERHEAT;
			health = 1;
		else
		//	health = POWER_SUPPLY_HEALTH_GOOD;
			health = 2;

		if (cache.flags & BQ27x00_FLAG_FC)
		//	status = POWER_SUPPLY_STATUS_FULL;
		//	status = 0;
			status = 5;
		else if (cache.flags & BQ27x00_FLAG_DSG)
		//	status = POWER_SUPPLY_STATUS_DISCHARGING;
		//	status = 0;
			status = 4;
		else
		//	status = POWER_SUPPLY_STATUS_CHARGING;
		//	status = 0;
			status = 5;
	}
	else
		status = 3;

    	p += sprintf(p,
                     "Manufacturer:\t Kedacom\n"
                     "SN:\t\t 2593SMP001\n"
                     "Technology:\t Li-ion\n"
                     "Health:\t\t %s\n"
                     "Temperature:\t %d.%d\n"
                     "Level:\t\t %d\%\n"
                     "TimeRemaining:\t %ds\n"
		     "Status:\t\t %s\n"
                     "DataToFlush:\t 100M\n",
		      health_str[health],
		      (cache.temperature-2731)/10,
		      (cache.temperature-2731)%10,
		      cache.capacity,
		      cache.time_to_empty,
		      status_str[status]);

        len = p - buffer;

        /*
         * We only support reading the whole string at once.
         */
        if (size < len)
                return -EINVAL;
        /*
         * If file position is non-zero, then assume the string has
         * been read and indicate there is no more data to be read.
         */
        if (offset != 0)
                return 0;
        /*
         * We know the buffer is big enough to hold the string.
         */
        /*
         * Signal EOF.
         */
        *eof = 1;

        return len;

}


static const struct i2c_device_id bq27x00_id[] = {
	{ "bq27200", BQ27000 },	/* bq27200 is same as bq27000, but with i2c */
	{ "bq27500", BQ27500 },
	{ "bq27425", BQ27425 },
	{ "bq34z100", BQ34Z100 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq27x00_id);

static struct i2c_driver bq27x00_battery_driver = {
	.driver = {
		.name = "bq34z100",
	},
	.probe = bq27x00_battery_probe,
	.remove = bq27x00_battery_remove,
	.id_table = bq27x00_id,
};

static struct i2c_board_info i2c_board_info[] = {
	{
		I2C_BOARD_INFO("bq34z100", 0x55),
	},
};

static struct i2c_client *client;

static inline int bq27x00_battery_i2c_init(void)
{
	struct i2c_adapter *adapter;
	
	int ret = i2c_add_driver(&bq27x00_battery_driver);
	if (ret)
		printk(KERN_ERR "Unable to register BQ27x00 i2c driver\n");

	adapter = i2c_get_adapter(9);
	if (!adapter)
		return -ENODEV;

	client = i2c_new_device(adapter, i2c_board_info);

	i2c_put_adapter(adapter);

	if (!client)
		return -ENODEV;

        if (create_proc_read_entry("bbu", 0, NULL, bbu_read_proc,
                                    NULL) == 0){
                printk(KERN_ERR
                       "Unable to register \"bbu\" proc file\n");
                
		return -ENOMEM;
        }

	return ret;
}

static inline void bq27x00_battery_i2c_exit(void)
{
	i2c_del_driver(&bq27x00_battery_driver);
	i2c_unregister_device(client);
	remove_proc_entry("bbu", NULL);
	
	printk("BBU driver exit.\n");
}

/*
 * Module stuff
 */

static int __init bq27x00_battery_init(void)
{
	int ret;

	ret = bq27x00_battery_i2c_init();
	if (ret)
		return ret;

	return ret;
}
module_init(bq27x00_battery_init);

static void __exit bq27x00_battery_exit(void)
{
	bq27x00_battery_i2c_exit();
}
module_exit(bq27x00_battery_exit);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("BQ27x00 battery monitor driver");
MODULE_LICENSE("GPL");
