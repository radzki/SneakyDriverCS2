#pragma once

#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>
#include "client.dll.hpp"
#include "offsets.hpp"
#include "common_structs.h"

namespace driver {
    namespace codes {
        //Used to Setup Driver
        constexpr ULONG attach{
            CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS) };
        constexpr ULONG read{
            CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS) };
        constexpr ULONG write{
            CTL_CODE(FILE_DEVICE_UNKNOWN, 0x698, METHOD_BUFFERED, FILE_SPECIAL_ACCESS) };
    }//namespace codes

    //Shared between user mode and kernel mode
    struct Request {
        HANDLE processId{};

        PVOID target{};
        PVOID buffer{};

        SIZE_T size{};
        SIZE_T returnSize{};
    };

    bool attachToProcess(HANDLE driverHandle, const DWORD pid) {
        Request r;
        r.processId = reinterpret_cast<HANDLE>(pid);

        return DeviceIoControl(driverHandle, codes::attach, &r, sizeof(r), &r, sizeof(r), nullptr,
            nullptr);
    }

    template <class T>
    T readMemory(HANDLE driverHandle, const std::uintptr_t addr) {
        T temp = {};

        Request r;
        r.target = reinterpret_cast<PVOID>(addr);
        r.buffer = &temp;
        r.size = sizeof(T);

        DeviceIoControl(driverHandle, codes::read, &r, sizeof(r), &r, sizeof(r), nullptr,
            nullptr);

        return temp;
    }

    template <class T>
    void writeMemory(HANDLE driverHandle, const std::uintptr_t addr, const T& value) {
        Request r;
        r.target = reinterpret_cast<PVOID>(addr);
        r.buffer = (PVOID)&value;
        r.size = sizeof(T);

        DeviceIoControl(driverHandle, codes::write, &r, sizeof(r), &r, sizeof(r), nullptr,
            nullptr);
    }
}