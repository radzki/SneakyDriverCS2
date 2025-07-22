#include <ntifs.h>

extern "C" {

    //Hidden windows functions (according to youtuber)
    NTKERNELAPI NTSTATUS IoCreateDriver(PUNICODE_STRING DriverName,
        PDRIVER_INITIALIZE InitilizationFunction);

    NTKERNELAPI NTSTATUS MmCopyVirtualMemory(PEPROCESS SourceProcess, PVOID SourceAddress,
        PEPROCESS TargetProcess, PVOID TargetAddress,
        SIZE_T BufferSize, KPROCESSOR_MODE PreviousMode,
        PSIZE_T ReturnSize);
}

//Helper function to print debug text
void debugPrint(PCSTR text) {
#ifndef DEBUG
    UNREFERENCED_PARAMETER(text);
#endif
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, text));
}


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
    //Functions use to commuicate with usermode client
    NTSTATUS create(PDEVICE_OBJECT deviceObject, PIRP irp) {
        UNREFERENCED_PARAMETER(deviceObject);
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return irp->IoStatus.Status;
    }
    NTSTATUS close(PDEVICE_OBJECT deviceObject, PIRP irp) {
        UNREFERENCED_PARAMETER(deviceObject);
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return irp->IoStatus.Status;
    }
    //Function controlling read/write/attach
    NTSTATUS deviceControl(PDEVICE_OBJECT deviceObject, PIRP irp) {
        UNREFERENCED_PARAMETER(deviceObject);
        debugPrint("Device Control Called.\n");

        NTSTATUS status{ STATUS_UNSUCCESSFUL };
        PIO_STACK_LOCATION stackIrp = IoGetCurrentIrpStackLocation(irp);
        auto request{ reinterpret_cast<Request*>(irp->AssociatedIrp.SystemBuffer) };

        if (stackIrp == nullptr || request == nullptr) {
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            return status;
        }

        //Target process
        static PEPROCESS targetProcess{ nullptr };
        const ULONG controlCode{ stackIrp->Parameters.DeviceIoControl.IoControlCode };


        //Recieve request from usermode client and excecute function based on input
        switch (controlCode) {
        case codes::attach:
            status = PsLookupProcessByProcessId(request->processId, &targetProcess);
            break;
        case codes::read:
            if (targetProcess != nullptr)
                status = MmCopyVirtualMemory(targetProcess, request->target, PsGetCurrentProcess(),
                    request->buffer, request->size, KernelMode, &request->returnSize);
            break;
        case codes::write:
            if (targetProcess != nullptr)
                status = MmCopyVirtualMemory(PsGetCurrentProcess(), request->buffer, targetProcess,
                    request->target, request->size, KernelMode, &request->returnSize);
            break;
        }

        irp->IoStatus.Status = status;
        irp->IoStatus.Information = sizeof(Request);


        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return irp->IoStatus.Status;
    }


}//namespace driver

NTSTATUS driverMain(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath) {
    UNREFERENCED_PARAMETER(registryPath);

    UNICODE_STRING deviceName{};
    RtlInitUnicodeString(&deviceName, L"\\Device\\SneakyDriver");
    PDEVICE_OBJECT deviceObject{};

    NTSTATUS status = IoCreateDevice(driverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN, FALSE, &deviceObject);
    if (status != STATUS_SUCCESS) {
        debugPrint("Failed to create driver device.\n");
        return status;
    }
    debugPrint("Driver device successfully created.\n");

    UNICODE_STRING symbolicLink{};
    RtlInitUnicodeString(&symbolicLink, L"\\DosDevices\\SneakyDriver");

    status = IoCreateSymbolicLink(&symbolicLink, &deviceName);
    if (status != STATUS_SUCCESS) {
        debugPrint("Failed to establish symbolic link.\n");
        return status;
    }
    debugPrint("Symbolic link successfully established.\n");

    SetFlag(deviceObject->Flags, DO_BUFFERED_IO);

    driverObject->MajorFunction[IRP_MJ_CREATE] = driver::create;
    driverObject->MajorFunction[IRP_MJ_CLOSE] = driver::close;
    driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = driver::deviceControl;

    ClearFlag(deviceObject->Flags, DO_DEVICE_INITIALIZING);

    debugPrint("Driver Initialized successfully.\n");

    return status;
}


NTSTATUS DriverEntry() {
    debugPrint("SneakyDriver is starting\n");

    UNICODE_STRING driverName{};
    RtlInitUnicodeString(&driverName, L"\\Driver\\SneakyDriver");

    return IoCreateDriver(&driverName, &driverMain);
}