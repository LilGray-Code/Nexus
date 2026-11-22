#include "netdev.h"
#include <stddef.h>
struct netdev_entry {
 char name[NEXUS_NETDEV_NAME_MAX + 1U];
 const struct nexus_net_device_ops *ops;
 void *context;
};
static struct netdev_entry devices[NEXUS_NETDEV_MAX_DEVICES];
static uint32_t device_count;
static uint32_t default_device;
bool netdev_register(const char *name, const struct nexus_net_device_ops *ops, void *context) {
 if (name == NULL || name[0] == '\0' || ops == NULL || ops->get_mac == NULL ||
     ops->send_frame == NULL || ops->receive_frame == NULL ||
     device_count >= NEXUS_NETDEV_MAX_DEVICES) return false;
 size_t length = 0U;
 while (length <= NEXUS_NETDEV_NAME_MAX && name[length] != '\0') ++length;
 if (length == 0U || length > NEXUS_NETDEV_NAME_MAX) return false;
 for (uint32_t i = 0U; i < device_count; ++i) {
  size_t j = 0U;
  while (j < length && devices[i].name[j] == name[j]) ++j;
  if (j == length && devices[i].name[j] == '\0') return false;
 }
 struct netdev_entry *entry = &devices[device_count];
 for (size_t i = 0U; i < length; ++i) entry->name[i] = name[i];
 entry->name[length] = '\0';
 entry->ops = ops;
 entry->context = context;
 ++device_count;
 return true;
}
uint32_t netdev_device_count(void) { return device_count; }
const char *netdev_device_name_at(uint32_t i) {
 return i < device_count ? devices[i].name : NULL;
}
bool netdev_set_default(uint32_t i) {
 if (i >= device_count) return false;
 default_device = i;
 return true;
}
uint32_t netdev_default_index(void) { return default_device; }
bool netdev_get_mac_at(uint32_t i, uint8_t address[6]) {
 return i < device_count && address != NULL &&
        devices[i].ops->get_mac(devices[i].context, address);
}
bool netdev_send_frame_at(uint32_t i, const void *frame, size_t length) {
 if (i >= device_count || frame == NULL || length < NEXUS_NETDEV_MIN_FRAME_SIZE ||
     length > NEXUS_NETDEV_MAX_FRAME_SIZE) return false;
 return devices[i].ops->send_frame(devices[i].context, frame, length);
}
bool netdev_receive_frame_at(uint32_t i, void *buffer, size_t capacity, size_t *length) {
 if (length != NULL) *length = 0U;
 if (i >= device_count || buffer == NULL || length == NULL ||
     capacity < NEXUS_NETDEV_MIN_FRAME_SIZE) return false;
 if (!devices[i].ops->receive_frame(devices[i].context, buffer, capacity, length)) {
  *length = 0U;
  return false;
 }
 if (*length < NEXUS_NETDEV_MIN_FRAME_SIZE || *length > NEXUS_NETDEV_MAX_FRAME_SIZE ||
     *length > capacity) {
  *length = 0U;
  return false;
 }
 return true;
}
bool netdev_ready(void) { return device_count != 0U && default_device < device_count; }
const char *netdev_name(void) { return netdev_ready() ? devices[default_device].name : NULL; }
bool netdev_get_mac(uint8_t address[6]) {
 return netdev_ready() && netdev_get_mac_at(default_device, address);
}
bool netdev_send_frame(const void *frame, size_t length) {
 return netdev_ready() && netdev_send_frame_at(default_device, frame, length);
}
bool netdev_receive_frame(void *buffer, size_t capacity, size_t *length) {
 if (!netdev_ready()) { if (length != NULL) *length = 0U; return false; }
 return netdev_receive_frame_at(default_device, buffer, capacity, length);
}
