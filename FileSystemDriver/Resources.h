#pragma once
#include <wchar.h>
#include "ntifs.h"
#include "Extensions.h"

VOID FreeVCB(
	IN PVCB Vcb);

VOID FreeFCB(
	IN PFCB Fcb);

VOID FreeCCB(
	IN PCCB Ccb);

VOID FreeFileOnDisk(
	IN PFILE_ON_DISK File);

PVCB FindVcbByIdentificator(
	IN PLIST_ENTRY head,
	IN PWCHAR Identificator);

PFCB FindFcbByIdentificator(
	IN PLIST_ENTRY head,
	IN PUNICODE_STRING Identificator);

PFILE_ON_DISK FindFileByName(
	IN PVCB Vcb,
	IN PUNICODE_STRING Name);

PFCB AllocateFcb();

PCCB AllocateCcb();

PFILE_ON_DISK AllocateFileOnDisk();



VOID FreeVCB(IN PVCB Vcb) {

	// KdPrint(("[FreeVCB] Start \r\n"));

	PDEVICE_OBJECT VolumeDeviceObject = Vcb->VCBDeviceObject;
	PDEVICE_OBJECT DiskDeviceObject = Vcb->TargetDeviceObject;
	PDISK_DEVICE_EXTENSION disk_device_ext = (PDISK_DEVICE_EXTENSION)DiskDeviceObject->DeviceExtension;

	//
	// Delete symbolic link
	//
	// UNICODE_STRING symLink = Vcb->SymLink;
	// NTSTATUS status = IoDeleteSymbolicLink(&symLink);
	// if (NT_SUCCESS(status))
	// 	KdPrint(("[FreeVCB] Delete symlink FINISHED \r\n"));

	//
	// Delete all assosiated FCBs
	//
	PFCB Fcb = NULL;
	PLIST_ENTRY head = &(Vcb->FirstFCB);
	PLIST_ENTRY currentEntry;

	while (!IsListEmpty(head)) {

		currentEntry = RemoveTailList(head);
		Fcb = CONTAINING_RECORD(currentEntry, FILE_CONTROL_BLOCK, NextFCB);

		FreeFCB(Fcb);
	}

	KdPrint(("[FreeVCB] Delete all assosiated FCB FINISHED \r\n"));

	//
	// Delete all assosiated FILE_ON_DISKs
	//
	PFILE_ON_DISK File = NULL;
	head = &(Vcb->FirstFile);

	while (!IsListEmpty(head)) {

		currentEntry = RemoveTailList(head);
		File = CONTAINING_RECORD(currentEntry, FILE_ON_DISK, NextFile);

		FreeFileOnDisk(File);
	}

	KdPrint(("[FreeVCB] Delete all assosiated FILE_ON_DISKs FINISHED \r\n"));

	//
	// Free ERESOURCE
	//
	ExDeleteResourceLite(&Vcb->VCBResource);


	//
	// Free Identifier wchar array
	//
	ExFreePool(Vcb->Identifier);


	//
	// Delete Volume Device and Disk Device Object
	//
	IoDeleteDevice(VolumeDeviceObject);
	IoDeleteDevice(DiskDeviceObject);	// Don't work -> ERROR ???

	// KdPrint(("[FreeVCB] End \r\n"));

}

VOID FreeFCB(IN PFCB Fcb) {

	// KdPrint(("[FreeFCB] Start \r\n"));

	//
	// Delete all assosiated CCBs
	//
	PCCB Ccb = NULL;
	PLIST_ENTRY head = &(Fcb->FirstCCB);
	PLIST_ENTRY currentEntry;

	while (!IsListEmpty(head)) {

		currentEntry = RemoveTailList(head);
		Ccb = CONTAINING_RECORD(currentEntry, CONTEXT_CONTROL_BLOCK, NextCCB);

		FreeCCB(Ccb);
	}

	KdPrint(("[FreeFCB] Delete all assosiated CCB FINISHED \r\n"));

	//
	// Free all ERESOURCEs
	//
	ExDeleteResourceLite(&Fcb->FCBResource);
	ExDeleteResourceLite(&(&Fcb->NTRequiredFCB)->MainResource);
	ExDeleteResourceLite(&(&Fcb->NTRequiredFCB)->PagingloResource);

	KdPrint(("[FreeFCB] Free all ERESOURCEs FINISHED \r\n"));

	//
	// Free memory pool
	//
	ExFreePool(Fcb);

	// KdPrint(("[FreeFCB] End \r\n"));

	KdPrint(("[FreeFCB] End \r\n"));

}

