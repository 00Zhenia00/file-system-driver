#include <iostream>
#include <Windows.h> 

#define MOUNT_DISK_CTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define UNMOUNT_DISK_CTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define MAX_DISK_NAME_LENGTH 64

SC_HANDLE schSCManager, schService;

LPCWSTR service_name = L"FileSystemDriver";

void LoadDriver() {
    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager)
    {
        printf("open service manager failed: %d\n", GetLastError());
        return;
    }
    const char* partialPath = "utility.exe";
    char utility_path[_MAX_PATH];
    std::string path;
    if (_fullpath(utility_path, partialPath, _MAX_PATH) != NULL)
        path = utility_path;
    path = path.substr(0, path.find("Utility\\x64\\Debug\\utility.exe")) + "FileSystemDriver\\x64\\Debug\\FileSystemDriver.sys";
    std::wstring stemp = std::wstring(path.begin(), path.end());
    LPCWSTR driver_path = stemp.c_str();
    schService = CreateService(schSCManager,
        service_name,
        service_name,
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        driver_path, // path to sys file
        NULL,
        NULL,
        NULL,
        NULL,
        NULL);
    if (!schService)
    {
        if (GetLastError() != ERROR_SERVICE_EXISTS)
        {
            printf("create service failed: %d\n", GetLastError());
            CloseServiceHandle(schSCManager);
            return;
        }
    }
    schService = OpenService(schSCManager, service_name, GENERIC_ALL);
    if (!schService)
    {
        printf("open service failed: %d\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }
    if (!StartService(
        schService,  // handle to service 
        0,           // number of arguments 
        NULL))      // no arguments 
    {
        printf("StartService failed (%d)\n", GetLastError());
        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
        return;
    }
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

void UnloadDriver() {
    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager)
    {
        printf("open service manager failed: %d\n", GetLastError());
        return;
    }
    schService = OpenService(schSCManager, service_name, SC_MANAGER_ALL_ACCESS);
    if (!schService)
    {
        printf("OpenService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }
    SERVICE_STATUS k_Status = { 0 };
    if (!ControlService(schService, SERVICE_CONTROL_STOP, &k_Status)) {
        printf("close service failed: %d\n", GetLastError());
        CloseServiceHandle(schSCManager);
        CloseServiceHandle(schService);
        return;
    }

    if (!DeleteService(schService))
    {
        printf("DeleteService failed (%d)\n", GetLastError());
    }
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

void MountDisk(WCHAR* message) {
    HANDLE device_handle = CreateFile(L"\\\\.\\MountDeviceLink", GENERIC_ALL, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM, 0);

    if (device_handle == INVALID_HANDLE_VALUE || device_handle == NULL) {
        std::cout << "ERROR: failed to open Control Device!" << "\n";
        exit(1);
    }

    std::cout << "Opened Control Device" << "\n";

    WCHAR output[1024] = { 0 };
    ULONG returnLength = 0;

    if (DeviceIoControl(device_handle, MOUNT_DISK_CTL, message, (wcslen(message) + 1) * 2, output, 1024, &returnLength, 0)) {
        std::cout << "disk mounted" << "\n";

        if (!DefineDosDevice(DDD_RAW_TARGET_PATH, message, output)) {
            std::cout << "ERROR: Failed to create symbolic link!\n";
            DWORD word = GetLastError();
            std::cout << word << std::endl;
        }

    }
    else {
        if (device_handle != INVALID_HANDLE_VALUE && device_handle != NULL) {
            CloseHandle(device_handle);
        }
        std::cout << "ERROR: disk not mounted" << "\n";
        exit(1);
    }
    if (device_handle != INVALID_HANDLE_VALUE && device_handle != NULL) {
        CloseHandle(device_handle);
    }
}

void UnmountDisk(WCHAR* message) {
    HANDLE device_handle = CreateFile(L"\\\\.\\MountDeviceLink", GENERIC_ALL, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM, 0);

    if (device_handle == INVALID_HANDLE_VALUE || device_handle == NULL) {
        std::cout << "ERROR: failed to open Control Device!" << "\n";
        exit(1);
    }
    ULONG returnLength = 0;
    if (device_handle != INVALID_HANDLE_VALUE && device_handle != NULL) {
        if (!DeviceIoControl(device_handle, UNMOUNT_DISK_CTL, message, (wcslen(message) + 1) * 2, NULL, 0, &returnLength, 0)) {
            std::cout << "ERROR: disk not unmounted" << "\n";
            exit(1);
        }
        else {
            DefineDosDevice(DDD_REMOVE_DEFINITION, message, NULL);
            std::cout << "disk unmounted" << "\n";
        }
    }
    if (device_handle != INVALID_HANDLE_VALUE && device_handle != NULL) {
        CloseHandle(device_handle);
    }
}

int main(int argc, char** argv)
{
    if (argc > 3) {
        std::cout << "too many commands\n";
    }
    if (argc == 1) {
        std::cout << "write command\n";
    }
    if (argc == 2) {
        std::string command = argv[1];
        if (command == "load") {
            std::cout << "load driver\n";
            LoadDriver();
        }
        else if (command == "unload") {
            std::cout << "unload driver\n";
            UnloadDriver();
        }
    }
    if (argc == 3) {
        std::string command = argv[1];
        std::string disk_name = argv[2];
        if ((command == "mount") || (command == "unmount")) {
            std::wstring stemp = std::wstring(disk_name.begin(), disk_name.end());
            WCHAR buffer[MAX_DISK_NAME_LENGTH];
            std::size_t length = stemp.copy(buffer, MAX_DISK_NAME_LENGTH - 1, 0);
            buffer[length] = '\0';
            if (command == "mount") {
                std::cout << "mount " << disk_name << "\n";
                MountDisk(buffer);
            }
            else {
                std::cout << "unmount " << disk_name << "\n";
                UnmountDisk(buffer);
            }
        }
    }
    return 0;
}