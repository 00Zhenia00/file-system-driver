// #include <ntddk.h>
#include <stdio.h>
#include "Extensions.h"
#include "Resources.h"
#include "ntstrsafe.h"

#define MOUNT_DISK_CTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define UNMOUNT_DISK_CTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

NTSTATUS DriverEntry(
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath);

NTSTATUS CreateDiskDevice(
	PDEVICE_OBJECT DeviceObject,
	PIRP Irp);

VOID DeleteDiskDevice(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp);

NTSTATUS DispatchDevCTL(
	PDEVICE_OBJECT DeviceObject,
	PIRP Irp);

NTSTATUS IrpHandler(
	IN PDEVICE_OBJECT Device_Object,
	IN PIRP Irp);

VOID DriverUnload(
	IN PDRIVER_OBJECT DriverObject);

NTSTATUS CompleteIrp(
	PIRP Irp,
	NTSTATUS status,
	ULONG info);

NTSTATUS CreateFileOnDisk(
	PDEVICE_OBJECT DiskDevice,
	PIRP Irp);

NTSTATUS CloseFileOnDisk(
	PDEVICE_OBJECT DiskDevice,
	PIRP Irp);

NTSTATUS DispatchQueryInformation(
	PDEVICE_OBJECT DeviceObject,
	PIRP Irp);

// device object associated with driver object
PDEVICE_OBJECT gMountDeviceObject = NULL;


NTSTATUS DriverEntry(
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath) {

	DriverObject->DriverUnload = DriverUnload;

	UNICODE_STRING deviceName, symLink;
	RtlInitUnicodeString(&deviceName, L"\\Device\\MountDevice");
	RtlInitUnicodeString(&symLink, L"\\??\\MountDeviceLink");

	//
	// Create Control Device
	//
	NTSTATUS status = IoCreateDevice(DriverObject,
									sizeof(CONTROL_DEVICE_EXTENSION),
									&deviceName,
									FILE_DEVICE_UNKNOWN,
									FILE_DEVICE_SECURE_OPEN,
									FALSE,
									&gMountDeviceObject);


	if (!NT_SUCCESS(status)) {
		KdPrint((" -> ERROR: Failed to create Control Device! \r\n"));
		return status;
	}

	//
	// Create symbolic link
	//
	status = IoCreateSymbolicLink(&symLink, &deviceName);

	if (!NT_SUCCESS(status)) {
		KdPrint((" -> ERROR: Failed to create Control Device symbolic link! \r\n"));
		IoDeleteDevice(gMountDeviceObject);
		return status;
	}

	//
	// Fill Control Device extension
	//
	PCONTROL_DEVICE_EXTENSION pdx = (PCONTROL_DEVICE_EXTENSION)gMountDeviceObject->DeviceExtension;
	pdx->DriverObject = DriverObject;
	pdx->SymLink = symLink;
	pdx->DiskCount = 0;

	//
	// Init Control Device RESOURCE
	//
	status = ExInitializeResourceLite(&pdx->Resource);

	if (!NT_SUCCESS(status)) {
		KdPrint((" -> ERROR: Failed to initialize Control Device Resource! \r\n"));
		IoDeleteDevice(gMountDeviceObject);
		return status;
	}

	//
	// Init Control Device VCB list
	//
	InitializeListHead(&pdx->FirstVCB);

	//
	// Add MajorFunction handlers
	//
	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
		DriverObject->MajorFunction[i] = IrpHandler;
	}

	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDevCTL;

	KdPrint((" -> Driver loaded \r\n"));

	return STATUS_SUCCESS;

}

