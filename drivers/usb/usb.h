#ifndef KOLIBRIARM_DRIVERS_USB_USB_H
#define KOLIBRIARM_DRIVERS_USB_USB_H

#include <stdint.h>

/*
 * USB 2.0 descriptor types. The kernel only parses what it needs to
 * find and configure an HID-class device: device, configuration,
 * interface, endpoint, and HID class descriptors.
 */
#define USB_DESC_DEVICE                    0x01U
#define USB_DESC_CONFIGURATION             0x02U
#define USB_DESC_STRING                     0x03U
#define USB_DESC_INTERFACE                  0x04U
#define USB_DESC_ENDPOINT                   0x05U
#define USB_DESC_HID                        0x21U
#define USB_DESC_HID_REPORT                 0x22U
#define USB_DESC_HID_PHYSICAL               0x23U

/* Standard control request codes (subset). */
#define USB_REQ_GET_STATUS                  0x00U
#define USB_REQ_CLEAR_FEATURE               0x01U
#define USB_REQ_SET_FEATURE                 0x03U
#define USB_REQ_SET_ADDRESS                 0x05U
#define USB_REQ_GET_DESCRIPTOR               0x06U
#define USB_REQ_SET_DESCRIPTOR               0x07U
#define USB_REQ_GET_CONFIGURATION            0x08U
#define USB_REQ_SET_CONFIGURATION            0x09U
#define USB_REQ_GET_INTERFACE               0x0AU
#define USB_REQ_SET_INTERFACE               0x0BU

/* HID class requests. */
#define USB_HID_GET_REPORT                  0x01U
#define USB_HID_SET_IDLE                    0x0AU
#define USB_HID_SET_PROTOCOL                0x0BU
#define USB_HID_BOOT_PROTOCOL               0x00U
#define USB_HID_REPORT_PROTOCOL             0x01U

/* Standard descriptor field helpers (little-endian). */
#define USB_DESC_TYPE(b)                    ((b)[1])
#define USB_DESC_LEN(b)                     ((b)[0])

/* Endpoint transfer types. */
#define USB_EP_TRANSFER_CONTROL             0x00U
#define USB_EP_TRANSFER_ISOCHRONOUS         0x01U
#define USB_EP_TRANSFER_BULK                0x02U
#define USB_EP_TRANSFER_INTERRUPT           0x03U

/* Class codes. */
#define USB_CLASS_HID                       0x03U

/* Setup packet layout. */
typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_t;

/* Standard device descriptor (18 bytes). */
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

/* Standard configuration descriptor (9 bytes, header only). */
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;

/* Standard interface descriptor (9 bytes). */
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;

/* Standard endpoint descriptor (7 bytes). */
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

/* HID class descriptor (minimal). */
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdHID;
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    uint8_t bReportDescriptorType;
    uint16_t wReportDescriptorLength;
} __attribute__((packed)) usb_hid_descriptor_t;

#endif