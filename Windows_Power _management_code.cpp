/* Windows Power management Kernel code Sample.
This code is an improved version of the existing code :)
I dare to write it in English as a common language, but since the author is Japanese, please forgive me if English is wrong.
*/


#include <Wdm.h>
#include "Driver.h"API
#include "Power.h"
#include "SupportFunc.h"

NTSTATUS
DispatchPower(
    PDEVICE_OBJECT fdo,
    PIRP           pIrp
    )
{
    NTSTATUS           status;
    PIO_STACK_LOCATION stack;


    LockDevice(
        fdo->DeviceExtension
        );

    KdPrint((DBG_NAME "Received POWER Message (IRP=0x%08x) ==> ", pIrp));

    stack = IoGetCurrentIrpStackLocation(
                pIrp
                );

    switch (stack->MinorFunction)
    {
        case IRP_MN_WAIT_WAKE:
            KdPrint(("IRP_MN_WAIT_WAKE\n"));
            status = DefaultPowerHandler(
                         fdo,
                         pIrp
                         );
            break;

        case IRP_MN_POWER_SEQUENCE:
            KdPrint(("IRP_MN_POWER_SEQUENCE\n"));
            status = DefaultPowerHandler(
                         fdo,
                         pIrp
                         );
            break;

        case IRP_MN_SET_POWER:
            KdPrint(("IRP_MN_SET_POWER\n"));
            if (stack->Parameters.Power.Type == SystemPowerState)
            {
                KdPrint((DBG_NAME "SystemContext = 0x%08x  SystemPowerState = 0x%08x\n",
                         stack->Parameters.Power.SystemContext,
                         stack->Parameters.Power.State.SystemState));
            }
            else
            {
                KdPrint((DBG_NAME "SystemContext = 0x%08x  DevicePowerState = 0x%08x\n",
                         stack->Parameters.Power.SystemContext,
                         stack->Parameters.Power.State.DeviceState));
            }
            status = HandleSetPower(
                         fdo,
                         pIrp
                         );
            break;

        case IRP_MN_QUERY_POWER:
            KdPrint(("IRP_MN_QUERY_POWER\n"));
            KdPrint((DBG_NAME "SystemContext = 0x%08x",
                     stack->Parameters.Power.SystemContext));
            status = HandleQueryPower(
                         fdo,
                         pIrp
                         );
            break;

        default:
            KdPrint(("Unsupported IRP_MN_Xxx (0x%08x)\n", stack->MinorFunction));
            status = DefaultPowerHandler(
                         fdo,
                         pIrp
                         );
            break;
    }

    UnlockDevice(
        fdo->DeviceExtension
        );

    return status;
}

NTSTATUS
DefaultPowerHandler(
    PDEVICE_OBJECT fdo,
    PIRP           pIrp
    )
{
    PoStartNextPowerIrp(
        pIrp
        );

    IoSkipCurrentIrpStackLocation(
        pIrp
        );

    return PoCallDriver(
               ((DEVICE_EXTENSION *)fdo->DeviceExtension)->pLowerDeviceObject,
               pIrp
               );
}

NTSTATUS
HandleSetPower(
    PDEVICE_OBJECT fdo,
    PIRP           pIrp
    )
{
    U32                 context;
    NTSTATUS            status;
    POWER_STATE         state;
    POWER_STATE_TYPE    type;
    DEVICE_EXTENSION   *pdx;
    PIO_STACK_LOCATION  stack;
    DEVICE_POWER_STATE  devstate;


    pdx     = fdo->DeviceExtension;
    stack   = IoGetCurrentIrpStackLocation(pIrp);
    context = stack->Parameters.Power.SystemContext;
    type    = stack->Parameters.Power.Type;
    state   = stack->Parameters.Power.State;

    if (type == SystemPowerState)
    {
        if (state.SystemState <= PowerSystemWorking)
            devstate = PowerDeviceD0;
        else
            devstate = PowerDeviceD3;
    }
    else
        devstate = state.DeviceState;

    if (devstate < pdx->Power)
    {
        // Adding more power
        IoCopyCurrentIrpStackLocationToNext(
            pIrp
            );

        IoSetCompletionRoutine(
            pIrp,
            (PIO_COMPLETION_ROUTINE) OnFinishPowerUp,
            NULL,
            TRUE,
            TRUE,
            TRUE
            );

        return PoCallDriver(
                   pdx->pLowerDeviceObject,
                   pIrp
                   );
    }
    else if (devstate > pdx->Power)
    {
        if (type == SystemPowerState)
        {
            status = SendDeviceSetPower(
                         fdo,
                         devstate,
                         context
                         );
            if (!NT_SUCCESS(status))
            {
                PoStartNextPowerIrp(
                    pIrp
                    );

                return S6010_CompleteIrpWithInformation(
                           pIrp,
                           status,
                           0
                           );
            }
        }
        else
        {
            PoSetPowerState(
                fdo,
                type,
                state
                ); 

            SetPowerState(
                fdo,
                devstate
                );
        }

        // Pass request down
        return DefaultPowerHandler(
                   fdo,
                   pIrp
                   );
    }

    // Pass request down
    return DefaultPowerHandler(
               fdo,
               pIrp
               );
}