NTSTATUS DispatchDevCTL(PDEVICE_OBJECT DeviceObject, PIRP Irp) {

	PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS status = STATUS_SUCCESS;
	PCONTROL_DEVICE_EXTENSION control_device_ext = DeviceObject->DeviceExtension;

	Irp->IoStatus.Information = 0;

	switch (irpsp->Parameters.DeviceIoControl.IoControlCode) {

	case MOUNT_DISK_CTL:
		KdPrint((" -> Mounting disk... \r\n"));

		// Mount disk
		status = CreateDiskDevice(DeviceObject, Irp);

		if (NT_SUCCESS(status)) {
			control_device_ext->DiskCount++;
			KdPrint((" -> Disk mounted \r\n"));
		}
		else {
			KdPrint((" -> ERROR: Failed to mount Disk Device! \r\n"));
		}

		break;

	case UNMOUNT_DISK_CTL:
		KdPrint((" -> Unmounting disk... \r\n"));

		// Unmount disk
		DeleteDiskDevice(DeviceObject, Irp);

		control_device_ext->DiskCount--;

		status = STATUS_SUCCESS;

		KdPrint((" -> Disk unmounted \r\n"));

		break;

	case IOCTL_DISK_GET_DRIVE_GEOMETRY:	// *
		KdPrint((" -> IOCTL_DISK_GET_DRIVE_GEOMETRY request \r\n"));

		PDISK_GEOMETRY  disk_geometry;

		disk_geometry = (PDISK_GEOMETRY)Irp->AssociatedIrp.SystemBuffer;
		disk_geometry->Cylinders.QuadPart = 0x80;
		disk_geometry->MediaType = FixedMedia;
		disk_geometry->TracksPerCylinder = 0x80;
		disk_geometry->SectorsPerTrack = 0x20;
		disk_geometry->BytesPerSector = 0x80;
		Irp->IoStatus.Information = sizeof(DISK_GEOMETRY);

		status = STATUS_SUCCESS;
		break;

	case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:	// *
		KdPrint((" -> IOCTL_MOUNTDEV_QUERY_DEVICE_NAME request \r\n"));

		if (irpsp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(MOUNTDEV_NAME))
		{
			Irp->IoStatus.Information = sizeof(MOUNTDEV_NAME);
			status = STATUS_BUFFER_OVERFLOW;
		}
		else
		{
			PMOUNTDEV_NAME devName = (PMOUNTDEV_NAME)Irp->AssociatedIrp.SystemBuffer;

			// WCHAR device_name_buffer[MAXIMUM_FILENAME_LENGTH];
			// swprintf(device_name_buffer, DIRECT_DISK_PREFIX L"%u", devId_);

			WCHAR device_name_buffer[] = L"\\Device\\MyDiskLink0";	// TODO: Add real Disk Device name

			UNICODE_STRING deviceName;
			RtlInitUnicodeString(&deviceName, device_name_buffer);

			devName->NameLength = deviceName.Length;
			int outLength = sizeof(USHORT) + deviceName.Length;
			if (irpsp->Parameters.DeviceIoControl.OutputBufferLength < outLength)
			{
				status = STATUS_BUFFER_OVERFLOW;
				Irp->IoStatus.Information = sizeof(MOUNTDEV_NAME);
				break;
			}

			RtlCopyMemory(devName->Name, deviceName.Buffer, deviceName.Length);

			Irp->IoStatus.Information = outLength;
			status = STATUS_SUCCESS;
		}

		break;

	case IOCTL_MOUNTDEV_QUERY_STABLE_GUID:	// *
		KdPrint((" -> IOCTL_MOUNTDEV_QUERY_STABLE_GUID request \r\n"));

		Irp->IoStatus.Information = 0;
		status = STATUS_INVALID_DEVICE_REQUEST;

		break;

	case IOCTL_DISK_IS_WRITABLE:	// *
		KdPrint((" -> IOCTL_DISK_IS_WRITABLE request \r\n"));

		Irp->IoStatus.Information = 0;

		status = STATUS_SUCCESS;
		break;

	case IOCTL_DISK_GET_DRIVE_LAYOUT:

		KdPrint((" -> IOCTL_DISK_GET_DRIVE_LAYOUT request \r\n"));

		if (irpsp->Parameters.DeviceIoControl.OutputBufferLength <
			sizeof(DRIVE_LAYOUT_INFORMATION))
		{
			status = STATUS_INVALID_PARAMETER;
			Irp->IoStatus.Information = 0;
		}
		else
		{
			PDRIVE_LAYOUT_INFORMATION outputBuffer = (PDRIVE_LAYOUT_INFORMATION)Irp->AssociatedIrp.SystemBuffer;

			outputBuffer->PartitionCount = 1;
			outputBuffer->Signature = 0;

			outputBuffer->PartitionEntry->PartitionType = PARTITION_ENTRY_UNUSED;
			outputBuffer->PartitionEntry->BootIndicator = FALSE;
			outputBuffer->PartitionEntry->RecognizedPartition = TRUE;
			outputBuffer->PartitionEntry->RewritePartition = FALSE;
			outputBuffer->PartitionEntry->StartingOffset = RtlConvertUlongToLargeInteger(0);
			outputBuffer->PartitionEntry->PartitionLength.QuadPart = 0x80;
			outputBuffer->PartitionEntry->HiddenSectors = 1L;

			status = STATUS_SUCCESS;
			Irp->IoStatus.Information = sizeof(PARTITION_INFORMATION);
		}
		break;

	case IOCTL_DISK_GET_LENGTH_INFO:

		KdPrint((" -> IOCTL_DISK_GET_LENGTH_INFO request \r\n"));
	
		PGET_LENGTH_INFORMATION get_length_information;
		if (irpsp->Parameters.DeviceIoControl.OutputBufferLength <
			sizeof(GET_LENGTH_INFORMATION))
		{
			status = STATUS_BUFFER_TOO_SMALL;
			Irp->IoStatus.Information = 0;
			break;
		}
		get_length_information = (PGET_LENGTH_INFORMATION)Irp->AssociatedIrp.SystemBuffer;
		get_length_information->Length.QuadPart = 0x80;
		status = STATUS_SUCCESS;
		Irp->IoStatus.Information = sizeof(GET_LENGTH_INFORMATION);
		break;
	
	case IOCTL_DISK_GET_PARTITION_INFO:
		KdPrint((" -> IOCTL_DISK_GET_PARTITION_INFO request \r\n"));

		PPARTITION_INFORMATION  partition_information;
		if (irpsp->Parameters.DeviceIoControl.OutputBufferLength <
			sizeof(PARTITION_INFORMATION))
		{
			status = STATUS_BUFFER_TOO_SMALL;
			Irp->IoStatus.Information = 0;
			break;
		}
		partition_information = (PPARTITION_INFORMATION)Irp->AssociatedIrp.SystemBuffer;
		partition_information->StartingOffset.QuadPart = 0;
		partition_information->PartitionLength.QuadPart = 0x80;
		partition_information->HiddenSectors = 0;
		partition_information->PartitionNumber = 0;
		partition_information->PartitionType = 0;
		partition_information->BootIndicator = FALSE;
		partition_information->RecognizedPartition = TRUE;
		partition_information->RewritePartition = FALSE;
		Irp->IoStatus.Information = sizeof(PARTITION_INFORMATION);
		status = STATUS_SUCCESS;

		break;

	case IOCTL_DISK_GET_PARTITION_INFO_EX:

		KdPrint((" -> IOCTL_DISK_GET_PARTITION_INFO_EX request \r\n"));
	
		PPARTITION_INFORMATION_EX   partition_information_ex;
		if (irpsp->Parameters.DeviceIoControl.OutputBufferLength <
			sizeof(PARTITION_INFORMATION_EX))
		{
			status = STATUS_BUFFER_TOO_SMALL;
			Irp->IoStatus.Information = 0;
			break;
		}
		partition_information_ex = (PPARTITION_INFORMATION_EX)Irp->AssociatedIrp.SystemBuffer;
		partition_information_ex->PartitionStyle = PARTITION_STYLE_MBR;
		partition_information_ex->StartingOffset.QuadPart = 0;
		partition_information_ex->PartitionLength.QuadPart = 0x80;
		partition_information_ex->PartitionNumber = 0;
		partition_information_ex->RewritePartition = FALSE;
		partition_information_ex->Mbr.PartitionType = 0;
		partition_information_ex->Mbr.BootIndicator = FALSE;
		partition_information_ex->Mbr.RecognizedPartition = TRUE;
		partition_information_ex->Mbr.HiddenSectors = 0;
		Irp->IoStatus.Information = sizeof(PARTITION_INFORMATION_EX);
		status = STATUS_SUCCESS;
		break;
	

	case IOCTL_DISK_MEDIA_REMOVAL:

		KdPrint((" -> IOCTL_DISK_MEDIA_REMOVAL request \r\n"));
	
		Irp->IoStatus.Information = 0;
		status = STATUS_SUCCESS;
		break;
	

	case IOCTL_DISK_SET_PARTITION_INFO:
		KdPrint((" -> IOCTL_DISK_SET_PARTITION_INFO request \r\n"));

		if (irpsp->Parameters.DeviceIoControl.InputBufferLength <
			sizeof(SET_PARTITION_INFORMATION))
		{
			status = STATUS_INVALID_PARAMETER;
			Irp->IoStatus.Information = 0;
			break;
		}

		Irp->IoStatus.Information = 0;
		status = STATUS_SUCCESS;

		break;

	case IOCTL_DISK_VERIFY:

		KdPrint((" -> IOCTL_DISK_VERIFY request \r\n"));
	
		PVERIFY_INFORMATION verify_information;
		if (irpsp->Parameters.DeviceIoControl.InputBufferLength <
			sizeof(VERIFY_INFORMATION))
		{
			status = STATUS_INVALID_PARAMETER;
			Irp->IoStatus.Information = 0;
			break;
		}
		verify_information = (PVERIFY_INFORMATION)Irp->AssociatedIrp.SystemBuffer;
		status = STATUS_SUCCESS;
		Irp->IoStatus.Information = verify_information->Length;
		break;
	
	case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:

		KdPrint((" -> IOCTL_MOUNTDEV_QUERY_UNIQUE_ID request \r\n"));
	
		if (irpsp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(MOUNTDEV_UNIQUE_ID))
		{
			Irp->IoStatus.Information = sizeof(MOUNTDEV_UNIQUE_ID);
			status = STATUS_BUFFER_OVERFLOW;
		}
		else
		{
			PMOUNTDEV_UNIQUE_ID mountDevId = (PMOUNTDEV_UNIQUE_ID)Irp->AssociatedIrp.SystemBuffer;

			// WCHAR unique_id_buffer[MAXIMUM_FILENAME_LENGTH];
			// USHORT unique_id_length;

			// swprintf(unique_id_buffer, DIRECT_DISK_PREFIX L"%u", devId_);

			WCHAR unique_id_buffer[] = L"\\Device\\MyDiskLink0";	// TODO: Set the real Disk Device name
			USHORT unique_id_length;

			UNICODE_STRING uniqueId;
			RtlInitUnicodeString(&uniqueId, unique_id_buffer);
			unique_id_length = uniqueId.Length;

			mountDevId->UniqueIdLength = uniqueId.Length;
			int outLength = sizeof(USHORT) + uniqueId.Length;

			if (irpsp->Parameters.DeviceIoControl.OutputBufferLength < outLength)
			{
				status = STATUS_BUFFER_OVERFLOW;
				Irp->IoStatus.Information = sizeof(MOUNTDEV_UNIQUE_ID);
				break;
			}

			RtlCopyMemory(mountDevId->UniqueId, uniqueId.Buffer, uniqueId.Length);

			Irp->IoStatus.Information = outLength;
			status = STATUS_SUCCESS;
		}
	
		break;

	case IOCTL_DISK_GET_DISK_ATTRIBUTES:
		KdPrint((" -> IOCTL_DISK_GET_DISK_ATTRIBUTES request \r\n"));
		status = STATUS_NOT_IMPLEMENTED;
		break;

	case IOCTL_STORAGE_QUERY_PROPERTY:
		KdPrint((" -> IOCTL_STORAGE_QUERY_PROPERTY request \r\n"));

		Irp->IoStatus.Information = 0;
		status = STATUS_INVALID_DEVICE_REQUEST;

		break;

	case IOCTL_STORAGE_GET_HOTPLUG_INFO:
	
		if (irpsp->Parameters.DeviceIoControl.OutputBufferLength <
			sizeof(STORAGE_HOTPLUG_INFO))
		{
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		PSTORAGE_HOTPLUG_INFO hotplug =
			(PSTORAGE_HOTPLUG_INFO)Irp->AssociatedIrp.SystemBuffer;

		RtlZeroMemory(hotplug, sizeof(STORAGE_HOTPLUG_INFO));

		hotplug->Size = sizeof(STORAGE_HOTPLUG_INFO);
		hotplug->MediaRemovable = 1;

		Irp->IoStatus.Information = sizeof(STORAGE_HOTPLUG_INFO);
		status = STATUS_SUCCESS;
	
		break;

	case IOCTL_STORAGE_GET_DEVICE_NUMBER:
		KdPrint((" -> IOCTL_STORAGE_GET_DEVICE_NUMBER request \r\n"));

		Irp->IoStatus.Information = 0;
		status = STATUS_INVALID_DEVICE_REQUEST;

		break;

	case IOCTL_DISK_GET_MEDIA_TYPES:
		KdPrint((" -> IOCTL_DISK_GET_MEDIA_TYPES request \r\n"));

		Irp->IoStatus.Information = 0;
		status = STATUS_INVALID_DEVICE_REQUEST;

		break;

	case FSCTL_IS_VOLUME_MOUNTED:
		KdPrint((" -> FSCTL_IS_VOLUME_MOUNTED request ** \r\n"));
		status = STATUS_NOT_IMPLEMENTED;
		break;

	default:
		KdPrint((" -> Undefined IOCTL code \r\n"));
		DbgPrint(("IOCTL code: %ld \r\n"), irpsp->Parameters.DeviceIoControl.IoControlCode);
		status = STATUS_NOT_IMPLEMENTED;
	}

	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return status;

}

NTSTATUS IrpHandler(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp) {

	PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	switch (irpsp->MajorFunction) {

	case IRP_MJ_CREATE:
		if (DeviceObject == gMountDeviceObject) {
			KdPrint((" -> Control Device Create request\r\n"));
		}
		else {

			KdPrint((" -> Disk Create request\r\n"));

			// Create File
			status = CreateFileOnDisk(DeviceObject, Irp);

		}
		break;

	case IRP_MJ_CLOSE:
		if (DeviceObject == gMountDeviceObject) {
			KdPrint((" -> Control Device Close request\r\n"));
		}
		else {

			KdPrint((" -> Disk Close request\r\n"));

			// Close file
			status = CloseFileOnDisk(DeviceObject, Irp);

		}
		break;

	case IRP_MJ_READ:

		if (DeviceObject == gMountDeviceObject) {

			KdPrint((" -> Control Device Read request\r\n"));

		}
		else {

			KdPrint((" -> Disk Read request\r\n"));

		}
		status = STATUS_NOT_IMPLEMENTED;

		break;

	case IRP_MJ_WRITE:

		if (DeviceObject == gMountDeviceObject) {

			KdPrint((" -> Control Device Write request\r\n"));

		}
		else {

			KdPrint((" -> Disk Write request\r\n"));

		}

		status = STATUS_NOT_IMPLEMENTED;

		break;

	case IRP_MJ_CLEANUP:

		if (DeviceObject == gMountDeviceObject) {

			KdPrint((" -> Control Device CleanUp request\r\n"));

		}
		else {

			KdPrint((" -> Disk CleanUp request\r\n"));

		}

		status = STATUS_SUCCESS;

		break;

	case IRP_MJ_QUERY_INFORMATION:

		if (DeviceObject == gMountDeviceObject) {

			KdPrint((" -> Control Device QUERY_INFORMATION request\r\n"));
			status = STATUS_SUCCESS;

		}
		else {

			KdPrint((" -> Disk QUERY_INFORMATION request\r\n"));
			status = DispatchQueryInformation(DeviceObject, Irp);

		}

		break;

	case IRP_MJ_QUERY_VOLUME_INFORMATION:

		KdPrint((" -> Disk IRP_MJ_QUERY_VOLUME_INFORMATION request\r\n"));

		Irp->IoStatus.Information = 0;
		status = STATUS_INVALID_DEVICE_REQUEST;

		break;

	default:
		KdPrint((" -> Undefined IRP request\r\n"));
		status = STATUS_NOT_IMPLEMENTED;
		break;
	}

	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS CreateDiskDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp) {

	/*
	
	1. Create a device object for the disk by calling the IoCreateDevice function. Set the device type to FILE_DEVICE_DISK
	and the device characteristics to FILE_DEVICE_SECURE_OPEN.

	2. Create a device object for the volume by calling the IoCreateDevice function. Set the device type to
	FILE_DEVICE_DISK_FILE_SYSTEM.

	3. Set the VPB field of the volume device object to a new VPB structure by calling the IoAllocateVpb function.

	4. Set the DeviceObject field of the VPB structure to the volume device object.

	5. Set the RealDevice field of the VPB structure to the disk device object.

	6. Assign a drive letter to the volume by calling the IoCreateSymbolicLink function to create a symbolic link between the
	volume device object and a user-visible drive letter.

	*/

	//
	// Init variables
	//

	NTSTATUS status;
	PCONTROL_DEVICE_EXTENSION control_device_ext = (PCONTROL_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	PDRIVER_OBJECT DriverObject = control_device_ext->DriverObject;
	PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);
	PVOID buffer = Irp->AssociatedIrp.SystemBuffer;
	PDEVICE_OBJECT DiskDeviceObject;
	UNICODE_STRING DiskDeviceName;
	// UNICODE_STRING DiskDeviceSymLink;
	PDISK_DEVICE_EXTENSION disk_device_ext;
	PDEVICE_OBJECT VolumeDeviceObject;
	UNICODE_STRING VolumeDeviceName;
	PVOLUME_CONTROL_BLOCK Vcb;


	//
	// Create Disk Device
	//

	// Init Disk Device device name and symlink
	//RtlInitUnicodeString(&DiskDeviceName, L"\\Device\\MyDiskLink0");	// hardcode
	WCHAR disk_name_prefix_buffer[] = L"\\Device\\MyDiskLink";
	INT Length = wcslen(disk_name_prefix_buffer) + 1;
	PWCHAR disk_name_buffer = ExAllocatePool(NonPagedPool, Length * sizeof(WCHAR) + sizeof(INT));

	if (disk_name_buffer == NULL) {
		KdPrint((" -> ERROR: Failed to init Disk Device Name! \r\n"));
		return STATUS_UNSUCCESSFUL;
	}

	_snwprintf(disk_name_buffer, Length + 1, L"\\Device\\MyDiskLink%d", control_device_ext->DiskCount);

	RtlInitUnicodeString(&DiskDeviceName, disk_name_buffer);

	ExFreePool(disk_name_buffer);

	/*
	WCHAR buffer1[] = L"\\GLOBAL??\\";
	INT Length = wcslen(buffer1) + wcslen((PWCHAR)buffer) + 1;
	PWCHAR symlink_buffer = ExAllocatePool(NonPagedPool, Length * sizeof(WCHAR));

	if (symlink_buffer != NULL) {
		wcscpy(symlink_buffer, buffer1);
		wcscat(symlink_buffer, (PWCHAR)buffer);
		RtlInitUnicodeString(&DiskDeviceSymLink, symlink_buffer);
		ExFreePool(symlink_buffer);
	}
	*/

	// Create Disk Device
	status = IoCreateDevice(DriverObject,
							sizeof(DISK_DEVICE_EXTENSION),
							&DiskDeviceName,
							FILE_DEVICE_DISK,
							FILE_DEVICE_SECURE_OPEN,
							FALSE,
							&DiskDeviceObject);

	if (!NT_SUCCESS(status)) {
		KdPrint((" -> ERROR: Failed to create Disk Device! \r\n"));
		return status;
	}

	// DiskDeviceObject->Flags |= DO_DIRECT_IO;
	// DiskDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

	// Create symbolic link
	// status = IoCreateUnprotectedSymbolicLink(&DiskDeviceSymLink, &DiskDeviceName);

	// if (!NT_SUCCESS(status)) {
	//	KdPrint((" -> ERROR: Failed to create Disk Device symbolic link \r\n"));
	//	IoDeleteDevice(DiskDeviceObject);
	//	return status;
	// }


	//
	// Create Volume Device
	//

	// Init Volume Device device name
	// RtlInitUnicodeString(&VolumeDeviceName, L"\\Device\\MyVolumeLink0");	// hardcode
	WCHAR volume_name_prefix_buffer[] = L"\\Device\\MyVolumeLink";
	Length = wcslen(volume_name_prefix_buffer) + 1;
	PWCHAR volume_name_buffer = ExAllocatePool(NonPagedPool, Length * sizeof(WCHAR) + sizeof(INT));

	if (volume_name_buffer == NULL) {
		KdPrint((" -> ERROR: Failed to init Volume Device Name! \r\n"));
		return STATUS_UNSUCCESSFUL;
	}

	_snwprintf(volume_name_buffer, Length + 1, L"\\Device\\MyVolumeLink%d", control_device_ext->DiskCount);

	RtlInitUnicodeString(&VolumeDeviceName, volume_name_buffer);

	ExFreePool(volume_name_buffer);

	// Create Volume Device
	status = IoCreateDevice(DriverObject,
							sizeof(VOLUME_CONTROL_BLOCK),
							&VolumeDeviceName,
							FILE_DEVICE_DISK_FILE_SYSTEM,
							FILE_DEVICE_SECURE_OPEN,
							FALSE,
							&VolumeDeviceObject);

	if (!NT_SUCCESS(status)) {
		KdPrint((" -> ERROR: Failed to create Volume Device! \r\n"));
		IoDeleteDevice(DiskDeviceObject);
		return status;
	}

	VolumeDeviceObject->Flags |= DO_DIRECT_IO;
	VolumeDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;


	//
	// Init VCB
	//
	Vcb = (PVOLUME_CONTROL_BLOCK)VolumeDeviceObject->DeviceExtension;

	Vcb->VCBDeviceObject = VolumeDeviceObject;
	Vcb->DriverObject = DriverObject;

	Vcb->Identifier = ExAllocatePool(NonPagedPool, wcslen(buffer) * sizeof(WCHAR));

	if (Vcb->Identifier != NULL)
		wcscpy(Vcb->Identifier, (PWCHAR)buffer);


	// Link VCB with Volume Device assosiated Disk Device
	Vcb->TargetDeviceObject = DiskDeviceObject;


	// Add VCB to Control Device VCB list
	ACQUIRE_EX(control_device_ext->Resource);
	InsertHeadList(&(control_device_ext->FirstVCB), &(Vcb->NextVCB));
	RELEASE(control_device_ext->Resource);


	// Init VCB's list of FCB's
	InitializeListHead(&Vcb->FirstFCB);


	// Init VCB's list of Files
	InitializeListHead(&Vcb->FirstFile);


	// Init VCB ERESOURCE
	status = ExInitializeResourceLite(&Vcb->VCBResource);


	//
	// Init Disk Device extension
	//
	disk_device_ext = (PDISK_DEVICE_EXTENSION)DiskDeviceObject->DeviceExtension;
	disk_device_ext->Vcb = Vcb;
	disk_device_ext->DiskDeviceObject = DiskDeviceObject;


	//
	// Init VPB
	//

	Vcb->PtrVPB = DiskDeviceObject->Vpb;
	KIRQL Irql;
	IoAcquireVpbSpinLock(&Irql);
	Vcb->PtrVPB->DeviceObject = VolumeDeviceObject;
	Vcb->PtrVPB->Flags |= VPB_MOUNTED;
	VolumeDeviceObject->Vpb = Vcb->PtrVPB;
	Vcb->TargetDeviceObject->Flags &= (~DO_VERIFY_VOLUME);
	IoReleaseVpbSpinLock(Irql);

	DiskDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

	// Attach volume device to disk device
	// IoAttachDeviceToDeviceStack(VolumeDeviceObject, DiskDeviceObject);


	//
	// Return Disk Device name to user app
	//
	wcscpy(buffer, DiskDeviceName.Buffer);

	// DbgPrint("Disk Device name to user app = %ws \r\n", (PWCHAR)buffer);	// DEBUG - OK

	ULONG returnedLen = (wcsnlen(buffer, 511) + 1) * 2;
	ULONG inLength = irpsp->Parameters.DeviceIoControl.InputBufferLength;
	ULONG outLength = irpsp->Parameters.DeviceIoControl.OutputBufferLength;

	Irp->IoStatus.Information = returnedLen;

	return status;
}

VOID DeleteDiskDevice(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp) {
	
	// KdPrint(("[DeleteDiskDevice] Start \r\n"));

	PCONTROL_DEVICE_EXTENSION control_device_ext = (PCONTROL_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);
	PVOID buffer = Irp->AssociatedIrp.SystemBuffer;

	//
	// Find necessary VCB by identificator(Letter)
	//
	PVCB Vcb = FindVcbByIdentificator(&(control_device_ext->FirstVCB), (PWCHAR)buffer);

	if (Vcb == NULL) {
		KdPrint((" -> ERROR: No such Disk! \r\n"));
		return;
	}

	//
	// Remove VCB from Control Device VCB List
	//
	ACQUIRE_EX(control_device_ext->Resource);
	RemoveEntryList(&Vcb->NextVCB);
	RELEASE(control_device_ext->Resource);

	//
	// Clear VCB
	//
	FreeVCB(Vcb);

	// KdPrint(("[DeleteDiskDevice] End \r\n"));
	
}

NTSTATUS CreateFileOnDisk(PDEVICE_OBJECT VolumeDevice, PIRP Irp) {

	//
	// Init necessary data structures
	//
	PFILE_OBJECT NewFileObject;
	PFILE_OBJECT RelatedFileObject;
	UNICODE_STRING TargetObjectName;
	PIO_STACK_LOCATION irpsp;
	PVCB Vcb = NULL;
	PFCB Fcb = NULL;
	PCCB Ccb = NULL;
	ULONG CreateOptions;
	ULONG CreateDisposition;
	USHORT FileAttributes;
	PFILE_ON_DISK File;
	PDISK_DEVICE_EXTENSION disk_device_ext;

	//
	// Fill necessary data structures
	//
	irpsp = IoGetCurrentIrpStackLocation(Irp);
	NewFileObject = irpsp->FileObject;
	RelatedFileObject = NewFileObject->RelatedFileObject;
	TargetObjectName = NewFileObject->FileName;
	Vcb = (PVCB)VolumeDevice->DeviceExtension;
	CreateOptions = irpsp->Parameters.Create.Options & FILE_VALID_OPTION_FLAGS;
	CreateDisposition = (irpsp->Parameters.Create.Options >> 24) & 0xFF;
	FileAttributes = irpsp->Parameters.Create.FileAttributes;

	//
	// Debug print
	//
	DbgPrint(("[CreateFileOnDisk] File name: %wZ \r\n"), &TargetObjectName);	// if we call CreateFile for our disk symLink, 
																				// TargetObjectName will be empty

	if (CreateDisposition == FILE_CREATE)
		DbgPrint(("Creation disposition: FILE_CREATE \r\n"));

	if (CreateDisposition == FILE_OPEN)
		DbgPrint(("Creation disposition: FILE_OPEN \r\n"));

	if (CreateDisposition == FILE_SUPERSEDE)
		DbgPrint(("Creation disposition: FILE_SUPERSEDE \r\n"));

	if (CreateDisposition == FILE_OPEN_IF)
		DbgPrint(("Creation disposition: FILE_OPEN_IF \r\n"));

	if (CreateDisposition == FILE_OVERWRITE)
		DbgPrint(("Creation disposition: FILE_OVERWRITE \r\n"));

	if (CreateDisposition == FILE_OVERWRITE_IF)
		DbgPrint(("Creation disposition: FILE_OVERWRITE_IF \r\n"));

	//
	// Return STATUS_SUCCESS if disk device is opened
	//
	if (TargetObjectName.Length == 0 && RelatedFileObject == NULL)
		return STATUS_SUCCESS;

	//
	// Search in list of existing files
	//
	File = FindFileByName(Vcb, &TargetObjectName);


	//
	// if OPEN_EXISTING file request
	//
	if (CreateDisposition == FILE_OPEN) {

		if (File == NULL) {
			KdPrint((" -> ERROR: There is no such file to open! \r\n"));
			return STATUS_NO_SUCH_FILE;
		}
		else {
			Fcb = FindFcbByIdentificator(&Vcb->FirstFCB, &TargetObjectName);
		}

	}

	//
	// if CREATE_ALWAYS file request
	//
	if (File != NULL && CreateDisposition == FILE_CREATE) {
		KdPrint((" -> ERROR: There is already such file! \r\n"));
		return STATUS_UNSUCCESSFUL;
	}

	//
	// Init FCB if it is first-time-opened file
	//
	if (Fcb == NULL) {

		Fcb = AllocateFcb();

		// Allocation is unsuccessful
		if (Fcb == NULL) {
			KdPrint((" -> ERROR: Failed to allocate FCB! \r\n"));
			return STATUS_MEMORY_NOT_ALLOCATED;
		}

		// Fill FCB fields
		Fcb->PtrVCB = Vcb;
		Fcb->Identifier = TargetObjectName;

		// Add FCB to VCB's list of FCBs
		ACQUIRE_EX(Vcb->VCBResource);
		InsertHeadList(&(Vcb->FirstFCB), &(Fcb->NextFCB));
		RELEASE(Vcb->VCBResource);

		// Increment VCB's OpenHandleCount
		Vcb->VCBOpenCount++;

		// KdPrint(("[CreateFileOnDisk] Init FCB if it is first-time-opened file FINISHED \r\n"));

	}

	// KdPrint(("[CreateFileOnDisk] Init FCB FINISHED \r\n"));

	//
	// Init CCB structure for opening this file process
	//
	Ccb = AllocateCcb();

	// Allocation is unsuccessful
	if (Ccb == NULL) {
		KdPrint((" -> ERROR: Failed to allocate CCB! \r\n"));
		return STATUS_MEMORY_NOT_ALLOCATED;
	}

	// Fill CCB fields
	Ccb->Id = Fcb->CCBCounter++;
	Ccb->PtrFCB = Fcb;
	Ccb->PtrFileObject = NewFileObject;

	// Add CCB to FCB list of CCBs
	ACQUIRE_EX(Fcb->FCBResource);
	InsertTailList(&(Fcb->FirstCCB), &(Ccb->NextCCB));
	RELEASE(Fcb->FCBResource);

	// Increment FCB's OpenHandleCount
	Fcb->OpenHandleCount++;

	// KdPrint(("[CreateFileOnDisk] Init CCB structure for opening this file process FINISHED \r\n"));

	//
	// Fill FileObject fields
	//
	NewFileObject->FsContext = (&(&Fcb->NTRequiredFCB)->CommonFCBHeader);
	NewFileObject->FsContext2 = Ccb;
	NewFileObject->SectionObjectPointer = &(&Fcb->NTRequiredFCB)->SectionObject;

	// KdPrint(("[CreateFileOnDisk] Fill FileObject fields FINISHED \r\n"));

	//
	// If it is CREATE_ALWAYS request -> Add File to <list of existing files>
	//
	if (CreateDisposition == FILE_CREATE) {

		File = AllocateFileOnDisk();

		// Allocation is unsuccessful
		if (File == NULL) {
			KdPrint((" -> ERROR: Failed to allocate FILE_ON_DISK! \r\n"));
			FreeCCB(Ccb);
			return STATUS_MEMORY_NOT_ALLOCATED;
		}

		// Fill FILE_ON_DISK fields
		File->IsOpened = TRUE;

		File->name.Length = 0;
		File->name.Buffer = (PWCH)ExAllocatePool(NonPagedPool, TargetObjectName.Length * sizeof(WCHAR));
		File->name.MaximumLength = TargetObjectName.Length;
		RtlUnicodeStringCopy(&File->name, &TargetObjectName);

		File->Attributes = FileAttributes;

		// Add FILE_ON_DISK to VCB's list of FILE_ON_DISKs
		ACQUIRE_EX(Vcb->VCBResource);
		InsertTailList(&(Vcb->FirstFile), &(File->NextFile));
		RELEASE(Vcb->VCBResource);

	}

	// Add FILE_ON_DISK pointer to FCB
	Fcb->File = File;

	// KdPrint(("[CreateFileOnDisk] Add File to <list of existing files> FINISHED \r\n"));
	// KdPrint(("[CreateFileOnDisk] End \r\n"));

	Irp->IoStatus.Information = FILE_OPENED;

	return STATUS_SUCCESS;

}

NTSTATUS CloseFileOnDisk(PDEVICE_OBJECT DiskDevice, PIRP Irp) {

	PIO_STACK_LOCATION irpsp;
	PFILE_OBJECT FileObject;
	PVCB Vcb = NULL;
	PFCB Fcb = NULL;
	PCCB Ccb = NULL;

	irpsp = IoGetCurrentIrpStackLocation(Irp);
	FileObject = irpsp->FileObject;
	Ccb = FileObject->FsContext2;
	Fcb = Ccb->PtrFCB;
	Vcb = Fcb->PtrVCB;


	//
	// Delete CCBs assosiated with file
	//
	ACQUIRE_EX(Fcb->FCBResource);
	RemoveEntryList(&Ccb->NextCCB);
	RELEASE(Fcb->FCBResource);

	Fcb->OpenHandleCount--;

	FreeCCB(Ccb);

	// KdPrint(("[CloseFileOnDisk] Delete file assosiated CCB FINISHED \r\n"));

	//
	// Delete FCB if OpenHandleCount == 0
	//
	if (Fcb->OpenHandleCount == 0) {

		ACQUIRE_EX(Vcb->VCBResource);
		RemoveEntryList(&Fcb->NextFCB);
		RELEASE(Vcb->VCBResource);

		(Fcb->File)->IsOpened = FALSE;

		FreeFCB(Fcb);

	}

	// KdPrint(("[CloseFileOnDisk] End \r\n"));

	Irp->IoStatus.Information = 0;

	return STATUS_SUCCESS;
}

LARGE_INTEGER intToLargeInt(int i) {
	LARGE_INTEGER li;
	li.QuadPart = i;
	return li;
}

NTSTATUS DispatchQueryInformation(PDEVICE_OBJECT DeviceObject, PIRP Irp) {

	//
	// Init variables
	//
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);
	PFILE_OBJECT FileObject = irpsp->FileObject;
	UNICODE_STRING FileName = FileObject->FileName;
	FILE_INFORMATION_CLASS FileInfoClass = irpsp->Parameters.QueryFile.FileInformationClass;
	PFILE_BASIC_INFORMATION Buffer = (PFILE_BASIC_INFORMATION)Irp->AssociatedIrp.SystemBuffer;
	ULONG fileAttributes;
	PVCB Vcb = (PVCB)DeviceObject->DeviceExtension;
	PFILE_ON_DISK File;


	//
	// TODO: VolumeInformation request
	//


	//
	// Check if FILE_OBJECT is valid
	//
	if (FileObject == NULL)
	{
		status = STATUS_INVALID_PARAMETER;
		return status;
	}

	//
	// Check if it is FileBasicInformation request (for handling GetFileAttributes)
	//
	if (irpsp->Parameters.QueryFile.FileInformationClass != FileBasicInformation)
	{
		KdPrint((" -> ERROR: Not implemented File Information request! \r\n"));
		status = STATUS_INVALID_PARAMETER;
		return status;
	}

	//
	// Find necessary file by name
	//
	File = FindFileByName(Vcb, &FileName);

	//
	// Set file attributes
	//
	fileAttributes = File->Attributes;

	//
	// Fill output IRP information buffer
	//
	RtlZeroMemory(Buffer, sizeof(FILE_BASIC_INFORMATION));
	Buffer->FileAttributes = fileAttributes;
	Buffer->CreationTime = intToLargeInt(1000000000);
	Buffer->ChangeTime = intToLargeInt(1000000000);
	Buffer->LastAccessTime = intToLargeInt(1000000000);
	Buffer->LastWriteTime = intToLargeInt(1000000000);

	Irp->IoStatus.Information = sizeof(FILE_BASIC_INFORMATION);

	return STATUS_SUCCESS;
}

