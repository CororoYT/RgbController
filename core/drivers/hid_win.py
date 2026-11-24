"""
core/drivers/hid_win.py – Accesso nativo Windows HID (SetupAPI + hid.dll + kernel32.dll)
Zero dipendenze esterne.
"""
from __future__ import annotations
import ctypes
from ctypes import wintypes
from dataclasses import dataclass
from typing import Optional

setupapi = ctypes.windll.setupapi
hid = ctypes.windll.hid
kernel32 = ctypes.windll.kernel32


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", wintypes.DWORD),
        ("Data2", wintypes.WORD),
        ("Data3", wintypes.WORD),
        ("Data4", ctypes.c_ubyte * 8),
    ]


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("InterfaceClassGuid", GUID),
        ("Flags", wintypes.DWORD),
        ("Reserved", ctypes.c_void_p),
    ]


class HIDD_ATTRIBUTES(ctypes.Structure):
    _fields_ = [
        ("Size", wintypes.ULONG),
        ("VendorID", wintypes.USHORT),
        ("ProductID", wintypes.USHORT),
        ("VersionNumber", wintypes.USHORT),
    ]


class HIDP_CAPS(ctypes.Structure):
    _fields_ = [
        ("Usage", wintypes.USHORT),
        ("UsagePage", wintypes.USHORT),
        ("InputReportByteLength", wintypes.USHORT),
        ("OutputReportByteLength", wintypes.USHORT),
        ("FeatureReportByteLength", wintypes.USHORT),
        ("Reserved", wintypes.USHORT * 17),
        ("NumberLinkCollectionNodes", wintypes.USHORT),
        ("NumberInputButtonCaps", wintypes.USHORT),
        ("NumberInputValueCaps", wintypes.USHORT),
        ("NumberInputDataIndices", wintypes.USHORT),
        ("NumberOutputButtonCaps", wintypes.USHORT),
        ("NumberOutputValueCaps", wintypes.USHORT),
        ("NumberOutputDataIndices", wintypes.USHORT),
        ("NumberFeatureButtonCaps", wintypes.USHORT),
        ("NumberFeatureValueCaps", wintypes.USHORT),
        ("NumberFeatureDataIndices", wintypes.USHORT),
    ]


setupapi.SetupDiGetClassDevsW.restype = wintypes.HANDLE
setupapi.SetupDiDestroyDeviceInfoList.argtypes = [wintypes.HANDLE]
setupapi.SetupDiEnumDeviceInterfaces.argtypes = [
    wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(GUID), wintypes.DWORD, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA)
]
setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [
    wintypes.HANDLE, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA), ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p
]
kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.CreateFileW.argtypes = [
    wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE
]
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.WriteFile.argtypes = [
    wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p
]
hid.HidD_GetAttributes.argtypes = [wintypes.HANDLE, ctypes.POINTER(HIDD_ATTRIBUTES)]
hid.HidD_GetPreparsedData.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
hid.HidP_GetCaps.argtypes = [ctypes.c_void_p, ctypes.POINTER(HIDP_CAPS)]
hid.HidD_FreePreparsedData.argtypes = [ctypes.c_void_p]
hid.HidD_SetFeature.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.ULONG]
hid.HidD_GetFeature.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.ULONG]

INVALID_HANDLE_VALUE = wintypes.HANDLE(-1).value


@dataclass
class HIDDeviceInfo:
    vid: int
    pid: int
    usage_page: int
    usage: int
    in_len: int
    out_len: int
    feat_len: int
    path: str


def enumerate_hid() -> list[HIDDeviceInfo]:
    results: list[HIDDeviceInfo] = []
    hid_guid = GUID()
    hid.HidD_GetHidGuid(ctypes.byref(hid_guid))
    h_dev_info = setupapi.SetupDiGetClassDevsW(ctypes.byref(hid_guid), None, None, 0x12)
    if h_dev_info == INVALID_HANDLE_VALUE:
        return results

    try:
        iface_data = SP_DEVICE_INTERFACE_DATA()
        iface_data.cbSize = ctypes.sizeof(SP_DEVICE_INTERFACE_DATA)
        idx = 0
        while setupapi.SetupDiEnumDeviceInterfaces(h_dev_info, None, ctypes.byref(hid_guid), idx, ctypes.byref(iface_data)):
            idx += 1
            req = wintypes.DWORD(0)
            setupapi.SetupDiGetDeviceInterfaceDetailW(h_dev_info, ctypes.byref(iface_data), None, 0, ctypes.byref(req), None)
            if req.value == 0:
                continue
            buf = ctypes.create_string_buffer(req.value)
            ctypes.cast(buf, ctypes.POINTER(wintypes.DWORD))[0] = 8
            if setupapi.SetupDiGetDeviceInterfaceDetailW(h_dev_info, ctypes.byref(iface_data), buf, req.value, None, None):
                path = ctypes.wstring_at(ctypes.addressof(buf) + 4)
                h = kernel32.CreateFileW(path, 0, 3, None, 3, 0, None)
                if h != INVALID_HANDLE_VALUE:
                    try:
                        attr = HIDD_ATTRIBUTES()
                        attr.Size = ctypes.sizeof(HIDD_ATTRIBUTES)
                        if hid.HidD_GetAttributes(h, ctypes.byref(attr)):
                            pp = ctypes.c_void_p()
                            if hid.HidD_GetPreparsedData(h, ctypes.byref(pp)):
                                caps = HIDP_CAPS()
                                hid.HidP_GetCaps(pp, ctypes.byref(caps))
                                hid.HidD_FreePreparsedData(pp)
                                results.append(HIDDeviceInfo(
                                    vid=attr.VendorID,
                                    pid=attr.ProductID,
                                    usage_page=caps.UsagePage,
                                    usage=caps.Usage,
                                    in_len=caps.InputReportByteLength,
                                    out_len=caps.OutputReportByteLength,
                                    feat_len=caps.FeatureReportByteLength,
                                    path=path,
                                ))
                    finally:
                        kernel32.CloseHandle(h)
    finally:
        setupapi.SetupDiDestroyDeviceInfoList(h_dev_info)
    return results


def open_hid_rw(path: str) -> Optional[int]:
    h = kernel32.CreateFileW(path, 0xC0000000, 3, None, 3, 0, None)
    if h == INVALID_HANDLE_VALUE:
        return None
    return h


def close_hid(handle: int) -> None:
    if handle and handle != INVALID_HANDLE_VALUE:
        kernel32.CloseHandle(handle)


def set_feature(handle: int, data: bytes) -> bool:
    buf = (ctypes.c_ubyte * len(data))(*data)
    return bool(hid.HidD_SetFeature(handle, buf, len(data)))


def get_feature(handle: int, report_id: int, length: int) -> Optional[bytes]:
    buf = (ctypes.c_ubyte * length)()
    buf[0] = report_id
    if hid.HidD_GetFeature(handle, buf, length):
        return bytes(buf)
    return None


def write_output(handle: int, data: bytes) -> bool:
    buf = (ctypes.c_ubyte * len(data))(*data)
    written = wintypes.DWORD(0)
    return bool(kernel32.WriteFile(handle, buf, len(data), ctypes.byref(written), None))
