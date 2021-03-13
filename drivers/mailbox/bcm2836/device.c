//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
// Module Name:
//
//     device.c
//
// Abstract:
//
//
//

#include "precomp.h"

#include "trace.h"
#include "device.tmh"

#include "register.h"
#include "ioctl.h"
#include "device.h"
#include "interrupt.h"
#include "mailbox.h"
#include "init.h"

const int RpiqTag = 'QipR';

RPIQ_PAGED_SEGMENT_BEGIN

/*++

Routine Description:

    Worker routine called to create a device and its software resources.

Arguments:

    DeviceInit - Pointer to an opaque init structure. Memory for this
        structure will be freed by the framework when the WdfDeviceCreate
        succeeds. So don't access the structure after that point.

Return Value:

    NTSTATUS

--*/
_Use_decl_annotations_
NTSTATUS RpiqCreateDevice (
    WDFDRIVER Driver,
    PWDFDEVICE_INIT DeviceInit
    )
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    DEVICE_CONTEXT* deviceContextPtr;
    WDFDEVICE device;
    NTSTATUS status;
    WDF_IO_TYPE_CONFIG ioConfig;
    DECLARE_CONST_UNICODE_STRING(rpiqSymbolicLink, RPIQ_SYMBOLIC_NAME);

    PAGED_CODE();

    WDF_IO_TYPE_CONFIG_INIT(&ioConfig);
    ioConfig.DeviceControlIoType = WdfDeviceIoDirect;

    WdfDeviceInitSetIoTypeEx(DeviceInit, &ioConfig);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = RpiqPrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = RpiqReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0EntryPostInterruptsEnabled = RpiqInitOperation;

    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(
        &DeviceInit, 
        &deviceAttributes, 
        &device);
    if (!NT_SUCCESS(status)) {
        RPIQ_LOG_ERROR(
            "WdfDeviceCreate failed %!STATUS!",
            status);
        goto End;
    }

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFQUEUE queue;

    // Get a pointer to the device context structure that we just 
    // associated with the device object. We define this structure in
    // the device.h header file. RpiqGetContext is an inline function
    // generated by using the WDF_DECLARE_CONTEXT_TYPE_WITH_NAME macro in
    // device.h. This function will do the type checking and return the
    // device context. If you pass a wrong object handle it will return
    // NULL and assert if run under framework verifier mode.
    deviceContextPtr = RpiqGetContext(device);

    // Initialize version.
    deviceContextPtr->VersionMajor = RPIQ_VERSION_MAJOR;
    deviceContextPtr->VersionMinor = RPIQ_VERSION_MINOR;

    // Initialize IO Queue, the IO queue is initialize as sequential queue
    // for stability purposes. This could be expanded to multiple queue 
    // for each channel as other channel usage comes online to improve
    // performance. The power and frame buffer channel are channels that 
    // has some documentation and would be consider for further expansion.
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = RpiqProcessChannel;
    queueConfig.EvtIoStop = RpiqIoStop;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;

    status = WdfIoQueueCreate(
        device,
        &queueConfig,
        &attributes,
        &queue);
    if (!NT_SUCCESS(status)) {
        RPIQ_LOG_ERROR(
            "WdfIoQueueCreate fail %!STATUS!", status);
        goto End;
    }

    // Create symbolic and device interface
    status = WdfDeviceCreateSymbolicLink(
        device,
        &rpiqSymbolicLink);
    if (!NT_SUCCESS(status)) {
        RPIQ_LOG_ERROR(
            "Fail to register symbolic link %!STATUS!",
            status);
        goto End;
    }

    status = WdfDeviceCreateDeviceInterface(
        device,
        &RPIQ_INTERFACE_GUID,
        NULL);
    if (!NT_SUCCESS(status)) {
        RPIQ_LOG_ERROR(
            "Fail to register device interface %!STATUS!",
            status);
        goto End;
    }

    // Register notification for Ndis device interface
    status = IoRegisterPlugPlayNotification(
        EventCategoryDeviceInterfaceChange,
        PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
        (VOID*)&GUID_NDIS_LAN_CLASS,
        WdfDriverWdmGetDriverObject(Driver),
        RpiqNdisInterfaceCallback,
        device,
        &deviceContextPtr->NdisNotificationHandle);
    if (!NT_SUCCESS(status)) {
        RPIQ_LOG_ERROR(
            "Registrating Ndis interface notification fails %!STATUS!",
            status);
        goto End;
    }

    // Initialize a queuefor each channel. It seem like the VC is able
    // to process multiple channel request simultaneously.
    {
        WDF_IO_QUEUE_CONFIG ioQueueConfig;
        WDF_OBJECT_ATTRIBUTES ioQueueConfigAttributes;
        WDF_IO_QUEUE_CONFIG_INIT(
            &ioQueueConfig,
            WdfIoQueueDispatchManual);

        WDF_OBJECT_ATTRIBUTES_INIT(&ioQueueConfigAttributes);
        ioQueueConfigAttributes.ParentObject = device;

        for (ULONG queueCount = 0; queueCount < MAILBOX_CHANNEL_MAX; ++queueCount) {

            status = WdfIoQueueCreate(
                device,
                &ioQueueConfig,
                &ioQueueConfigAttributes,
                &deviceContextPtr->ChannelQueue[queueCount]);
            if (!NT_SUCCESS(status)) {
                RPIQ_LOG_ERROR(
                    "WdfIoQueueCreate (%d) failed %!STATUS!)",
                    queueCount,
                    status);
                goto End;
            }
        }
    }

