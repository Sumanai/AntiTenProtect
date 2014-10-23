#include "Ntos.h"

NTSTATUS ReloadNtos(PDRIVER_OBJECT   DriverObject)
{
	//NTSTATUS status = STATUS_UNSUCCESSFUL;
	if (!GetNtosInformation(&SystemKernelFilePath,&SystemKernelModuleBase,&SystemKernelModuleSize))
	{
		if (SystemKernelFilePath)
		{
			ExFreePool(SystemKernelFilePath);
		}
		return STATUS_UNSUCCESSFUL;
	}
	if (!PeReload(SystemKernelFilePath,SystemKernelModuleBase,&ReloadNtosImageBase,g_MyDriverObject))
	{
		if (SystemKernelFilePath)
		{
			ExFreePool(SystemKernelFilePath);
		}
		if (ReloadNtosImageBase)
		{
			ExFreePool(ReloadNtosImageBase);
		}
		return STATUS_UNSUCCESSFUL;
	}
	//���������ں�·�������ͷŲ��Ƿ��أ�
	if (SystemKernelFilePath)
	{
		ExFreePool(SystemKernelFilePath);
	}
	//ntos�ض�λ֮��reloadģ���е�ssdt����Ļ���ԭʼ�� 

	return STATUS_SUCCESS;
}