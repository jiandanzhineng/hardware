#ifndef DEVICE_BLE_SERVICE_H
#define DEVICE_BLE_SERVICE_H

void device_ble_service_init(void);
void device_ble_service_deinit(void);
void device_ble_update_property(int index);
void device_ble_send_message(const char *message);

#endif