End:

    return status;
}

/*++

Routine Description:

    In this callback, the driver does whatever is necessary to make the
    hardware ready to use.

Arguments:

    Device - A handle to a framework device object.

    ResourcesRaw - A handle to a framework resource-list object that identifies
        the raw hardware resources that the Plug and Play manager has assigned 
        to the device.

    ResourcesTranslated - A handle to a framework resource-list object that
        identifies the translated hardware resources that the Plug and Play
        manager has assigned to the device.

Return Value:

    NTSTATUS value

--*/
_Use_decl_annotations_
NTSTATUS RpiqPrepareHardware (
    WDFDEVICE Device,
    WDFCMRESLIST ResourcesRaw,
    WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS status;
    DEVICE_CONTEXT *deviceContextPtr;
    ULONG resourceCount;
    ULONG MemoryResourceCount = 0;
    ULONG InterruptResourceCount = 0;
    
    PAGED_CODE();

    deviceContextPtr = RpiqGetContext(Device);
    resourceCount = WdfCmResourceListGetCount(ResourcesTranslated);

    for (ULONG i = 0; i < resourceCount; ++i) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR res;

        res = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        switch (res->Type)
        {
        case CmResourceTypeMemory:
            {
                RPIQ_LOG_INFORMATION(
                    "Memory Resource Start: 0x%08x, Length: 0x%08x\n",
                    res->u.Memory.Start.LowPart,
                    res->u.Memory.Length);

                deviceContextPtr->Mailbox =
                    MmMapIoSpaceEx(
                        res->u.Memory.Start,
                        res->u.Memory.Length,
                        PAGE_READWRITE | PAGE_NOCACHE);
                if (deviceContextPtr->Mailbox == NULL) {
                    RPIQ_LOG_ERROR( "Failed to map mailbox register");
                    status = STATUS_UNSUCCESSFUL;
                    goto End;
                }

                deviceContextPtr->MailboxMmioLength = res->u.Memory.Length;

                status = RpiqMailboxInit(Device);
                if (!NT_SUCCESS(status)) {
                    RPIQ_LOG_ERROR(
                    "Failed to initiliaze mailbox module %!STATUS!",
                    status);
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    goto End;
                }
                ++MemoryResourceCount;
            }
            break;
        case CmResourceTypeInterrupt:
            {
                RPIQ_LOG_INFORMATION(
                    "Interrupt Level: 0x%08x, Vector: 0x%08x\n",
                    res->u.Interrupt.Level,
                    res->u.Interrupt.Vector);

                WDF_INTERRUPT_CONFIG interruptConfig;

                // First interrupt resource belongs to mailbox
                WDF_INTERRUPT_CONFIG_INIT(
                    &interruptConfig,
                    RpiqMailboxIsr,
                    RpiqMailboxDpc);

                interruptConfig.InterruptRaw =
                    WdfCmResourceListGetDescriptor (ResourcesRaw, i);
                    interruptConfig.InterruptTranslated = res;

                status = WdfInterruptCreate(
                    Device,
                    &interruptConfig,
                    WDF_NO_OBJECT_ATTRIBUTES,
                    &deviceContextPtr->MailboxIntObj);
                if (!NT_SUCCESS (status)) {
                    RPIQ_LOG_ERROR ("Fail to initialize mailbox interrupt object");
                    goto End;
                }
                ++InterruptResourceCount;
            }
            break;
        default:
            {
                RPIQ_LOG_WARNING("Unsupported resources, ignoring");
            }
            break;
        }
        // Only one memory and irq resource
        // Break so that we can work with both old and new firmware
        if(MemoryResourceCount && InterruptResourceCount)
            break;
    }

    if (MemoryResourceCount != RPIQ_MEMORY_RESOURCE_TOTAL &&
        InterruptResourceCount != RPIQ_INT_RESOURCE_TOTAL) {
        status = STATUS_UNSUCCESSFUL;
        RPIQ_LOG_ERROR("Unknown resource assignment");
        goto End;
    }

    status = STATUS_SUCCESS;

