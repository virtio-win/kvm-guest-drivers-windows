/*
 * HID passthrough driver.
 *
 * Copyright (c) 2017 Red Hat, Inc.
 *
 * Author: Ladi Prosek <lprosek@redhat.com> (spec),
 *         Paolo Bonzini <pbonzini@redhat.com> (code)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <wdm.h>
#include <hidport.h>
#include "pdo.h"

static DRIVER_ADD_DEVICE HidPassthroughAddDevice;
static DRIVER_UNLOAD     HidPassthroughUnload;

static _Dispatch_type_(IRP_MJ_POWER) DRIVER_DISPATCH HidPassthroughDispatchPower;
static _Dispatch_type_(IRP_MJ_OTHER) DRIVER_DISPATCH HidPassthroughDispatch;

DRIVER_INITIALIZE DriverEntry;

static NTSTATUS
HidPassthroughAddDevice(_In_ PDRIVER_OBJECT DriverObject,
                        _In_ PDEVICE_OBJECT FunctionalDeviceObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    FunctionalDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

static void
HidPassthroughUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

static NTSTATUS
HidPassthroughDispatch(_In_    PDEVICE_OBJECT  DeviceObject,
                       _Inout_ PIRP            Irp)
{
    PHID_DEVICE_EXTENSION ext = DeviceObject->DeviceExtension;

    IoCopyCurrentIrpStackLocationToNext(Irp);
    return IoCallDriver(ext->NextDeviceObject, Irp);
}


static NTSTATUS
HidPassthroughDispatchPower(_In_    PDEVICE_OBJECT  DeviceObject,
                            _Inout_ PIRP            Irp)
{
    PHID_DEVICE_EXTENSION ext = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    return PoCallDriver(ext->NextDeviceObject, Irp);
}

_Dispatch_type_(IRP_MJ_INTERNAL_DEVICE_CONTROL)
static NTSTATUS
HidPassthroughDispatchIoctl(_In_    PDEVICE_OBJECT  DeviceObject,
                            _Inout_ PIRP            Irp)
{
    PHID_DEVICE_EXTENSION ext = DeviceObject->DeviceExtension;
    PPDO_EXTENSION ext_pdo = (PPDO_EXTENSION)ext->PhysicalDeviceObject->DeviceExtension;

    IoCopyCurrentIrpStackLocationToNext(Irp);
    return IoCallDriver(ext_pdo->BusFdo, Irp);
}

NTSTATUS
DriverEntry (_In_ PDRIVER_OBJECT  DriverObject,
             _In_ PUNICODE_STRING RegistryPath)
{
    HID_MINIDRIVER_REGISTRATION hid;
    int i;

    /* The dispatch table should pass through all the IRPs,
     * but IRP_MJ_POWER is special because we need to start
     * the next power IRP immediately.
     * IRP_MJ_INTERNAL_DEVICE_CONTROL is forwarded to the
     * parent device to handle HID IOCTLs.
     */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        if (i == IRP_MJ_INTERNAL_DEVICE_CONTROL) {
           DriverObject->MajorFunction[i] = HidPassthroughDispatchIoctl;
        } else if (i == IRP_MJ_POWER) {
           DriverObject->MajorFunction[i] = HidPassthroughDispatchPower;
        } else {
           DriverObject->MajorFunction[i] = HidPassthroughDispatch;
        }
    }

    DriverObject->DriverExtension->AddDevice = HidPassthroughAddDevice;
    DriverObject->DriverUnload               = HidPassthroughUnload;

    RtlZeroMemory(&hid, sizeof(HID_MINIDRIVER_REGISTRATION));
    hid.Revision     = HID_REVISION;
    hid.DriverObject = DriverObject;
    hid.RegistryPath = RegistryPath;

    return HidRegisterMinidriver(&hid);
}
