#define AIL_ALL_IMPL
#include "ail.h"
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <xpsprint.h>

#define MAX_KEY_LEN 255



int main(void)
{
    (void)ail_default_allocator;

    // unsigned long key_name_size = MAX_KEY_LEN;
    // char key_name[MAX_KEY_LEN] = {0};
    // long res = RegEnumKeyEx(HKEY_LOCAL_MACHINE, 6, key_name, &key_name_size, NULL, NULL, NULL, NULL);
    // if (res == ERROR_NO_MORE_ITEMS) {
    //     printf("done");
    //     return 0;
    // }
    // else if (res == ERROR_SUCCESS) {
    //     printf("Failed to enumerate keys: %ld\n", res);
    //     return 1;
    // }
    // printf("Name: %s\n", key_name);

    // unsigned long ports_amount = 0;
    // unsigned long required_size;
    // bool res = EnumPorts(NULL, 2, NULL, 0, &required_size, &ports_amount);
    // PORT_INFO_2 *ports = malloc(required_size);
    // res = EnumPorts(NULL, 2, (u8 *)ports, required_size, &required_size, &ports_amount);
    // if (res == 0) {
    //     printf("Error: %d\n", res);
    //     return 1;
    // }
    // for (unsigned long i = 0; i < ports_amount; i++) {
    //     PORT_INFO_2 port = ports[i];
    //     if (port.fPortType & PORT_TYPE_READ && port.fPortType & PORT_TYPE_WRITE) {
    //         printf("Name: %s, Monitor: %s, Desc: %s\n", port.pPortName, port.pMonitorName, port.pDescription);
    //     }
    // }

    // // Initialize the COM interface, if the application has not
    // //  already done so.
    // i32 hr;
    // if (FAILED(hr = CoInitializeEx(0, COINIT_MULTITHREADED))) {
    //     fprintf(stderr, "ERROR: CoInitializeEx failed with HRESULT 0x%X\n", hr);
    //     return 1;
    // }
    // // Create the completion event
    // void *completionEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    // if (!completionEvent) {
    //     hr = HRESULT_FROM_WIN32(GetLastError());
    //     fprintf(stderr, "ERROR: Could not create completion event: %08X\n", hr);
    // }
    // // Start an XPS Print Job
    // void *job = NULL;
    // void *jobStream = NULL;
    // char *printerName = "COM4";
    // if (FAILED(hr = StartXpsPrintJob(printerName, NULL, NULL, NULL, completionEvent, NULL, 0, &job, &jobStream, NULL))) {
    //     fprintf(stderr, "ERROR: Could not start XPS print job: %08X\n", hr);
    // }

    HANDLE hComm;
    hComm = CreateFile(TEXT("COM4"), GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    if (hComm == INVALID_HANDLE_VALUE) {
        u32 err = GetLastError();
        fprintf(stderr, "Failed creating file: %d\n", err);
        if (err == ERROR_ACCESS_DENIED) fprintf(stderr, "Access denied\n");
        return 1;
    }
    u8 data[] = { 69 };
    bool res;
    res = WriteFile(hComm, data, 1, NULL, NULL);
    if (!res) {
        fprintf(stderr, "Failed writing to file: %ld\n", GetLastError());
        return 1;
    }
    printf("Wrote %d to file\n", data[0]);
    char buffer[255] = {0};
    Sleep(10);
    res = ReadFile(hComm, buffer, 16, NULL, NULL);
    if (!res) {
        fprintf(stderr, "Failed reading from file: %ld\n", GetLastError());
        return 1;
    }
    printf("Read %lld bytes: %s\n", strlen(buffer), buffer);

    return 0;
}