End:

    if (!NT_SUCCESS(status)) {
        RPIQ_LOG_ERROR("RpiqPrepareHardware failed %!STATUS!", status);
    }

    return status;
}

/*++

Routine Description:

    EvtDeviceReleaseHardware for Rpiq.

Arguments:

    Device - A handle to a framework device object.

    ResourcesTranslated - A handle to a resource list object that identifies
        the translated hardware resources that the Plug and Play manager has
        assigned to the device.

Return Value:

    NTSTATUS value

--*/
_Use_decl_annotations_
NTSTATUS RpiqReleaseHardware (
    WDFDEVICE Device,
    WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS status;
    DEVICE_CONTEXT *deviceContextPtr;

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    deviceContextPtr = RpiqGetContext(Device);

    if (deviceContextPtr->Mailbox != NULL) {
        status = RpiqDisableInterrupts(deviceContextPtr);
        if (!NT_SUCCESS(status)) {
            RPIQ_LOG_ERROR("Fail to disable interrupts %!STATUS!", status);
        }

        MmUnmapIoSpace(deviceContextPtr->Mailbox,
            deviceContextPtr->MailboxMmioLength);
        deviceContextPtr->Mailbox = NULL;
        deviceContextPtr->MailboxMmioLength = 0;
    }

    if (deviceContextPtr->NdisNotificationHandle != NULL) {
        // Ignore return value as there is not much we can do
        status = IoUnregisterPlugPlayNotification(
            deviceContextPtr->NdisNotificationHandle);
        if (!NT_SUCCESS(status)) {
            RPIQ_LOG_ERROR(
                "Ndis interface notification deregistration fails %!STATUS!",
                status);
        }
        deviceContextPtr->NdisNotificationHandle = NULL;
    }

    for (ULONG queueCount = 0; queueCount < MAILBOX_CHANNEL_MAX; ++queueCount) {
        if (deviceContextPtr->ChannelQueue[queueCount]) {
            WdfIoQueuePurgeSynchronously(
                deviceContextPtr->ChannelQueue[queueCount]);

            deviceContextPtr->ChannelQueue[queueCount] = NULL;
        }
    }

    return STATUS_SUCCESS;
}

/*++

Routine Description:

    RpiqNdisInterfaceCallback is DRIVER_NOTIFICATION_CALLBACK_ROUTINE callback
    responsible to monitor Ndis GUID enabling. It is responsible create a 
    handle once the Ndis interface is enabled.

Arguments:

    NotificationStructure - The DEVICE_INTERFACE_CHANGE_NOTIFICATION structure
        describes a device interface that has been enabled (arrived) or 
        disabled (removed).

    Context - The callback routine's Context parameter contains the context
        data the driver supplied during registration.

Return Value:

    NTSTATUS value

--*/

extern WCHAR macAddrStrGlobal[13];

