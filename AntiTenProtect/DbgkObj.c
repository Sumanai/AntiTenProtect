#include "DbgkObj.h"
FAST_MUTEX  codevDbgkpProcessDebugPortMutex;//�Լ�����һ�����ٻ�����
POBJECT_TYPE codevDbgkDebugObjectType=NULL;//�Լ�ʵ��һ�����Զ������ͣ�����
PDEBUG_OBJECT codevDebugObject = NULL;//�Լ�ά��DEBUG_OBJECT
PEPROCESS codevDebuggedProcess = NULL;//��¼�±����Խ��̶���
//
VOID
__stdcall
codevDbgkpMarkProcessPeb (
    PEPROCESS Process
    )
{
	KAPC_STATE ApcState;
	EX_RUNDOWN_REF* RundownProtect;
	PVOID Peb;
	UCHAR* BeingDebugged;
	PDEBUG_OBJECT DebugPort;
	PRKPROCESS PKprocess;
	PAGED_CODE ();
	PKprocess = (PRKPROCESS)Process;
	//��ʼ��PEPROCESS�ı���
	if (bWin7)
	{
		RundownProtect = (EX_RUNDOWN_REF*)((ULONG)Process + RundownProtectOffsetWin7);
		Peb = (PVOID)(*(PULONG)((ULONG)Process + PebOffsetWin7));
		BeingDebugged = (UCHAR*)((ULONG)Peb + 0x2);
	}
	else
	{
		RundownProtect = (EX_RUNDOWN_REF*)((ULONG)Process + RundownProtectOffsetXp);
		Peb = (PVOID)(*(PULONG)((ULONG)Process + PebOffsetXp));
		BeingDebugged = (UCHAR*)((ULONG)Peb + 0x2);
	}
	//
	// Acquire process rundown protection as we are about to look at the processes address space
	//
	if (ExAcquireRundownProtection (RundownProtect)) 
	{

		if (Peb != NULL) 
		{
			codevKeStackAttachProcess(PKprocess, &ApcState);


			codevExAcquireFastMutex (&codevDbgkpProcessDebugPortMutex);

			__try {
				//���ﻹҪ�ж������Լ���debugobject
				//����ý����Ǳ����Խ���
				if (Process == codevDebuggedProcess)
				{
					*BeingDebugged = (BOOL)(codevDebugObject != NULL ? TRUE : FALSE);
				}
				else
				{
					*BeingDebugged = (BOOL)(DebugPort != NULL ? TRUE : FALSE);
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			codevExReleaseFastMutex (&codevDbgkpProcessDebugPortMutex);

			codevKeUnstackDetachProcess(&ApcState);

		}

		ExReleaseRundownProtection (RundownProtect);
	}
}
//DbgkpWakeTarget���������DbgkpFreeDebugEvent����û�и���
VOID __stdcall codevDbgkpWakeTarget (
	IN PDEBUG_EVENT DebugEvent
	)
{
	DBGKPWAKETARGET pfn_DbgkpWakeTarget;
	if (!MmIsAddressValidEx((PVOID)SystemKernelModuleBase) ||
		!MmIsAddressValidEx(ReloadNtosImageBase))
	{
		return;
	}
	pfn_DbgkpWakeTarget = (DBGKPWAKETARGET)(OriginDbgkpWakeTarget - SystemKernelModuleBase + (ULONG)ReloadNtosImageBase);
	return pfn_DbgkpWakeTarget(DebugEvent);
}
//
VOID __stdcall codevDbgkpDeleteObject(IN  PVOID  Object)
{
	DBGKPDELETEOBJECT pfn_DbgkpDeleteObject;
	if (!MmIsAddressValidEx((PVOID)SystemKernelModuleBase) ||
		!MmIsAddressValidEx(ReloadNtosImageBase))
	{
		return;
	}
	pfn_DbgkpDeleteObject = (DBGKPDELETEOBJECT)(OriginDbgkpDeleteObject - SystemKernelModuleBase + (ULONG)ReloadNtosImageBase);
	return pfn_DbgkpDeleteObject(Object);
}
//
VOID __stdcall codevDbgkpCloseObject (
	IN PEPROCESS Process,
	IN PVOID Object,
	IN ACCESS_MASK GrantedAccess,
	IN ULONG_PTR ProcessHandleCount,
	IN ULONG_PTR SystemHandleCount
	)
{

	PDEBUG_OBJECT DebugObject = (PDEBUG_OBJECT)Object;
	PDEBUG_OBJECT DebugPort;//Ϊ�˲���EPROCESSֱ��bc��
	PDEBUG_EVENT DebugEvent;
	PLIST_ENTRY ListPtr;
	BOOLEAN Deref;
	BOOLEAN DerefEx;//����һ��Ex��ר�Ÿ㶨�����Լ�ά����debugobject
	PAGED_CODE ();

	UNREFERENCED_PARAMETER (GrantedAccess);
	UNREFERENCED_PARAMETER (ProcessHandleCount);

	//
	// If this isn't the last handle then do nothing.
	//
	if (SystemHandleCount > 1) {
		return;
	}

	codevExAcquireFastMutex (&DebugObject->Mutex);

	//
	// Mark this object as going away and wake up any processes that are waiting.
	//
	DebugObject->Flags |= DEBUG_OBJECT_DELETE_PENDING;

	//
	// Remove any events and queue them to a temporary queue
	//
	ListPtr = DebugObject->EventList.Flink;
	InitializeListHead (&DebugObject->EventList);

	codevExReleaseFastMutex (&DebugObject->Mutex);

	//
	// Wake anyone waiting. They need to leave this object alone now as its deleting
	//
	KeSetEvent (&DebugObject->EventsPresent, 0, FALSE);

	//
	// Loop over all processes and remove the debug port from any that still have it.
	// Debug port propagation was disabled by setting the delete pending flag above so we only have to do this
	// once. No more refs can appear now.
	//
	for (Process = codevPsGetNextProcess(NULL);Process != NULL;Process = codevPsGetNextProcess (Process)) 
	{
		if (bWin7)
		{
			DebugPort = (PDEBUG_OBJECT)(*(PULONG)((ULONG)Process+DebugPortOffsetWin7));
		}
		else
		{
			DebugPort = (PDEBUG_OBJECT)(*(PULONG)((ULONG)Process+DebugPortOffsetXp));
		}
		if (DebugPort == DebugObject) 
		{
			Deref = FALSE;
			codevExAcquireFastMutex (&codevDbgkpProcessDebugPortMutex);
			if (DebugPort == DebugObject) 
			{
				DebugPort = NULL;
				Deref = TRUE;
			}
			codevExReleaseFastMutex (&codevDbgkpProcessDebugPortMutex);
			if (Deref) 
			{
				codevDbgkpMarkProcessPeb(Process);
				//
				// If the caller wanted process deletion on debugger dying (old interface) then kill off the process.
				//
				if (DebugObject->Flags&DEBUG_OBJECT_KILL_ON_CLOSE) 
				{
					codevPsTerminateProcess (Process, STATUS_DEBUGGER_INACTIVE);
				}
				ObDereferenceObject (DebugObject);
			}
		}//�ر������Լ��ĵ��Զ���
		else if (codevDebugObject == DebugObject) 
		{
			DerefEx = FALSE;
			codevExAcquireFastMutex (&codevDbgkpProcessDebugPortMutex);
			if (codevDebugObject == DebugObject) 
			{
				codevDebugObject = NULL;
				DerefEx = TRUE;
			}
			codevExReleaseFastMutex (&codevDbgkpProcessDebugPortMutex);
			if (DerefEx) 
			{
				codevDbgkpMarkProcessPeb(Process);//������̾��Ǳ����Խ�����

				//
				// If the caller wanted process deletion on debugger dying (old interface) then kill off the process.
				//
				if (DebugObject->Flags&DEBUG_OBJECT_KILL_ON_CLOSE) 
				{
					codevPsTerminateProcess (Process, STATUS_DEBUGGER_INACTIVE);
				}
				ObDereferenceObject (DebugObject);
			}
		}

	}
	//
	// Wake up all the removed threads.
	//
	while (ListPtr != &DebugObject->EventList) {
		DebugEvent = CONTAINING_RECORD (ListPtr, DEBUG_EVENT, EventList);
		ListPtr = ListPtr->Flink;
		DebugEvent->Status = STATUS_DEBUGGER_INACTIVE;
		codevDbgkpWakeTarget (DebugEvent);
	}
}
NTSTATUS __stdcall codevDbgkInitialize()
{
	NTSTATUS st = STATUS_UNSUCCESSFUL;
	UNICODE_STRING NameString;
	OBJECT_TYPE_INITIALIZER oti = {0};
	GENERIC_MAPPING GenericMapping = {STANDARD_RIGHTS_READ | DEBUG_READ_EVENT,
		STANDARD_RIGHTS_WRITE | DEBUG_PROCESS_ASSIGN,
		STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
		DEBUG_ALL_ACCESS};
	ExInitializeFastMutex (&codevDbgkpProcessDebugPortMutex);
	RtlInitUnicodeString(&NameString,L"codevDebugObject");
	oti.Length                    = sizeof (oti);
	oti.SecurityRequired          = TRUE;
	oti.InvalidAttributes         = 0;
	oti.PoolType                  = NonPagedPool;
	oti.DeleteProcedure           = codevDbgkpDeleteObject;
	oti.CloseProcedure            = codevDbgkpCloseObject;
	oti.ValidAccessMask           = DEBUG_ALL_ACCESS;
	oti.GenericMapping            = GenericMapping;
	oti.DefaultPagedPoolCharge    = 0;
	oti.DefaultNonPagedPoolCharge = 0;
	st = codevObCreateObjectType(&NameString,&oti,NULL,&codevDbgkDebugObjectType);
	if (!NT_SUCCESS(st))
	{
		CodeVprint(("codevDbgkInitialize failed\r\n"));
	}
	return st;
}

NTSTATUS __stdcall codevNtCreateDebugObject(
	OUT PHANDLE DebugObjectHandle,
	IN ACCESS_MASK DesiredAccess,
	IN POBJECT_ATTRIBUTES ObjectAttributes,
	IN ULONG Flags
	)
{
	NTSTATUS status;
	KPROCESSOR_MODE PreviousMode;
	PDEBUG_OBJECT DebugObject;
	HANDLE Handle;
	PAGED_CODE();
	PreviousMode = KeGetPreviousMode();
	__try
	{
		if (PreviousMode != KernelMode)
		{
			//ProbeForWriteHandle
			ProbeForWrite(DebugObjectHandle,sizeof(HANDLE),sizeof(ULONG));
		}
		DebugObjectHandle = NULL;
	}
	__except(ExSystemExceptionFilter()){
			return GetExceptionCode();
	}
	if (Flags & ~DEBUG_KILL_ON_CLOSE)
	{
		return STATUS_INVALID_PARAMETER;
	}
	status = codevObCreateObject(PreviousMode,
		codevDbgkDebugObjectType,
		ObjectAttributes,
		PreviousMode,
		NULL,
		sizeof(DEBUG_OBJECT),
		0,
		0,
		&DebugObject);
	if (!NT_SUCCESS(status))
	{
		return status;
	}
	ExInitializeFastMutex(&DebugObject->Mutex);
	InitializeListHead (&DebugObject->EventList);
	KeInitializeEvent (&DebugObject->EventsPresent, NotificationEvent, FALSE);

	if (Flags & DEBUG_KILL_ON_CLOSE) {
		DebugObject->Flags = DEBUG_OBJECT_KILL_ON_CLOSE;
	} else {
		DebugObject->Flags = 0;
	}

	status = codevObInsertObject(DebugObject,NULL,DesiredAccess,0,NULL,&Handle);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	__try {
		*DebugObjectHandle = Handle;
	} __except (ExSystemExceptionFilter ()) {
		status = GetExceptionCode ();
	}

	return status;
}
//////////////////////////////////////////////////////////////////////////
NTSTATUS __stdcall codevNtDebugActiveProcess (
	IN HANDLE ProcessHandle,
	IN HANDLE DebugObjectHandle
	)
{
	//Ҫ��¼codevDebuggedProcess
	 NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode;
    PDEBUG_OBJECT DebugObject;
    PEPROCESS Process;
    PETHREAD LastThread;

    PAGED_CODE ();

    PreviousMode = KeGetPreviousMode();

    Status = codevObReferenceObjectByHandle (ProcessHandle,
                                        PROCESS_SET_PORT,
                                        codevPsProcessType,
                                        PreviousMode,
                                        &Process,
                                        NULL);
    if (!NT_SUCCESS (Status)) {
        return Status;
    }

    //
    // Don't let us debug ourselves or the system process.
    //
    if (Process == PsGetCurrentProcess () || Process == PsInitialSystemProcess) {
        ObDereferenceObject (Process);
        return STATUS_ACCESS_DENIED;
    }
    Status = codevObReferenceObjectByHandle (DebugObjectHandle,
                                        DEBUG_PROCESS_ASSIGN,
                                        codevDbgkDebugObjectType,
                                        PreviousMode,
                                        &DebugObject,
                                        NULL);

    if (NT_SUCCESS (Status)) 
	{
        //
        // We will be touching process address space. Block process rundown.
        //
        if (ExAcquireRundownProtection (&Process->RundownProtect)) {

            //
            // Post the fake process create messages etc.
            //
            Status = codevDbgkpPostFakeProcessCreateMessages (Process,
                                                         DebugObject,
                                                         &LastThread);

            //
            // Set the debug port. If this fails it will remove any faked messages.
            //
			//�����ǹؼ���!!!
            Status = codevDbgkpSetProcessDebugObject (Process,
                                                 DebugObject,
                                                 Status,
                                                 LastThread);

            ExReleaseRundownProtection (&Process->RundownProtect);
			//�㶨֮���¼�±����Խ����� ���Զ���
			//�������¼������DbgkpSetProcessDebugObject���¼��
			//codevDebuggedProcess = Process;
			//codevDebugObject = DebugObject;
        }
		else 
		{
            Status = STATUS_PROCESS_IS_TERMINATING;
        }

        ObDereferenceObject (DebugObject);
    }
    ObDereferenceObject (Process);

    return Status;
}

