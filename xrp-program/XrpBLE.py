import bluetooth
import struct
from micropython import const

_IRQ_CENTRAL_CONNECT = const(1)
_IRQ_CENTRAL_DISCONNECT = const(2)
_IRQ_GATTS_WRITE = const(3)
_IRQ_GATTS_READ_REQUEST = const(4)
_IRQ_MTU_EXCHANGED = const(21)
_IRQ_CONNECTION_UPDATE = const(27)

_ADV_TYPE_SHORTENED_LOCAL_NAME = const(0x08)
_ADV_TYPE_COMPLETE_LOCAL_NAME  = const(0x09)
_ADV_TYPE_FLAGS = const(0x01)

_ADV_FLAG_LE_GENERAL_DISCOVERABLE_MODE = const(1 << 1)
_ADV_FLAG_BR_EDR_NOT_SUPPORTED         = const(1 << 2)

class BleReceiver:
    def __init__(self, name: str, uuid, write_handler):
        ble = bluetooth.BLE()
        self.ble = ble
        self.write_handler = write_handler

        ble.active(True)
        mac = ble.config('mac')
        mtu = ble.config('mtu')
        print(f"mac={mac[1].hex()}, mtu={mtu}")
        ble.config(gap_name=name)
        ble.irq(self.ble_irq)

        service_uuid = bluetooth.UUID(uuid)
        rx_characterisitc = (service_uuid, bluetooth.FLAG_WRITE)
        service = (service_uuid, (rx_characterisitc,))
        ((rx_handle,),) = ble.gatts_register_services((service,))

        adv_data = (
            struct.pack("BB", 1 + len(name), _ADV_TYPE_COMPLETE_LOCAL_NAME) + name +
            struct.pack("BBB", 1 + 1, _ADV_TYPE_FLAGS, _ADV_FLAG_LE_GENERAL_DISCOVERABLE_MODE | _ADV_FLAG_BR_EDR_NOT_SUPPORTED)
        )
        ble.gap_advertise(500000, adv_data=adv_data)

        print("Ble started:")
        print(f"rx_handle = {rx_handle}")

    def ble_irq(self, event, data):
        if event == _IRQ_CENTRAL_CONNECT:
            # A central has connected to this peripheral.
            conn_handle, addr_type, addr = data
            print(f"[BLE] Central connected: conn_handle={conn_handle}, addr_type={addr_type}, addr={addr}")

        elif event == _IRQ_CENTRAL_DISCONNECT:
            # A central has disconnected from this peripheral.
            conn_handle, addr_type, addr = data
            print(f"[BLE] Central disconnected: conn_handle={conn_handle}, addr_type={addr_type}, addr={addr}")

        elif event == _IRQ_GATTS_WRITE:
            # A client has written to this characteristic or descriptor.
            conn_handle, attr_handle = data
            print(f"[BLE] value written: conn_handle={conn_handle}, attr_handle={attr_handle}")

            res = self.ble.gatts_read(attr_handle)
            print(f"Read result: {res}")
            self.write_handler(res)

        elif event == _IRQ_GATTS_READ_REQUEST:
            # A client has issued a read. Note: this is only supported on STM32.
            # Return a non-zero integer to deny the read (see below), or zero (or None)
            # to accept the read.
            conn_handle, attr_handle = data
            print(f"[BLE] Gatt server read request: conn_handle={conn_handle}, attr_handle={attr_handle}")

        elif event == _IRQ_MTU_EXCHANGED:
            # ATT MTU exchange complete (either initiated by us or the remote device).
            conn_handle, mtu = data
            print(f"[BLE] Mtu exchanged: conn_handle={conn_handle}, mtu={mtu}")
            # ble.config(mtu=mtu)

        elif event == _IRQ_CONNECTION_UPDATE:
            # The remote device has updated connection parameters.
            conn_handle, conn_interval, conn_latency, supervision_timeout, status = data
            print(f"[BLE] Connection update: conn_handle={conn_handle}, conn_interval={conn_interval}, conn_latency={conn_latency}, supervision_timeout={supervision_timeout}, status={status}")

        else:
            print(f"[BLE] Got event: {event}, data: {data}")