NTSTATUS RpiqSetNdisMacAddress()
{
    NTSTATUS status = STATUS_SUCCESS;
    UNICODE_STRING KeyPrefix = RTL_CONSTANT_STRING (
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}\\");
    UNICODE_STRING DriverDesc = RTL_CONSTANT_STRING ( L"DriverDesc" );
    UNICODE_STRING Desc;
    UNICODE_STRING DescLAN7800 = RTL_CONSTANT_STRING ( L"LAN7800" );
    UNICODE_STRING DescLAN951x = RTL_CONSTANT_STRING ( L"LAN9512/LAN9514" );
    UNICODE_STRING NetworkAddress = RTL_CONSTANT_STRING ( L"NetworkAddress" );
    UNICODE_STRING PropertyChangeStatus = RTL_CONSTANT_STRING ( L"PropertyChangeStatus" );
    UNICODE_STRING KeyName;
    WCHAR KeyInstance[] = L"0000";
    ULONG Instance = 0;
    static BOOLEAN MacSet = 0;
    OBJECT_ATTRIBUTES ObjectAttributes;  
    HANDLE BaseKey = NULL;
    HANDLE SubKey = NULL;
    PKEY_VALUE_FULL_INFORMATION infoBuffer = NULL;
    ULONG infoBufferLength = 0;
    ULONG RequireBufferLength = 0;
    DWORD value;

    if(MacSet) {
        return status;
    }

    MacSet = TRUE;

    InitializeObjectAttributes (&ObjectAttributes,
                                (PUNICODE_STRING)&KeyPrefix,
                                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                NULL,
                                NULL
                                );

    status = ZwOpenKey (&BaseKey,
                        KEY_READ | KEY_WRITE,
                        &ObjectAttributes);
    if (!NT_SUCCESS(status)){
        BaseKey = NULL;
        goto End;
    }
    
    KeyName.Length = 8;
    KeyName.MaximumLength = 8;
    KeyName.Buffer = KeyInstance;

    // iterates through all enumerated NDIS adapters looking for the instances interested
    // such as LAN7800 or LAN9512/9514
    // Sets MAC address and property change flag to the device if the values are not set
    while(STATUS_OBJECT_NAME_NOT_FOUND != status)
    {
        status = RtlStringCchPrintfW(KeyInstance,
                                     sizeof(KeyInstance) / sizeof(WCHAR),
                                     L"%04d",
                                     Instance);
        Instance ++;
        
        if (!NT_SUCCESS(status)){
            // continue to try next entry
            continue;
        }

        InitializeObjectAttributes (&ObjectAttributes,
                                    (PUNICODE_STRING)&KeyName,
                                    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                    BaseKey,
                                    NULL
                                    );

        status = ZwOpenKey (&SubKey,
                            KEY_READ | KEY_WRITE,
                            &ObjectAttributes);
        if (!NT_SUCCESS(status)){
            SubKey = NULL;
            // continue to try next entry
            continue;
        }

        if ( infoBuffer ) {
            RtlZeroMemory  (infoBuffer, infoBufferLength);
        } 

        status = ZwQueryValueKey(
                            SubKey,
                            &DriverDesc,
                            KeyValueFullInformation,
                            infoBuffer,
                            infoBufferLength,
                            &RequireBufferLength
                            );

        if ( status == STATUS_BUFFER_TOO_SMALL ) {
            infoBuffer = ExAllocatePoolWithTag(
                                NonPagedPoolNx,
                                RequireBufferLength,
                                'qipr'
                                );

            if ( infoBuffer == NULL ) {
                goto End;
            }

            infoBufferLength = RequireBufferLength;

            status = ZwQueryValueKey(
                                SubKey,
                                &DriverDesc,
                                KeyValueFullInformation,
                                infoBuffer,
                                infoBufferLength,
                                &RequireBufferLength
                                );
                    
        }

        // Query Success
        if ( status == STATUS_SUCCESS ) {
            Desc.Length = (USHORT)infoBuffer->DataLength;
            Desc.MaximumLength = (USHORT)infoBuffer->DataLength;
            Desc.Buffer = (PWSTR)((PCHAR)infoBuffer +
                                          infoBuffer->DataOffset);

            // Use OR to match multiple devices here
            // It is better to make a list for more devices
            if (   (RtlPrefixUnicodeString (&DescLAN7800, &Desc, TRUE) == TRUE) 
                || (RtlPrefixUnicodeString (&DescLAN951x, &Desc, TRUE) == TRUE)){
                status = ZwQueryValueKey(
                                SubKey,
                                &PropertyChangeStatus,
                                KeyValueFullInformation,
                                infoBuffer,
                                infoBufferLength,
                                &RequireBufferLength
                                );
                                
                if(STATUS_OBJECT_NAME_NOT_FOUND == status) {
                    //Network Mac Address is not set
                    status = ZwSetValueKey(SubKey,
                                   &NetworkAddress,
                                   0, // title index; must be zero.
                                   REG_SZ,
                                   &macAddrStrGlobal,
                                   sizeof(macAddrStrGlobal));
                    //Require Property change    
                    value = 1;                
                    status = ZwSetValueKey(SubKey,
                                   &PropertyChangeStatus,
                                   0, // title index; must be zero.
                                   REG_DWORD,
                                   (PVOID)&value,
                                   sizeof(DWORD));
                }
                 // Finished Setting MAC or it already set before, exit no matter status
                 goto End;
            }
        }

        if(STATUS_OBJECT_NAME_NOT_FOUND == status) {
            // ignore non existence for value key
            status = STATUS_SUCCESS;
        }

        if(SubKey) {
            // Close this sub key before query next
            ZwClose(SubKey);
            SubKey = NULL;
        }
        if ( infoBuffer ) {
            ExFreePool (infoBuffer);
            infoBuffer = NULL;
            infoBufferLength = 0;
        }
    }

End:
    if(BaseKey) {
        ZwClose(BaseKey);
    }
    if(SubKey) {
        ZwClose(SubKey);
    }
    if ( infoBuffer ) {
        ExFreePool (infoBuffer);
    }
    return status;
}

