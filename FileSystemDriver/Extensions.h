#pragma once
// #include <ntddk.h>
#include <ntifs.h>
#include <ntdddisk.h>
#include <mountmgr.h>
#include <mountdev.h>

// Resource locks
#define ACQUIRE(s) ExAcquireResourceSharedLite(&(s), TRUE)
#define ACQUIRE_EX(s) ExAcquireResourceExclusiveLite(&(s), TRUE)
#define RELEASE(s) ExReleaseResourceForThreadLite(&(s), ExGetCurrentResourceThread())

typedef struct _FILE_ON_DISK {

	UNICODE_STRING name;

	BOOLEAN IsOpened;

	LIST_ENTRY NextFile;

	ULONG Attributes;

} FILE_ON_DISK, * PFILE_ON_DISK;

typedef struct _CONTROL_DEVICE_EXTENSION {

	//  symbolic link name
	UNICODE_STRING SymLink;

	// Pointer to Driver Object
	PDRIVER_OBJECT DriverObject;

	// the fields in this list are protected by the following resource
	ERESOURCE Resource;

	// Linked list of VCB
	LIST_ENTRY FirstVCB;

	// Counter of Disk Devices
	INT DiskCount;

} CONTROL_DEVICE_EXTENSION, * PCONTROL_DEVICE_EXTENSION;


// _DISK_DEVICE_EXTENSION = Device Extension + Volume Control Block (VCB)
typedef struct _VOLUME_CONTROL_BLOCK {

	// A:, B:, ...
	PWCHAR Identifier;

	//  symbolic link name
	// UNICODE_STRING SymLink;	// Delete

	// Pointer to Driver Object
	PDRIVER_OBJECT DriverObject;

	// a resource to protect the fields contained within the VCB
	ERESOURCE VCBResource;

	// list of all FCB structures associated with the VCB
	LIST_ENTRY FirstFCB;

	// list of all FILE_ON_DISKs associated with the VCB
	LIST_ENTRY FirstFile;

	// list of all ÑCB structures associated with the VCB
	// PContextControlBlock FirstCCB;

	// each VCB points to a VPB structure created by the NT I/O Manager
	PVPB PtrVPB;

	// A count of the number of open files/directories
	// As long as the count is != 0, the volume cannot
	// be dismounted or locked.
	INT VCBOpenCount;

	// for each mounted volume, we create a device object. Here then
	// is a back pointer to that device object
	PDEVICE_OBJECT VCBDeviceObject;

	// We also retain a pointer to the physical device object, which we
	// have mounted ourselves. The I/O Manager passes us a pointer to this
	// device object when requesting a mount operation.
	PDEVICE_OBJECT TargetDeviceObject;

	// Required to use the Cache Manager.
	SECTION_OBJECT_POINTERS SectionObject;

	// each VCB is accessible on a global linked list
	LIST_ENTRY NextVCB;

} VOLUME_CONTROL_BLOCK, * PVOLUME_CONTROL_BLOCK, * PVCB;


typedef struct _NTRequiredFCB {

	FSRTL_COMMON_FCB_HEADER CommonFCBHeader;

	SECTION_OBJECT_POINTERS SectionObject;

	ERESOURCE MainResource;

	ERESOURCE PagingloResource;

} NTRequiredFCB, * PNTRequiredFCB;


typedef struct _FILE_CONTROL_BLOCK {
	UNICODE_STRING Identifier;

	// We embed the "NT Required FCB" right here.
	NTRequiredFCB NTRequiredFCB;

	// this FCB belongs to some mounted logical volume
	PVCB PtrVCB;

	// a resource to protect the fields contained within the FCB
	ERESOURCE FCBResource;

	// to be able to access all open file(s) for a volume, we will
	// link all FCB structures for a logical volume together
	LIST_ENTRY NextFCB;

	// all CCBs for this particular FCB are linked off the following list head.
	LIST_ENTRY FirstCCB;

	// Counter of CCBs associated with this FCB
	INT CCBCounter;

	// whenever a file stream has a create/open operation performed,
	// the Reference count below is incremented AND the OpenHandle count
	// below is also incremented.
	// When an IRP_MJ_CLEANUP is received, the OpenHandle count below
	// is decremented.
	// When an IRP_MJ_CLOSE is received, the Reference count below is
	// decremented.
	// When the Reference count goes down to zero, the FCB can be
	// de-allocated.
	// Zero Reference count implies a zero OpenHandle count.
	UINT32 ReferenceCount;
	UINT32 OpenHandleCount;

	// Pointer to assosiated File on Disk
	PFILE_ON_DISK File;

} FILE_CONTROL_BLOCK, * PFILE_CONTROL_BLOCK, * PFCB;


// one CCB for each open file handle
typedef struct _CONTEXT_CONTROL_BLOCK {

	INT Id;

	// Pointer to the associated FCB
	PFCB PtrFCB;

	// a list of CCB's 
	LIST_ENTRY NextCCB;

	// each CCB is associated with a file object
	PFILE_OBJECT PtrFileObject;

} CONTEXT_CONTROL_BLOCK, * PCONTEXT_CONTROL_BLOCK, * PCCB;

typedef struct _DISK_DEVICE_EXTENSION {

	// Assosiated Volume Device VCB
	PVCB Vcb;

	PDEVICE_OBJECT DiskDeviceObject;

} DISK_DEVICE_EXTENSION, * PDISK_DEVICE_EXTENSION;


