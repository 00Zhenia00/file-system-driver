#include <iostream>
#include <cstring>
#include <windows.h>
#include "winioctl.h"

void GetCharToContinue() {
    char c;
    std::cout << "Enter any symbol to continue executing...\n";
    std::cin >> c;
}

int main()
{
    HANDLE disk_device_handle = NULL;

    GetCharToContinue();

    //
    // Open File/Disk Device
    //
    disk_device_handle = CreateFileW(L"\\\\.\\X:\\file1",
        GENERIC_ALL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        0,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        0);

    if (disk_device_handle == INVALID_HANDLE_VALUE || disk_device_handle == NULL) {
        std::cout << "ERROR: Failed to open Disk Device!" << std::endl;
        DWORD word = GetLastError();
        std::cout << word << std::endl;
        GetCharToContinue();
        exit(1);
    }

    std::cout << "Opened Disk Device" << std::endl;

    GetCharToContinue();

    //
    // Close File/Disk Device
    //
    if (disk_device_handle != INVALID_HANDLE_VALUE && disk_device_handle != NULL) {
        CloseHandle(disk_device_handle);
    }

    std::cout << "Closed Disk Device" << std::endl;


    GetCharToContinue();


    //
    // GetFileAttrs
    //

    DWORD dwAttrs;

    dwAttrs = GetFileAttributesW(L"\\\\.\\X:\\file1");

    if (dwAttrs == INVALID_FILE_ATTRIBUTES) {
        std::cout << "Error retrieving the file attribtues for the file.\n";
        GetCharToContinue();
        exit(EXIT_FAILURE);
    }

    if (dwAttrs & FILE_ATTRIBUTE_ARCHIVE) {
        std::cout << "Archive.\n";
    }
    if (dwAttrs & FILE_ATTRIBUTE_DIRECTORY) {
        std::cout << "Directory.\n";
    }
    if (dwAttrs & FILE_ATTRIBUTE_HIDDEN) {
        std::cout << "File is hidden.\n";
    }
    if (dwAttrs & FILE_ATTRIBUTE_NORMAL) {
        std::cout << "File is normal.\n";
    }
    if (dwAttrs & FILE_ATTRIBUTE_ENCRYPTED) {
        std::cout << "File is encrypted.\n";
    }
    if (dwAttrs & FILE_ATTRIBUTE_TEMPORARY) {
        std::cout << "File is temporary.\n";
    }
    if (dwAttrs & FILE_ATTRIBUTE_READONLY) {
        std::cout << "File is read only.\n";
    }

    GetCharToContinue();


    //
    // Reopen file
    //
    disk_device_handle = CreateFileW(L"\\\\.\\X:\\file1",
        GENERIC_ALL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        0,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        0);

    if (disk_device_handle == INVALID_HANDLE_VALUE || disk_device_handle == NULL) {
        std::cout << "ERROR: Failed to reopen file!" << std::endl;
        GetCharToContinue();
        exit(1);
    }

    std::cout << "Reopened file" << std::endl;


    GetCharToContinue();


    //
    // Close file
    //
    if (disk_device_handle != INVALID_HANDLE_VALUE && disk_device_handle != NULL) {
        CloseHandle(disk_device_handle);
    }

    std::cout << "Closed file" << std::endl;

    system("pause");
    return 0;
}