VOID DriverUnload(IN PDRIVER_OBJECT DriverObject) {

	KdPrint((" -> Driver unloading... \r\n"));

	PDEVICE_OBJECT ControlDeviceObj = gMountDeviceObject;
	PCONTROL_DEVICE_EXTENSION control_device_ext = (PCONTROL_DEVICE_EXTENSION)ControlDeviceObj->DeviceExtension;
	PUNICODE_STRING pControlDeviceSymLink = &(control_device_ext->SymLink);

	//
	// Unload all Disk Devices
	//
	PVCB Vcb;
	PLIST_ENTRY head = &(control_device_ext->FirstVCB);
	PLIST_ENTRY currentEntry;

	while (!IsListEmpty(head)) {

		ACQUIRE_EX(control_device_ext->Resource);
		currentEntry = RemoveHeadList(head);
		RELEASE(control_device_ext->Resource);
		Vcb = CONTAINING_RECORD(currentEntry, VOLUME_CONTROL_BLOCK, NextVCB);

		// DbgPrint("[DriverUnload] Delete Disk with identifier %ws \r\n", Vcb->Identifier);	// DEBUG

		FreeVCB(Vcb);

	}

	// KdPrint(("[DriverUnload] Unload all Disk Devices FINISHED \r\n"));

	//
	// Unload Control Device
	//
	ExDeleteResourceLite(&control_device_ext->Resource);
	IoDeleteSymbolicLink(pControlDeviceSymLink);
	IoDeleteDevice(ControlDeviceObj);

	// KdPrint(("[DriverUnload] Unload Control Device FINISHED \r\n"));

	KdPrint((" -> Driver unloaded \r\n"));

}

NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status, ULONG info)
{
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}