VOID FreeCCB(IN PCCB Ccb) {

	// KdPrint(("[FreeCCB] Start \r\n"));

	//
	// Free memory pool
	//
	ExFreePool(Ccb);

	// KdPrint(("[FreeCCB] End \r\n"));

}

VOID FreeFileOnDisk(IN PFILE_ON_DISK File) {

	// KdPrint(("[FreeFileOnDisk] Start \r\n"));

	ExFreePool(File->name.Buffer);

	//
	// Free memory pool
	//
	ExFreePool(File);

	// KdPrint(("[FreeFileOnDisk] End \r\n"));

}

PVCB FindVcbByIdentificator(IN PLIST_ENTRY head, IN PWCHAR Identificator) {

	PLIST_ENTRY start = head->Flink;
	PVCB Vcb;

	if (start == NULL)
		// There is not any VCB yet
		return NULL;

	while (start != head) {

		Vcb = CONTAINING_RECORD(start, VOLUME_CONTROL_BLOCK, NextVCB);

		if (wcscmp(Vcb->Identifier, Identificator) == 0) {
			return Vcb;
		}

		start = start->Flink;
	}

	return NULL;

}

PFCB FindFcbByIdentificator(IN PLIST_ENTRY head, IN PUNICODE_STRING Identificator) {

	// KdPrint(("[FindFcbByIdentificator] Start \r\n"));

	PLIST_ENTRY start = head->Flink;
	PFCB Fcb;

	if (start == NULL)
		// There is not any FCB yet
		return NULL;

	while (start != head) {

		Fcb = CONTAINING_RECORD(start, FILE_CONTROL_BLOCK, NextFCB);

		if (RtlCompareUnicodeString(&Fcb->Identifier, Identificator, FALSE) == 0) {
			KdPrint(("[FindFcbByIdentificator] Find Fcb by Id\r\n"));
			return Fcb;
		}

		start = start->Flink;
	}

	// KdPrint(("[FindFcbByIdentificator] End \r\n"));

	return NULL;

}

PFILE_ON_DISK FindFileByName(IN PVCB Vcb, IN PUNICODE_STRING Name) {

	// KdPrint(("[FindFileByName] Start \r\n"));

	PLIST_ENTRY head = &Vcb->FirstFile;
	PLIST_ENTRY start = head->Flink;
	PFILE_ON_DISK File;

	if (start == NULL) {
		// There is not any files yet
		KdPrint(("[FindFileByName] There is not any files on disk yet \r\n"));
		return NULL;
	}

	while (start != head) {

		File = CONTAINING_RECORD(start, FILE_ON_DISK, NextFile);

		if (File == NULL)
			return NULL;

		// DbgPrint(("Search name: %wZ \r\n"), Name);
		// DbgPrint(("Current file name: %wZ \r\n"), &File->name);

		if (RtlCompareUnicodeString(&File->name, Name, FALSE) == 0) {
			KdPrint(("[FindFileByName] Find File by name \r\n"));
			return File;
		}

		start = start->Flink;

	}

	// KdPrint(("[FindFileByName] End \r\n"));

	return NULL;

}

PFCB AllocateFcb() {

	PFCB newFcb;

	newFcb = (PFCB)ExAllocatePool(NonPagedPool, sizeof(FILE_CONTROL_BLOCK));

	if (newFcb != NULL) {

		newFcb->ReferenceCount = 0;
		newFcb->OpenHandleCount = 0;
		newFcb->CCBCounter = 0;

		ExInitializeResourceLite(&(newFcb->FCBResource));
		ExInitializeResourceLite(&(newFcb->NTRequiredFCB.MainResource));
		ExInitializeResourceLite(&(newFcb->NTRequiredFCB.PagingloResource));

		InitializeListHead(&(newFcb->FirstCCB));
	}

	return newFcb;

}

PCCB AllocateCcb() {

	PCCB newCcb;

	newCcb = (PCCB)ExAllocatePool(NonPagedPool, sizeof(CONTEXT_CONTROL_BLOCK));

	return newCcb;

}

PFILE_ON_DISK AllocateFileOnDisk() {

	PFILE_ON_DISK newFile;

	newFile = (PFILE_ON_DISK)ExAllocatePool(NonPagedPool, sizeof(FILE_ON_DISK));

	if (newFile != NULL)
		newFile->IsOpened = FALSE;

	return newFile;

}