NTSTATUS
HandleQueryPower(
    PDEVICE_OBJECT fdo,
    PIRP           pIrp
    )
{
    U32                 context;
    POWER_STATE         state;
    POWER_STATE_TYPE    type;
    DEVICE_EXTENSION   *pdx;
    PIO_STACK_LOCATION  stack;
    DEVICE_POWER_STATE  devstate;

    pdx     = fdo->DeviceExtension;
    stack   = IoGetCurrentIrpStackLocation(pIrp);
    context = stack->Parameters.Power.SystemContext;
    type    = stack->Parameters.Power.Type;
    state   = stack->Parameters.Power.State;

    if (type == SystemPowerState)
    {
        if (state.SystemState <= PowerSystemWorking)
            devstate = PowerDeviceD0;
        else
            devstate = PowerDeviceD3;
    }
    else
        devstate = state.DeviceState;

    if (devstate - PowerDeviceD0 + D0 != D0)
    {
        KdPrint((DBG_NAME "WARNING - HandleQueryPower() unsupported power level\n"));
        return S6010_CompleteIrpWithInformation(
                   pIrp,
                   STATUS_NOT_IMPLEMENTED,
                   0
                   );
    }
    return DefaultPowerHandler(
               fdo,
               pIrp
               );
}

NTSTATUS
OnFinishPowerUp(
    PDEVICE_OBJECT fdo,
    PIRP           pIrp,
    PVOID          junk
    )
{
    NTSTATUS           status;
    POWER_STATE        state;
    POWER_STATE_TYPE   type;
    PIO_STACK_LOCATION stack;
    DEVICE_POWER_STATE devstate;


    KdPrint((DBG_NAME "in OnFinishPowerUp\n"));

    if (pIrp->PendingReturned)
        IoMarkIrpPending(pIrp);

    status = pIrp->IoStatus.Status;
    if (!NT_SUCCESS(status))
    {
        KdPrint((DBG_NAME "OnFinishPowerUp: IRP failed\n"));
        return status;
    }

    stack = IoGetCurrentIrpStackLocation(pIrp);
    type  = stack->Parameters.Power.Type;
    state = stack->Parameters.Power.State;

    if (type == SystemPowerState)
    {
        // Restoring power to the system
        if (state.SystemState <= PowerSystemWorking)
            devstate = PowerDeviceD0;
        else
            devstate = PowerDeviceD3;

        status = SendDeviceSetPower(
                     fdo,
                     devstate,
                     stack->Parameters.Power.SystemContext
                     );
    }
    else
    {
        SetPowerState(
            fdo,
            state.DeviceState
            );
        PoSetPowerState(
            fdo,
            type,
            state
            ); 
    }

    PoStartNextPowerIrp(pIrp);

    return status;
}

VOID
OnPowerRequestComplete(
    PDEVICE_OBJECT   DeviceObject,
    U8               MinorFunction,
    POWER_STATE      PowerState,
    PVOID            context,
    PIO_STATUS_BLOCK ioStatus
    )
{
    KdPrint((DBG_NAME "in OnPowerRequestComplete\n"));

    // Set event
    if ((PKEVENT)context != NULL)
    {
        KeSetEvent(
            (PKEVENT)context,
            1,
            FALSE
            );
    }
}

