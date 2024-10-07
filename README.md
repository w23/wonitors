# Why this repo?
Once upon a time I had an impossible to ignore urge to read EDIDs of all connected displays on Windows. In bare C and WinAPI only.

Expectation: There's a function like `GetEdidForMonitor(HMONITOR)`

Reality: One does not simply. There are no officially documented ways to do this, no fully working code examples, and no, LLMs couldn't conjure one up either (although they did provide valuable research pointers).

## A brief sequence of events
1. Call `EnumDisplayMonitors()` (масло масляное) to receive a bunch of `HMONITOR` handles for each connected display.
2. `GetMonitorInfo()` to get `MONITORINFOEX`. `szDevice` will contain display device name like `\\.\DISPLAY14`.
3. Use `EnumDisplayDevices(szDevice, EDD_GET_DEVICE_INTERFACE_NAME)` to get `DISPLAY_DEVICE.DeviceID` containing device path like `\\?\DISPLAY#LKG001A#5&272c5422&0&UID513#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}`
4. Use `SetupDiGetClassDevs(GUID_DEVINTERFACE_MONITOR, DIGCF_DEVICEINTERFACE)` to request a set of all monitor-class devices interfaces.
5. Enumerate the set using `SetupDiEnumDeviceInterfaces(set, GUID_DEVINTERFACE_MONITOR, index)`, getting `SP_DEVICE_INTERFACE_DATA`
6. For each interface retrieve `SP_INTERFACE_DEVICE_DETAIL_DATA` using `SetupDiGetDeviceInterfaceDetail(set, iface_data)`. It's a three-step process. First, get the detail struct size, then allocate it, then call the function again to fill the detail data. Also, use this function to read `SP_DEVINFO_DATA`.
7. Compare `detail.DevicePath` with the device path `DISPLAY_DEVICE.DeviceID` from step 3. Note that they will have different case, so case-insensitive comparison is needed. If they match, then you've found associated interface for the `HMONITOR` from step 1.
8. Use `SetupDiOpenDevRegKey(set, devinfo_data)` to open registry key. (Yes, the regedit-registry. No, it's not practical to guess the key, it's something like `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\DISPLAY\CMN152A\5&272c5422&0&UID512\Device Parameters` and seems unstable)
9. Use `RegQueryValueEx(key, "EDID")` to read EDID value data into buffer of 128 bytes. No, it won't read past 128 bytes to get extensions, e.g. for CEC.
10. If nothing failed, you're done. Time to cleanup all this mess. Maybe the real treasure was all the handles we created and memory we allocated along the way.

## Bonus One
The example code also reads monitor positions and current modes, so that there's association between EDID and virtual display geometry.

No, I haven't found an easy read monitor names (e.g. vendor, model). I haven't really looked, though.

## Bonus Two
This repo contains some cmake configuration to manifest this app into a DPI-aware world. Without this, the bonus one code would read dpi-unaware (scaled) monitor positions, which might be not what you want.

## Appendix
How to read EDID in X11:
1. `XRRGetScreenResources()` to get resources.
2. For each `resources->output[i]`: use `XRRGetOutputProperty(output, XInternAtom("EDID"))`, done.