_Use_decl_annotations_
NTSTATUS RpiqNdisInterfaceCallback (
    VOID* NotificationStructure,
    VOID* Context
    )
{
    NTSTATUS status;
    WDFDEVICE device = Context;
    const DEVICE_INTERFACE_CHANGE_NOTIFICATION* notification;
    
    PAGED_CODE();

    notification = 
        (const DEVICE_INTERFACE_CHANGE_NOTIFICATION*)NotificationStructure;

    // Open a handle to a NDIS device so rpiq always keep a reference.
    if (IsEqualGUID(&notification->Event, &GUID_DEVICE_INTERFACE_ARRIVAL)) {
        WDF_OBJECT_ATTRIBUTES  ioTargetAttrib;
        WDFIOTARGET  ioTarget;
        WDF_IO_TARGET_OPEN_PARAMS  openParams;

#pragma prefast(suppress:28922, "Check device value anyway to satisfy WdfIoTargetCreate requirement")
        if (device == NULL) {
            status = STATUS_INVALID_PARAMETER;
            RPIQ_LOG_ERROR("Fail to create remote target %!STATUS!", status);
            goto End;
        }

        RpiqSetNdisMacAddress();

        WDF_OBJECT_ATTRIBUTES_INIT(&ioTargetAttrib);
        ioTargetAttrib.ParentObject = device;

        status = WdfIoTargetCreate(
            device,
            &ioTargetAttrib,
            &ioTarget);
        if (!NT_SUCCESS(status)) {
            RPIQ_LOG_ERROR("Fail to create remote target %!STATUS!", status);
            goto End;
        }
        WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(
            &openParams,
            notification->SymbolicLinkName,
            STANDARD_RIGHTS_ALL);
        openParams.EvtIoTargetRemoveComplete = RpiqNdisTargetRemoveComplete;

        status = WdfIoTargetOpen(
            ioTarget,
            &openParams);
        if (!NT_SUCCESS(status)) {
            WdfObjectDelete(ioTarget);
            RPIQ_LOG_ERROR(
                "Fail to create Ndis remote target %!STATUS!",
                status);
            goto End;
        }
    }

End:
    return STATUS_SUCCESS;
}

/*++

Routine Description:

    RpiqNdisTargetRemoveComplete EvtIoTargetRemoveComplete event callback
    function performs operations when the removal of a specified 
    remote I/O target is complete.

Arguments:

    IoTarget - A handle to an I/O target object.

Return Value:

    NTSTATUS value

--*/
_Use_decl_annotations_
VOID RpiqNdisTargetRemoveComplete (
    WDFIOTARGET IoTarget
    )
{
    PAGED_CODE();
    
    // WdfObjectDelete would eventually call WdfIoTargetClose which is a
    // requirement for this call back
    WdfObjectDelete(IoTarget);
}

RPIQ_PAGED_SEGMENT_END

RPIQ_NONPAGED_SEGMENT_BEGIN

/*++

Routine Description:

    A driver's EvtIoStop event callback function completes, requeues, or
    suspends processing of a specified request because the request's I/O queue
    is being stopped.

Arguments:

    Queue - A handle to the framework queue object that is associated with the
        I/O request.

    Request - A handle to a framework request object.

    ActionFlags - A bitwise OR of one or more WDF_REQUEST_STOP_ACTION_FLAGS
        typed flags that identify the reason that the callback function is 
        being called and whether the request is cancelable.

Return Value:

    NTSTATUS value

--*/
_Use_decl_annotations_
VOID RpiqIoStop (
    WDFQUEUE   Queue,
    WDFREQUEST Request,
    ULONG      ActionFlags
    )
{
    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(ActionFlags);

    // Requeue all pending request
    WdfRequestStopAcknowledge(
        Request,
        TRUE);
}

RPIQ_NONPAGED_SEGMENT_END