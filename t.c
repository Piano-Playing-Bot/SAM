#define AIL_ALL_IMPL
#include "ail.h"
#include "libusb.h"
#include <stdio.h>
#include <string.h>

typedef struct USB {
	libusb_device *device;
	u8 device_addr;
	u8 endpoint_addr;
} USB;

AIL_DA_INIT(USB);

int main(void)
{
	libusb_init_context(NULL, NULL, 0);
	libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_DEBUG);

	// discover devices
	libusb_device **list;
	ssize_t n = libusb_get_device_list(NULL, &list);
	int ret;
	printf("n: %lld\n", n);

	if (n < 0) {
		printf("Unexpected Error: Count=%lld\n", n);
		goto exit;
	}

	AIL_DA(USB) usbs = ail_da_new(USB);

	for (ssize_t i = 0; i < n; i++) {
		libusb_device *device = list[i];
		struct libusb_device_descriptor desc;
		libusb_get_device_descriptor(device, &desc);

		struct libusb_config_descriptor *config;
		ret = libusb_get_config_descriptor(device, 0, &config);
		if (LIBUSB_SUCCESS != ret) {
			printf("  Couldn't retrieve descriptors\n");
			continue;
		}

		// @Study: Should we loop through all configurations too?
		if (config->bNumInterfaces >= 2) {
			for (uint8_t j = 0; j < config->bNumInterfaces; j++) {
				const struct libusb_interface *interface = &config->interface[j];
				for (int k = 0; k < interface->num_altsetting; k++) {
					const struct libusb_interface_descriptor *altsetting = &interface->altsetting[k];
					uint8_t clz = altsetting->bInterfaceClass;
					if (clz == 10) {
						for (uint8_t l = 0; l < altsetting->bNumEndpoints; l++) {
							const struct libusb_endpoint_descriptor *endpoint = &altsetting->endpoint[l];
							enum libusb_endpoint_direction dir = endpoint->bEndpointAddress & 0x80;
							enum libusb_endpoint_transfer_type type = endpoint->bmAttributes & 0x3;
							if (type == LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK && dir == LIBUSB_ENDPOINT_OUT) {
								USB usb = { device, libusb_get_device_address(device), endpoint->bEndpointAddress };
								ail_da_push(&usbs, usb);
							}
						}
					}
				}
			}
		}

		libusb_free_config_descriptor(config);
	}

	for (u32 i = 0; i < usbs.len; i++) {
		USB usb = usbs.data[i];
		libusb_device_handle *handle = NULL;

		if (usb.device_addr != libusb_get_device_address(usb.device)) continue;
		printf("Dev (bus %u, device %u):\n", libusb_get_bus_number(usb.device), usb.device_addr);

		// struct libusb_config_descriptor *config;
		// ret = libusb_get_config_descriptor(usb.device, 0, &config);
		// AIL_ASSERT(LIBUSB_SUCCESS == ret);

		ret = libusb_open(usb.device, &handle);
		if (ret != LIBUSB_SUCCESS) {
			printf("Couldn't open device (%d): %s\n", ret, libusb_strerror(ret));
			continue;
		}

		char *data = "Hello World!";
		i32 transferred_len = 0;
		u32 timeout = 2000;
		ret = libusb_bulk_transfer(handle, usb.endpoint_addr, (u8 *)data, strlen(data), &transferred_len, timeout);
		if (ret != LIBUSB_SUCCESS) {
			printf("Couldn't write to device (%d): %s\n", ret, libusb_strerror(ret));
			continue;
		}

		printf("transferred_len: %d\n", transferred_len);

		if (handle) libusb_close(handle);
	}

	libusb_free_device_list(list, 1);

exit:
	libusb_exit(NULL);
	return 0;
}