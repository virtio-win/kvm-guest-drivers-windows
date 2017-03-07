/* HID passthrough driver.
 *
 * Copyright (c) 2017 Red Hat, Inc.
 * Author: Ladi Prosek <lprosek@redhat.com> (spec),
 *         Paolo Bonzini <pbonzini@redhat.com> (code)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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