NTSTATUS
SendDeviceSetPower(
    PDEVICE_OBJECT     fdo,
    DEVICE_POWER_STATE state,
    U32                context
    )
{
    PIRP               pIrp;
    KEVENT             event;
    NTSTATUS           status;
    PIO_STACK_LOCATION stack;


    KdPrint((DBG_NAME "in SendDeviceSetPower\n"));

    // Skip request if we are already at the desired Power level
    if (state == ((DEVICE_EXTENSION *)fdo->DeviceExtension)->Power)
        return STATUS_SUCCESS;

    pIrp = IoAllocateIrp(
               fdo->StackSize,
               FALSE
               );
    if (pIrp == NULL)
    {
        KdPrint((DBG_NAME "ERROR - Unable to allocate IRP for Power request\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    stack                                     = IoGetNextIrpStackLocation(pIrp);
    stack->MajorFunction                      = IRP_MJ_POWER;
    stack->MinorFunction                      = IRP_MN_SET_POWER;
    stack->Parameters.Power.SystemContext     = context;
    stack->Parameters.Power.Type              = DevicePowerState;
    stack->Parameters.Power.State.DeviceState = state;

    KeInitializeEvent(
        &event,
        NotificationEvent,
        FALSE
        );

    IoSetCompletionRoutine(
        pIrp,
        (PIO_COMPLETION_ROUTINE) OnRequestComplete,
        (PVOID) &event,
        TRUE,
        TRUE,
        TRUE
        );

    status = PoCallDriver(
                 fdo,
                 pIrp
                 );
    if (status == STATUS_PENDING)
    {
        // Wait for completion
        KeWaitForSingleObject(
            &event,
            Executive,
            KernelMode,
            FALSE,
            NULL
            );
        status = pIrp->IoStatus.Status;
    }

    IoFreeIrp(pIrp);

    return status;
}

VOID
SetPowerState(
    PDEVICE_OBJECT     fdo,
    DEVICE_POWER_STATE state
    )
{
    POWER_INFO  pPowerInfo;


    pPowerInfo.pdx   = fdo->DeviceExtension;
    pPowerInfo.state = state;

    EmpowerDevice(
        &pPowerInfo
        );
}

BOOLEAN
EmpowerDevice(
    POWER_INFO *pPowerInfo
    )
{
    if (pPowerInfo->state == pPowerInfo->pdx->Power)
        return TRUE;

    KdPrint((DBG_NAME "Putting device into state ==> "));

    switch (pPowerInfo->state)
    {
        case D0Uninitialized:
            KdPrint(("D0Uninitialized\n"));
            break;

        case D0:
            KdPrint(("D0\n"));
            break;

        case D1:
            KdPrint(("D1\n"));
            break;

        case D2:
            KdPrint(("D2\n"));
            break;

        case D3Hot:
            KdPrint(("D3Hot\n"));
            break;

        case D3Cold:
            KdPrint(("D3Cold\n"));
            break;
    }
    pPowerInfo->pdx->Power = pPowerInfo->state;
    return TRUE;
}
#if 0

NTSTATUS
SendSelfSetPowerRequest(
    PDEVICE_OBJECT     fdo,
    DEVICE_POWER_STATE state
    )
{
    KEVENT            event;
    NTSTATUS          status;
    NTSTATUS          waitStatus;
    POWER_STATE       poState;
    DEVICE_EXTENSION *pdx;

    KdPrint((DBG_NAME "in SendSelfSetPowerRequest()\n"));
    pdx = fdo->DeviceExtension;

    if (state == pdx->Power)
        return STATUS_SUCCESS;

    KeInitializeEvent(
        &event,
        NotificationEvent,
        FALSE
    );
    poState.DeviceState = state;

    status = PoRequestPowerIrp(
                 pdx->PhysicalDeviceObject,
                 IRP_MN_SET_POWER,
                 poState,
                 OnPowerRequestComplete,
                 &event,
                 NULL
    );

    if (status == STATUS_PENDING)
    {
        // status pending is the return code we wanted
        waitStatus = KeWaitForSingleObject(
                         &event,
                         Suspended,
                         KernelMode,
                         FALSE,
                         NULL
                         );

        status = STATUS_SUCCESS;
    }
    else
    {
        KdPrint((DBG_NAME "PoRequestPowerIrp failed\n"));
    }

    return status;
}
#else

NTSTATUS
SendSelfSetPowerRequest(
    PDEVICE_OBJECT     fdo,
    DEVICE_POWER_STATE state
    )
{
    POWER_STATE       power;
    DEVICE_EXTENSION *pdx;


    KdPrint((DBG_NAME "in SendSelfSetPowerRequest\n"));

    if (fdo->DeviceExtension == NULL)
    {
        KdPrint((DBG_NAME "ERROR - NULL extension\n"));
        return STATUS_NOT_SUPPORTED;
    }
    pdx = fdo->DeviceExtension;
    if (state == pdx->Power)
        return STATUS_SUCCESS;
    power.DeviceState = state;

    if (state > pdx->Power)
    {
        PoSetPowerState(
            fdo,
            DevicePowerState,
            power
            );

        SetPowerState(
            fdo,
            state
            );
    }

    if (state < pdx->Power)
    {
        SetPowerState(
            fdo,
            state
            );

        PoSetPowerState(
            fdo,
            DevicePowerState,
            power
            );
    }

    return STATUS_SUCCESS;
}
#endif