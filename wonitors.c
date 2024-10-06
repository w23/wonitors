#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ShellScalingApi.h> // GetDpiForMonitor() and friends

#include <Setupapi.h> // SetupDiGetClassDevsEx()
#include <devguid.h>	// GUID_DEVCLASS_MONITOR
#include <initguid.h> // Must be before ntddvdeo.h to avoid linking errors (?! wtf)
#include <ntddvdeo.h> // GUID_DEVINTERFACE_MONITOR

#include <stdio.h> // fprintf()

#define LOGF(msg, ...) fprintf(stderr, msg "\n", __VA_ARGS__)

#define COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

static void printError(const char *msg, DWORD error) {
	LPSTR pbuf = NULL;
	const DWORD size = FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&pbuf, 0, NULL
	);
	LOGF("ERROR: %s: code=%#lx(%lu): %.*s", msg, error, error, (int)size, pbuf);
	LocalFree(pbuf);
}

#define printLastError(msg) printError(msg, GetLastError())

static void printDisplayDevice(const char *prefix, const DISPLAY_DEVICE *dd) {
	LOGF("%sDeviceName: %s", prefix, dd->DeviceName);
	LOGF("%sDeviceString: %s", prefix, dd->DeviceString);
	LOGF("%sStateFlags: %#lx", prefix, dd->StateFlags);
	LOGF("%sDeviceID: %s", prefix, dd->DeviceID);
	LOGF("%sDeviceKey: %s", prefix, dd->DeviceKey);
}

typedef struct {
	BYTE data[128];
} EDID;

