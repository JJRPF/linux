// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Passive diagnostics for the SEP driver: the "diag" attribute group on the SEP
 * platform device. Unattended test tools use it to see how far the bring-up
 * got without reading the kernel log, which may be restricted.
 *
 * Each attribute shows state that the driver caches here at the point where it
 * makes the decision. Reading one only loads that cached state: it sends
 * nothing to the SEP, takes no driver lock and starts no work, so a reader can
 * neither stall nor disturb the bring-up. Nothing here identifies a user, an
 * identity or a key.
 *
 * A machine has one SEP and the driver attaches to it at most once per boot,
 * so the state is static rather than per device. Every field starts at zero,
 * the state before the driver has decided anything.
 */
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/minmax.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#include "shim.h"

/* The contract version; see Documentation/ABI/testing/sysfs-driver-apple-sep. */
#define SEP_DIAG_ABI			1

#define SEP_DIAG_ATTACH_PENDING		0
#define SEP_DIAG_ATTACH_ATTACHED	1
#define SEP_DIAG_ATTACH_FAILED		2

#define SEP_DIAG_KEYSTORE_UNKNOWN	0
#define SEP_DIAG_KEYSTORE_OPEN		1
#define SEP_DIAG_KEYSTORE_CLOSED	2

/* The last bring-up of the bound sensor; reset when a sensor binds. */
#define SEP_DIAG_SENSOR_UNTRIED		0
#define SEP_DIAG_SENSOR_ONLINE		1
#define SEP_DIAG_SENSOR_FAILED		2

/*
 * Touch ID is activated by the first open of /dev/sep-bio, not by the boot-time
 * bring-up, so publishing the node leaves the outcome undecided until then.
 */
#define SEP_DIAG_TOUCHID_PENDING	0
#define SEP_DIAG_TOUCHID_PUBLISHED	1
#define SEP_DIAG_TOUCHID_UNAVAILABLE	2
#define SEP_DIAG_TOUCHID_STARTED	3
#define SEP_DIAG_TOUCHID_FAILED		4

static struct {
	/* Set at probe, before the group is added. */
	char profile[32];
	bool cold;
	bool sepos13;
	bool xart;

	atomic_t attach;
	atomic_t endpoints;
	atomic_t keystore;
	atomic_t keybag;
	atomic_t sensor_bound;
	atomic_t sensor_result;
	atomic_t touchid;
} sep_diag;

static bool sep_diag_sensor_online(void)
{
	return atomic_read(&sep_diag.sensor_bound) &&
	       atomic_read(&sep_diag.sensor_result) == SEP_DIAG_SENSOR_ONLINE;
}

static ssize_t abi_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sysfs_emit(buf, "%d\n", SEP_DIAG_ABI);
}
static DEVICE_ATTR_RO(abi);

static ssize_t profile_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sysfs_emit(buf, "%s\n", sep_diag.profile);
}
static DEVICE_ATTR_RO(profile);

static ssize_t boot_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "%s\n", sep_diag.cold ? "cold" : "warm");
}
static DEVICE_ATTR_RO(boot);

static ssize_t protocol_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return sysfs_emit(buf, "%s\n", sep_diag.sepos13 ? "sepos13" : "variant5");
}
static DEVICE_ATTR_RO(protocol);

static ssize_t xart_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "%s\n", sep_diag.xart ? "enabled" : "disabled");
}
static DEVICE_ATTR_RO(xart);

static ssize_t attach_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	const char *state;

	switch (atomic_read(&sep_diag.attach)) {
	case SEP_DIAG_ATTACH_ATTACHED:
		state = "attached";
		break;
	case SEP_DIAG_ATTACH_FAILED:
		state = "failed";
		break;
	default:
		state = "pending";
		break;
	}
	return sysfs_emit(buf, "%s\n", state);
}
static DEVICE_ATTR_RO(attach);

/* Discovery goes on after the attach, so the count can still grow. */
static ssize_t endpoints_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	unsigned int n = 0;

	if (atomic_read(&sep_diag.attach) == SEP_DIAG_ATTACH_ATTACHED)
		n = atomic_read(&sep_diag.endpoints);
	return sysfs_emit(buf, "%u\n", n);
}
static DEVICE_ATTR_RO(endpoints);

static ssize_t keystore_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	const char *state;

	switch (atomic_read(&sep_diag.keystore)) {
	case SEP_DIAG_KEYSTORE_OPEN:
		state = "open";
		break;
	case SEP_DIAG_KEYSTORE_CLOSED:
		state = "closed";
		break;
	default:
		state = "unknown";
		break;
	}
	return sysfs_emit(buf, "%s\n", state);
}
static DEVICE_ATTR_RO(keystore);