static BOOL readEdidForDisplayPath(const char *monitor_id, EDID *out_edid) {
	BOOL result = FALSE;

	// Start a query for all monitors device interfaces
	const HDEVINFO devs =
			SetupDiGetClassDevs(&GUID_DEVINTERFACE_MONITOR, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (devs == INVALID_HANDLE_VALUE) {
		printLastError("SetupDiGetClassDevs");
		return FALSE;
	}

	// Iterate through all monitor device interfaces
	for (DWORD iface_index = 0;; iface_index++) {
		// Get device interface data. It's needed in order to get interface detail
		SP_DEVICE_INTERFACE_DATA iface_data = {
				.cbSize = sizeof(iface_data),
		};
		if (!SetupDiEnumDeviceInterfaces(devs, NULL, &GUID_DEVINTERFACE_MONITOR, iface_index, &iface_data))
			break;
		LOGF("    iface[%lu]:", iface_index);

		// Get interface detail. It contains device path string, which is the same
		// as DeviceID for DISPLAY_DEVICE, see EDD_GET_DEVICE_INTERFACE_NAME flag
		// comment below.

		// Interface detail is obtained in two steps:
		// 1. First, read the needed size for the string
		DWORD detail_size = 0;
		if (!SetupDiGetDeviceInterfaceDetail(devs, &iface_data, NULL, 0, &detail_size, NULL)) {
			const DWORD err = GetLastError();
			if (err != ERROR_INSUFFICIENT_BUFFER) {
				printError("SetupDiGetDeviceInterfaceDetail -- get size", err);
				continue;
			}
		}

		// Allocate the structure with the required size.
		LOGF("      detail_size: %lu", detail_size);
		SP_INTERFACE_DEVICE_DETAIL_DATA *detail = malloc(detail_size);
		// Note how cbSize contains the static structure part, not the full payload.
		detail->cbSize = sizeof(*detail);

		// 2. Obtain the detail value fully. Also, obatin SP_DEVINFO_DATA, it is
		// needed to get the registry key
		SP_DEVINFO_DATA dev_data = {
				.cbSize = sizeof(dev_data),
		};
		if (!SetupDiGetDeviceInterfaceDetail(devs, &iface_data, detail, detail_size, NULL, &dev_data)) {
			printLastError("SetupDiGetDeviceInterfaceDetail -- get detail");
			free(detail);
			continue;
		}
		LOGF("      detail: %s", detail->DevicePath);

		const BOOL found = (_stricmp(detail->DevicePath, monitor_id) == 0);

		// Not needed anymore
		free(detail);

		if (!found)
			continue;

		LOGF("      DevicePath matches monitor");

		const HKEY key = SetupDiOpenDevRegKey(devs, &dev_data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
		if (key == INVALID_HANDLE_VALUE) {
			printLastError("SetupDiOpenDevRegKey");
			continue;
		}

		DWORD edid_size = sizeof(*out_edid);
		DWORD reg_type;

		const DWORD read_result = RegQueryValueEx(key, "EDID", NULL, &reg_type, out_edid->data, &edid_size);

		// Not needed anymore
		RegCloseKey(key);

		if (read_result != ERROR_SUCCESS) {
			printError("SetupDiOpenDevRegKey", read_result);
			continue;
		}

		// TODO check edid_size
		// TODO check reg_type

		// Reached here = found edid, may exit
		LOGF("      Read %lu EDID bytes", edid_size);
		result = TRUE;
		break;
	}

	SetupDiDestroyDeviceInfoList(devs);
	return result;
}

static BOOL readEdidFrorDisplayName(LPSTR device_name, EDID *out_edid) {
	// EDD_GET_DEVICE_INTERFACE_NAME specifies that DISPLAY_DEVICE.DeviceID field
	// will contain interface name that is congruent to GUID_DEVINTERFACE_MONITOR
	// detail DevicePath. This is used in readEdidForDisplayPath().
	DISPLAY_DEVICE monitor = {
			.cb = sizeof(monitor),
	};
	if (0 == EnumDisplayDevices(device_name, 0, &monitor, EDD_GET_DEVICE_INTERFACE_NAME)) {
		printLastError("EnumDisplayDevices");
		return FALSE;
	}

	LOGF("  DISPLAY_DEVICE(%s):", device_name);
	printDisplayDevice("    ", &monitor);

	return readEdidForDisplayPath(monitor.DeviceID, out_edid);
}

typedef struct {
	int x, y, w, h;
	int raw_dpi;
	EDID edid;
} DisplayInfo;

static void printDisplayInfo(const DisplayInfo *info) {
	LOGF("#####################################################################");
	LOGF("> FOUND DISPLAY %d,%d %dx%d dpi=%d", info->x, info->y, info->w, info->h, info->raw_dpi);
	const int block_size = 16;
	for (int i = 0; i < COUNTOF(info->edid.data); i += block_size) {
		const BYTE *const b = info->edid.data + i;
		fprintf(stderr, ">  ");
		for (int j = 0; j < block_size; ++j)
			fprintf(stderr, "%02x ", b[j]);
		fprintf(stderr, "| ");
		for (int j = 0; j < block_size; ++j)
			fprintf(stderr, "%c", isprint(b[j]) ? b[j] : '.');
		fprintf(stderr, "\n");
	}
	LOGF("#####################################################################");
}

static BOOL CALLBACK monitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
	LOGF("monitorEnumProc(hMonitor=%p):", hMonitor);
	(void)hdcMonitor;
	(void)lprcMonitor;
	(void)dwData;

	MONITORINFOEX minfo = {
			.cbSize = sizeof(minfo),
	};
	if (!GetMonitorInfo(hMonitor, (LPMONITORINFO)&minfo)) {
		printLastError("GetMonitorInfo");
		return TRUE;
	}

	LOGF("  szDevice: %s", minfo.szDevice);
	LOGF(
			"  rcMonitor: left=%ld top=%ld right=%ld bottom=%ld", minfo.rcMonitor.left, minfo.rcMonitor.top,
			minfo.rcMonitor.right, minfo.rcMonitor.bottom
	);
	LOGF(
			"  rcWork: left=%ld top=%ld right=%ld bottom=%ld", minfo.rcWork.left, minfo.rcWork.top, minfo.rcWork.right,
			minfo.rcWork.bottom
	);
	LOGF("  dwFlags: %#lx%s", minfo.dwFlags, minfo.dwFlags & MONITORINFOF_PRIMARY ? ": PRIMARY" : "");

	DEVMODE dev_mode = {
			.dmSize = sizeof(dev_mode),
	};
	if (!EnumDisplaySettings(minfo.szDevice, ENUM_CURRENT_SETTINGS, &dev_mode)) {
		printLastError("EnumDisplaySettings");
		return TRUE;
	}

	LOGF("  DEVMODE:");
	LOGF("    dmDeviceName: %s", dev_mode.dmDeviceName);
	LOGF("    dmFields: %#lx", dev_mode.dmFields);
	LOGF("    dmPelsWH: %lux%lu", dev_mode.dmPelsWidth, dev_mode.dmPelsHeight);
	LOGF("    dmBitsPerPel: %lu", dev_mode.dmBitsPerPel);
	LOGF("    dmDisplayFrequency: %lu", dev_mode.dmDisplayFrequency);

	UINT raw_dpi_x, raw_dpi_y, eff_dpi_x, eff_dpi_y;
	GetDpiForMonitor(hMonitor, MDT_RAW_DPI, &raw_dpi_x, &raw_dpi_y);
	GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &eff_dpi_x, &eff_dpi_y);
	LOGF("  DPI: effective=(%u, %u), raw=(%u, %u)", eff_dpi_x, eff_dpi_y, raw_dpi_y, raw_dpi_y);

	DisplayInfo info = {
			.x = minfo.rcMonitor.left,
			.y = minfo.rcMonitor.top,
			.w = dev_mode.dmPelsWidth,
			.h = dev_mode.dmPelsHeight,
			.raw_dpi = raw_dpi_x,
	};
	if (!readEdidFrorDisplayName(minfo.szDevice, &info.edid)) {
		LOGF("  Couldn't read EDID for this monitor, oh well");
		return TRUE;
	}

	printDisplayInfo(&info);

	return TRUE;
}

int main() {
	if (!EnumDisplayMonitors(NULL, NULL, monitorEnumProc, 0)) {
		printLastError("Failed to enumerate displays");
		return 1;
	}
	return 0;
}

// References:
// https://stackoverflow.com/questions/34987695/how-can-i-get-an-hmonitor-handle-from-a-display-device-name
// https://ofekshilon.com/2011/11/13/reading-monitor-physical-dimensions-or-getting-the-edid-the-right-way/
// https://stackoverflow.com/questions/18022612/enumerating-monitors-on-a-computer
// https://mariusbancila.ro/blog/2021/05/19/how-to-build-high-dpi-aware-native-desktop-applications/
// https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows
// https://www.aloneguid.uk/posts/2021/02/cmake-dpi-aware/
// https://github.com/Microsoft/Windows-classic-samples/tree/main/Samples/DPIAwarenessPerWindow