static ssize_t keybag_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	const char *state;

	switch (atomic_read(&sep_diag.keybag)) {
	case SEP_DIAG_KEYBAG_PRESENT:
		state = "present";
		break;
	case SEP_DIAG_KEYBAG_MISSING:
		state = "missing";
		break;
	case SEP_DIAG_KEYBAG_FAILED:
		state = "failed";
		break;
	default:
		state = "unknown";
		break;
	}
	return sysfs_emit(buf, "%s\n", state);
}
static DEVICE_ATTR_RO(keybag);

static ssize_t sensor_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	int result = atomic_read(&sep_diag.sensor_result);
	const char *state;

	if (!atomic_read(&sep_diag.sensor_bound))
		state = "unbound";
	else if (result == SEP_DIAG_SENSOR_ONLINE)
		state = "online";
	else if (result == SEP_DIAG_SENSOR_FAILED)
		state = "failed";
	else
		state = "bound";
	return sysfs_emit(buf, "%s\n", state);
}
static DEVICE_ATTR_RO(sensor);

static ssize_t touchid_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	const char *state;

	switch (atomic_read(&sep_diag.touchid)) {
	case SEP_DIAG_TOUCHID_STARTED:
		state = sep_diag_sensor_online() ? "ready" : "not-ready";
		break;
	case SEP_DIAG_TOUCHID_UNAVAILABLE:
	case SEP_DIAG_TOUCHID_FAILED:
		state = "not-ready";
		break;
	default:
		state = "unknown";
		break;
	}
	return sysfs_emit(buf, "%s\n", state);
}
static DEVICE_ATTR_RO(touchid);

static struct attribute *sep_diag_attrs[] = {
	&dev_attr_abi.attr,
	&dev_attr_profile.attr,
	&dev_attr_boot.attr,
	&dev_attr_protocol.attr,
	&dev_attr_xart.attr,
	&dev_attr_attach.attr,
	&dev_attr_endpoints.attr,
	&dev_attr_keystore.attr,
	&dev_attr_keybag.attr,
	&dev_attr_sensor.attr,
	&dev_attr_touchid.attr,
	NULL
};

static const struct attribute_group sep_diag_group = {
	.name = "diag",
	.attrs = sep_diag_attrs,
};

/*
 * The fixed facts are stored before the group is added, so no reader sees them
 * unset. devres removes the group when the driver unbinds.
 */
int sep_diag_register(struct device *dev, const char *profile,
		      size_t profile_len, bool cold, bool sepos13, bool xart)
{
	profile_len = min(profile_len, sizeof(sep_diag.profile) - 1);
	memcpy(sep_diag.profile, profile, profile_len);
	sep_diag.profile[profile_len] = '\0';
	sep_diag.cold = cold;
	sep_diag.sepos13 = sepos13;
	sep_diag.xart = xart;

	return devm_device_add_group(dev, &sep_diag_group);
}

void sep_diag_set_attach(bool attached)
{
	atomic_set(&sep_diag.attach,
		   attached ? SEP_DIAG_ATTACH_ATTACHED : SEP_DIAG_ATTACH_FAILED);
}

void sep_diag_set_endpoints(unsigned int count)
{
	atomic_set(&sep_diag.endpoints, count);
}

void sep_diag_set_keystore_open(void)
{
	atomic_set(&sep_diag.keystore, SEP_DIAG_KEYSTORE_OPEN);
}

void sep_diag_set_keybag(int state)
{
	atomic_set(&sep_diag.keybag, state);
}

void sep_diag_set_sensor_bound(bool bound)
{
	if (bound)
		atomic_set(&sep_diag.sensor_result, SEP_DIAG_SENSOR_UNTRIED);
	atomic_set(&sep_diag.sensor_bound, bound);
}

void sep_diag_set_sensor_result(bool online)
{
	atomic_set(&sep_diag.sensor_result,
		   online ? SEP_DIAG_SENSOR_ONLINE : SEP_DIAG_SENSOR_FAILED);
}

/*
 * A compare-exchange, not a store: an open of the new node can activate Touch
 * ID before the publisher gets here, and that outcome must stand.
 */
void sep_diag_set_bio_published(void)
{
	atomic_cmpxchg(&sep_diag.touchid, SEP_DIAG_TOUCHID_PENDING,
		       SEP_DIAG_TOUCHID_PUBLISHED);
}

void sep_diag_set_touchid(bool started)
{
	atomic_set(&sep_diag.touchid,
		   started ? SEP_DIAG_TOUCHID_STARTED : SEP_DIAG_TOUCHID_FAILED);
}

/*
 * Only the boot-time bring-up opens the key store and publishes /dev/sep-bio,
 * and it is not repeated. Whatever it left undecided is final for this boot:
 * the key store stays closed and Touch ID cannot start.
 */
void sep_diag_bringup_ended(void)
{
	atomic_cmpxchg(&sep_diag.keystore, SEP_DIAG_KEYSTORE_UNKNOWN,
		       SEP_DIAG_KEYSTORE_CLOSED);
	atomic_cmpxchg(&sep_diag.touchid, SEP_DIAG_TOUCHID_PENDING,
		       SEP_DIAG_TOUCHID_UNAVAILABLE);
}
