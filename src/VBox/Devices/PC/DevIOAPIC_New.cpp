/* $Id$ */
/** @file
 * IO APIC - Input/Output Advanced Programmable Interrupt Controller.
 */

/*
 * Copyright (C) 2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_IOAPIC
#include <VBox/log.h>
#include <VBox/msi.h>
#include <VBox/vmm/pdmdev.h>

#include "VBoxDD.h"
#include <iprt/x86.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The current IO APIC saved state version. */
#define IOAPIC_SAVED_STATE_VERSION              2
/** The saved state version used by VirtualBox 5.0 and
 *  earlier.  */
#define IOAPIC_SAVED_STATE_VERSION_VBOX_50      1

/** Implementation specified by the "Intel I/O Controller Hub 9
 *  (ICH9) Family" */
#define IOAPIC_HARDWARE_VERSION_ICH9            1
/** Implementation specified by the "82093AA I/O Advanced Programmable Interrupt
Controller" */
#define IOAPIC_HARDWARE_VERSION_82093AA         2
/** The IO APIC implementation to use. */
#define IOAPIC_HARDWARE_VERSION                 IOAPIC_HARDWARE_VERSION_ICH9

#if IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_82093AA
/** The version. */
# define IOAPIC_VERSION                         0x11
/** The ID mask. */
# define IOAPIC_ID_MASK                         0x0f
#elif IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_ICH9
/** The version. */
# define IOAPIC_VERSION                         0x20
/** The ID mask. */
# define IOAPIC_ID_MASK                         0xff
#else
# error "Implement me"
#endif

/** The default MMIO base physical address. */
#define IOAPIC_MMIO_BASE_PHYSADDR               UINT64_C(0xfec00000)
/** The size of the MMIO range. */
#define IOAPIC_MMIO_SIZE                        X86_PAGE_4K_SIZE
/** The mask for getting direct registers from physical address. */
#define IOAPIC_MMIO_REG_MASK                    0xff

/** The number of interrupt input pins. */
#define IOAPIC_NUM_INTR_PINS                    24
/** Maximum redirection entires. */
#define IOAPIC_MAX_REDIR_ENTRIES                (IOAPIC_NUM_INTR_PINS - 1)

/** Version register - Gets the version. */
#define IOAPIC_VER_GET_VER(a_Reg)               ((a_Reg) & 0xff)
/** Version register - Gets the maximum redirection entry. */
#define IOAPIC_VER_GET_MRE(a_Reg)               (((a_Reg) >> 16) & 0xff)
/** Version register - Gets whether Pin Assertion Register (PRQ) is
 *  supported. */
#define IOAPIC_VER_HAS_PRQ(a_Reg)               RT_BOOL((a_Reg) & RT_BIT_32(15))

/** Arbitration register - Gets the ID. */
#define IOAPIC_ARB_GET_ID(a_Reg)                ((a_Reg) >> 24 & 0xf)

/** ID register - Gets the ID. */
#define IOAPIC_ID_GET_ID(a_Reg)                 ((a_Reg) >> 24 & IOAPIC_ID_MASK)

/** Redirection table entry - Vector. */
#define IOAPIC_RTE_VECTOR                       UINT64_C(0xff)
/** Redirection table entry - Delivery mode. */
#define IOAPIC_RTE_DELIVERY_MODE                (RT_BIT_64(8) | RT_BIT_64(9) | RT_BIT_64(10))
/** Redirection table entry - Destination mode. */
#define IOAPIC_RTE_DEST_MODE                    RT_BIT_64(11)
/** Redirection table entry - Delivery status. */
#define IOAPIC_RTE_DELIVERY_STATUS              RT_BIT_64(12)
/** Redirection table entry - Interrupt input pin polarity. */
#define IOAPIC_RTE_POLARITY                     RT_BIT_64(13)
/** Redirection table entry - Remote IRR. */
#define IOAPIC_RTE_REMOTE_IRR                   RT_BIT_64(14)
/** Redirection table entry - Trigger Mode. */
#define IOAPIC_RTE_TRIGGER_MODE                 RT_BIT_64(15)
/** Redirection table entry - the mask bit number. */
#define IOAPIC_RTE_MASK_BIT                     16
/** Redirection table entry - the mask. */
#define IOAPIC_RTE_MASK                         RT_BIT_64(IOAPIC_RTE_MASK_BIT)
/** Redirection table entry - Extended Destination ID. */
#define IOAPIC_RTE_EXT_DEST_ID                  UINT64_C(0x00ff000000000000)
/** Redirection table entry - Destination. */
#define IOAPIC_RTE_DEST                         UINT64_C(0xff00000000000000)

/** Redirection table entry - Gets the destination. */
#define IOAPIC_RTE_GET_DEST(a_Reg)              ((a_Reg) >> 56 & 0xff)
/** Redirection table entry - Gets the mask flag. */
#define IOAPIC_RTE_GET_MASK(a_Reg)              (((a_Reg) >> IOAPIC_RTE_MASK_BIT) & 0x1)
/** Redirection table entry - Checks whether it's masked. */
#define IOAPIC_RTE_IS_MASKED(a_Reg)             ((a_Reg) & IOAPIC_RTE_MASK)
/** Redirection table entry - Gets the trigger mode. */
#define IOAPIC_RTE_GET_TRIGGER_MODE(a_Reg)      (((a_Reg) >> 15) & 0x1)
/** Redirection table entry - Gets the remote IRR flag. */
#define IOAPIC_RTE_GET_REMOTE_IRR(a_Reg)        (((a_Reg) >> 14) & 0x1)
/** Redirection table entry - Gets the interrupt pin polarity. */
#define IOAPIC_RTE_GET_POLARITY(a_Reg)          (((a_Reg) >> 13) & 0x1)
/** Redirection table entry - Gets the delivery status. */
#define IOAPIC_RTE_GET_DELIVERY_STATUS(a_Reg)   (((a_Reg) >> 12) & 0x1)
/** Redirection table entry - Gets the destination mode. */
#define IOAPIC_RTE_GET_DEST_MODE(a_Reg)         (((a_Reg) >> 11) & 0x1)
/** Redirection table entry - Gets the delivery mode. */
#define IOAPIC_RTE_GET_DELIVERY_MODE(a_Reg)     (((a_Reg) >> 8)  & 0x7)
/** Redirection table entry - Gets the vector. */
#define IOAPIC_RTE_GET_VECTOR(a_Reg)            ((a_Reg) & IOAPIC_RTE_VECTOR)
/** Redirection table entry - Valid write mask. */
#define IOAPIC_RTE_VALID_WRITE_MASK             (  IOAPIC_RTE_DEST     | IOAPIC_RTE_MASK      | IOAPIC_RTE_TRIGGER_MODE \
                                                 | IOAPIC_RTE_POLARITY | IOAPIC_RTE_DEST_MODE | IOAPIC_RTE_DELIVERY_MODE \
                                                 | IOAPIC_RTE_VECTOR)

#if IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_82093AA
/** Redirection table entry - Valid read mask. */
# define IOAPIC_RTE_VALID_READ_MASK             (  IOAPIC_RTE_DEST       | IOAPIC_RTE_MASK          | IOAPIC_RTE_TRIGGER_MODE \
                                                 | IOAPIC_RTE_REMOTE_IRR | IOAPIC_RTE_POLARITY      | IOAPIC_RTE_DELIVERY_STATUS \
                                                 | IOAPIC_RTE_DEST_MODE  | IOAPIC_RTE_DELIVERY_MODE | IOAPIC_RTE_VECTOR)
#elif IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_ICH9
/** Redirection table entry - Valid read mask. */
# define IOAPIC_RTE_VALID_READ_MASK             (  IOAPIC_RTE_DEST            | IOAPIC_RTE_EXT_DEST_ID | IOAPIC_RTE_MASK \
                                                 | IOAPIC_RTE_TRIGGER_MODE    | IOAPIC_RTE_REMOTE_IRR  | IOAPIC_RTE_POLARITY \
                                                 | IOAPIC_RTE_DELIVERY_STATUS | IOAPIC_RTE_DEST_MODE   | IOAPIC_RTE_DELIVERY_MODE \
                                                 | IOAPIC_RTE_VECTOR)
#endif
/** Redirection table entry - Trigger mode edge. */
#define IOAPIC_RTE_TRIGGER_MODE_EDGE            0
/** Redirection table entry - Trigger mode level. */
#define IOAPIC_RTE_TRIGGER_MODE_LEVEL           1

/** Index of indirect registers in the I/O APIC register table. */
#define IOAPIC_INDIRECT_INDEX_ID                0x0
#define IOAPIC_INDIRECT_INDEX_VERSION           0x1
#if IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_82093AA
# define IOAPIC_INDIRECT_INDEX_ARB              0x2
#endif
#define IOAPIC_INDIRECT_INDEX_REDIR_TBL_START   0x10
#define IOAPIC_INDIRECT_INDEX_REDIR_TBL_END     0x3F

/** Offset of direct registers in the I/O APIC MMIO space. */
#define IOAPIC_DIRECT_OFF_INDEX                 0x00
#define IOAPIC_DIRECT_OFF_DATA                  0x10
#if IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_ICH9
# define IOAPIC_DIRECT_OFF_EOI                  0x40
#endif

/** @def IOAPIC_LOCK
 * Acquires the PDM lock. */
#define IOAPIC_LOCK(pThis, rcBusy) \
    do { \
        int rcLock = (pThis)->CTX_SUFF(pIoApicHlp)->pfnLock((pThis)->CTX_SUFF(pDevIns), rcBusy); \
        if (rcLock != VINF_SUCCESS) \
            return rcLock; \
    } while (0)

/** @def IOAPIC_LOCK_VOID
 * Acquires the PDM lock assumes success. */
#define IOAPIC_LOCK_VOID(pThis) \
    do { \
        int rcLock = (pThis)->CTX_SUFF(pIoApicHlp)->pfnLock((pThis)->CTX_SUFF(pDevIns), VERR_INTERNAL_ERROR_2); \
        Assert(rcLock == VINF_SUCCESS); \
    } while (0)

/** @def IOAPIC_UNLOCK
 * Releases the PDM lock. */
#define IOAPIC_UNLOCK(pThis)    (pThis)->CTX_SUFF(pIoApicHlp)->pfnUnlock((pThis)->CTX_SUFF(pDevIns))


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct IOAPIC
{
    /** The device instance - R3 Ptr. */
    PPDMDEVINSR3            pDevInsR3;
    /** The IOAPIC helpers - R3 Ptr. */
    PCPDMIOAPICHLPR3        pIoApicHlpR3;

    /** The device instance - R0 Ptr. */
    PPDMDEVINSR0            pDevInsR0;
    /** The IOAPIC helpers - R0 Ptr. */
    PCPDMIOAPICHLPR0        pIoApicHlpR0;

    /** The device instance - RC Ptr. */
    PPDMDEVINSRC            pDevInsRC;
    /** The IOAPIC helpers - RC Ptr. */
    PCPDMIOAPICHLPRC        pIoApicHlpRC;

    /** The ID register. */
    uint8_t                 u8Id;
    /** The index register. */
    uint8_t                 u8Index;
    /** Number of CPUs. */
    uint8_t                 cCpus;
    /* Alignment padding. */
    uint8_t                 u8Padding0[5];
#if IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_ICH9
    /** The EOI register. */
    uint32_t                u32Eoi;
    uint32_t                u32Padding1;
#else
    uint64_t                u64Padding0;
#endif

    /** The redirection table registers. */
    uint64_t                au64RedirTable[IOAPIC_NUM_INTR_PINS];
    /** The IRQ tags and source IDs for each pin (tracing purposes). */
    uint32_t                au32TagSrc[IOAPIC_NUM_INTR_PINS];

    /** Alignment padding. */
    uint32_t                u32Padding2;
    /** The internal IRR reflecting interrupt lines. */
    uint32_t                uIrr;

#ifdef VBOX_WITH_STATISTICS
    /** Number of MMIO reads in R0. */
    STAMCOUNTER             StatMmioReadR0;
    /** Number of MMIO reads in R3. */
    STAMCOUNTER             StatMmioReadR3;
    /** Number of MMIO reads in RC. */
    STAMCOUNTER             StatMmioReadRC;

    /** Number of MMIO writes in R0. */
    STAMCOUNTER             StatMmioWriteR0;
    /** Number of MMIO writes in R3. */
    STAMCOUNTER             StatMmioWriteR3;
    /** Number of MMIO writes in RC. */
    STAMCOUNTER             StatMmioWriteRC;

    /** Number of SetIrq calls in R0. */
    STAMCOUNTER             StatSetIrqR0;
    /** Number of SetIrq calls in R3. */
    STAMCOUNTER             StatSetIrqR3;
    /** Number of SetIrq calls in RC. */
    STAMCOUNTER             StatSetIrqRC;

    /** Number of SetEoi calls in R0. */
    STAMCOUNTER             StatSetEoiR0;
    /** Number of SetEoi calls in R3. */
    STAMCOUNTER             StatSetEoiR3;
    /** Number of SetEoi calls in RC. */
    STAMCOUNTER             StatSetEoiRC;
#endif
} IOAPIC;
/** Pointer to IOAPIC data. */
typedef IOAPIC *PIOAPIC;
/** Pointer to a const IOAPIC data. */
typedef IOAPIC const *PCIOAPIC;
AssertCompileMemberAlignment(IOAPIC, au64RedirTable, 8);

#ifndef VBOX_DEVICE_STRUCT_TESTCASE

#if IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_82093AA
/**
 * Gets the arbitration register.
 *
 * @returns The arbitration.
 */
DECLINLINE(uint32_t) ioapicGetArb(void)
{
    Log2(("IOAPIC: ioapicGetArb: returns 0\n"));
    return 0;
}
#endif


/**
 * Gets the version register.
 *
 * @returns The version.
 */
DECLINLINE(uint32_t) ioapicGetVersion(void)
{
    uint32_t uValue = RT_MAKE_U32(IOAPIC_VERSION, IOAPIC_MAX_REDIR_ENTRIES);
    Log2(("IOAPIC: ioapicGetVersion: returns %#RX32\n", uValue));
    return uValue;
}


/**
 * Sets the ID register.
 *
 * @param   pThis       Pointer to the IOAPIC instance.
 * @param   uValue      The value to set.
 */
DECLINLINE(void) ioapicSetId(PIOAPIC pThis, uint32_t uValue)
{
    Log2(("IOAPIC: ioapicSetId: uValue=%#RX32\n", uValue));
    pThis->u8Id = (uValue >> 24) & IOAPIC_ID_MASK;
}


/**
 * Gets the ID register.
 *
 * @returns The ID.
 * @param   pThis       Pointer to the IOAPIC instance.
 */
DECLINLINE(uint32_t) ioapicGetId(PCIOAPIC pThis)
{
    uint32_t uValue = (uint32_t)(pThis->u8Id & IOAPIC_ID_MASK) << 24;
    Log2(("IOAPIC: ioapicGetId: returns %#RX32\n", uValue));
    return uValue;
}


/**
 * Sets the index register.
 *
 * @param pThis     Pointer to the IOAPIC instance.
 * @param uValue    The value to set.
 */
DECLINLINE(void) ioapicSetIndex(PIOAPIC pThis, uint32_t uValue)
{
    LogFlow(("IOAPIC: ioapicSetIndex: uValue=%#RX32\n", uValue));
    pThis->u8Index = uValue & 0xff;
}


/**
 * Gets the index register.
 *
 * @returns The index value.
 */
DECLINLINE(uint32_t) ioapicGetIndex(PCIOAPIC pThis)
{
    uint32_t const uValue = pThis->u8Index;
    LogFlow(("IOAPIC: ioapicGetIndex: returns %#x\n", uValue));
    return uValue;
}


/**
 * Signals the next pending interrupt for the specified Redirection Table Entry
 * (RTE).
 *
 * @param   pThis       The IOAPIC instance.
 * @param   idxRte      The index of the RTE.
 */
static void ioapicSignalIrqForRte(PIOAPIC pThis, uint8_t idxRte)
{
    /* Check if there's an interrupt on the corresponding interrupt input pin. */
    uint32_t const uPinMask = UINT32_C(1) << idxRte;
    if (pThis->uIrr & uPinMask)
    {
        /* Ensure the RTE isn't masked. */
        uint64_t const u64Rte = pThis->au64RedirTable[idxRte];
        if (!IOAPIC_RTE_IS_MASKED(u64Rte))
        {
            uint32_t const u32TagSrc      = pThis->au32TagSrc[idxRte];
            uint8_t const  u8Vector       = IOAPIC_RTE_GET_VECTOR(u64Rte);
            uint8_t const  u8DeliveryMode = IOAPIC_RTE_GET_DELIVERY_MODE(u64Rte);
            uint8_t const  u8DestMode     = IOAPIC_RTE_GET_DEST_MODE(u64Rte);
            uint8_t const  u8Polarity     = IOAPIC_RTE_GET_POLARITY(u64Rte);
            uint8_t const  u8TriggerMode  = IOAPIC_RTE_GET_TRIGGER_MODE(u64Rte);
            uint8_t const  u8Dest         = IOAPIC_RTE_GET_DEST(u64Rte);

            /* We cannot accept another level-triggered interrupt until remote IRR has been cleared. */
            if (u8TriggerMode == IOAPIC_RTE_TRIGGER_MODE_LEVEL)
            {
                uint8_t const u8RemoteIrr = IOAPIC_RTE_GET_REMOTE_IRR(u64Rte);
                if (u8RemoteIrr)
                    return;
            }

            /*
             * Deliver to the local APIC via the system/3-wire-APIC bus.
             */
            int rc = pThis->CTX_SUFF(pIoApicHlp)->pfnApicBusDeliver(pThis->CTX_SUFF(pDevIns),
                                                                    u8Dest,
                                                                    u8DestMode,
                                                                    u8DeliveryMode,
                                                                    u8Vector,
                                                                    u8Polarity,
                                                                    u8TriggerMode,
                                                                    u32TagSrc);
            /* Can't reschedule to R3. */
            Assert(rc == VINF_SUCCESS);

            /*
             * For edge triggered interrupts, we can clear our IRR bit to receive further
             * edge-triggered interrupts, as the local APIC has accepted the interrupt.
             */
            if (u8TriggerMode == IOAPIC_RTE_TRIGGER_MODE_EDGE)
            {
                pThis->uIrr &= ~uPinMask;
                pThis->au32TagSrc[idxRte] = 0;
            }
            else
            {
                /*
                 * For level triggered interrupts, we set the remote IRR bit to indicate
                 * the local APIC has accepted the interrupt.
                 */
                Assert(u8TriggerMode == IOAPIC_RTE_TRIGGER_MODE_LEVEL);
                pThis->au64RedirTable[idxRte] |= IOAPIC_RTE_REMOTE_IRR;
            }
        }
    }
}


/**
 * Gets the redirection table entry.
 *
 * @returns The redirection table entry.
 * @param   pThis       Pointer to the IOAPIC instance.
 * @param   uIndex      The index value.
 */
DECLINLINE(uint32_t) ioapicGetRedirTableEntry(PCIOAPIC pThis, uint32_t uIndex)
{
    uint8_t const idxRte = (uIndex - IOAPIC_INDIRECT_INDEX_REDIR_TBL_START) >> 1;
    uint32_t uValue;
    if (!(uIndex & 1))
        uValue = RT_LO_U32(pThis->au64RedirTable[idxRte]) & RT_LO_U32(IOAPIC_RTE_VALID_READ_MASK);
    else
        uValue = RT_HI_U32(pThis->au64RedirTable[idxRte]) & RT_HI_U32(IOAPIC_RTE_VALID_READ_MASK);

    LogFlow(("IOAPIC: ioapicGetRedirTableEntry: uIndex=%#RX32 idxRte=%u returns %#RX32\n", uIndex, idxRte, uValue));
    return uValue;
}


/**
 * Sets the redirection table entry.
 *
 * @param   pThis       Pointer to the IOAPIC instance.
 * @param   uIndex      The index value.
 * @param   uValue      The value to set.
 */
static void ioapicSetRedirTableEntry(PIOAPIC pThis, uint32_t uIndex, uint32_t uValue)
{
    uint8_t const idxRte = (uIndex - IOAPIC_INDIRECT_INDEX_REDIR_TBL_START) >> 1;
    AssertMsg(idxRte < RT_ELEMENTS(pThis->au64RedirTable), ("Invalid index %u, expected <= %u\n", idxRte,
                                                            RT_ELEMENTS(pThis->au64RedirTable)));

    /*
     * Write the low or high 32-bit value into the specified 64-bit RTE register.
     * Update only the valid, writable bits.
     */
    uint64_t const u64Rte = pThis->au64RedirTable[idxRte];
    if (!(uIndex & 1))
    {
        uint32_t const u32RteNewLo = uValue & RT_LO_U32(IOAPIC_RTE_VALID_WRITE_MASK);
        uint64_t const u64RteHi    = u64Rte & UINT64_C(0xffffffff00000000);
        pThis->au64RedirTable[idxRte] = u64RteHi | u32RteNewLo;
    }
    else
    {
        uint32_t const u32RteLo    = RT_LO_U32(u64Rte);
        uint64_t const u64RteNewHi = (uint64_t)(uValue & RT_HI_U32(IOAPIC_RTE_VALID_WRITE_MASK)) << 32;
        pThis->au64RedirTable[idxRte] = u64RteNewHi | u32RteLo;
    }

    ioapicSignalIrqForRte(pThis, idxRte);

    LogFlow(("IOAPIC: ioapicSetRedirTableEntry: uIndex=%#RX32 idxRte=%u uValue=%#RX32\n", uIndex, idxRte, uValue));
}


/**
 * Gets the data register.
 *
 * @returns The data value.
 * @param pThis     Pointer to the IOAPIC instance.
 */
static uint32_t ioapicGetData(PCIOAPIC pThis)
{
    uint8_t const uIndex = pThis->u8Index;
    uint32_t uValue;
    if (   uIndex >= IOAPIC_INDIRECT_INDEX_REDIR_TBL_START
        && uIndex <= IOAPIC_INDIRECT_INDEX_REDIR_TBL_END)
        uValue = ioapicGetRedirTableEntry(pThis, uIndex);
    else
    {
        switch (uIndex)
        {
            case IOAPIC_INDIRECT_INDEX_ID:
                uValue = ioapicGetId(pThis);
                break;

            case IOAPIC_INDIRECT_INDEX_VERSION:
                uValue = ioapicGetVersion();
                break;

#if IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_82093AA
            case IOAPIC_INDIRECT_INDEX_ARB:
                uValue = ioapicGetArb();
                break;
#endif

            default:
                Log2(("IOAPIC: Attempt to read register at invalid index %#x\n", uIndex));
                uValue = UINT32_C(0xffffffff);
                break;
        }
    }

    return uValue;
}


/**
 * Sets the data register.
 *
 * @param pThis     Pointer to the IOAPIC instance.
 * @param uValue    The value to set.
 */
static void ioapicSetData(PIOAPIC pThis, uint32_t uValue)
{
    uint8_t const uIndex = pThis->u8Index;
    LogFlow(("IOAPIC: ioapicSetData: uIndex=%#x uValue=%#RX32\n", uIndex, uValue));

    if (uIndex == IOAPIC_INDIRECT_INDEX_VERSION)
        ioapicSetId(pThis, uValue);
    else if (   uIndex >= IOAPIC_INDIRECT_INDEX_REDIR_TBL_START
             && uIndex <= IOAPIC_INDIRECT_INDEX_REDIR_TBL_END)
        ioapicSetRedirTableEntry(pThis, uIndex, uValue);
    else
        Log2(("IOAPIC: ioapicSetData: Invalid index %#RX32, ignoring write request with uValue=%#RX32\n", uIndex, uValue));
}


/**
 * @interface_method_impl{PDMIOAPICREG,pfnSetEoiR3}
 * @remarks The device critsect is entered by the caller(s).
 */
PDMBOTHCBDECL(void) ioapicSetEoi(PPDMDEVINS pDevIns, uint8_t u8Vector)
{
    PIOAPIC pThis = PDMINS_2_DATA(pDevIns, PIOAPIC);
    STAM_COUNTER_INC(&pThis->CTX_SUFF(StatSetEoi));
    LogFlow(("IOAPIC: ioapicSetEoi: u8Vector=%#x (%u)\n", u8Vector, u8Vector));

    for (uint8_t idxRte = 0; idxRte < RT_ELEMENTS(pThis->au64RedirTable); idxRte++)
    {
        uint64_t const u64Rte = pThis->au64RedirTable[idxRte];
        if (IOAPIC_RTE_GET_VECTOR(u64Rte) == u8Vector)
        {
            pThis->au64RedirTable[idxRte] &= ~IOAPIC_RTE_REMOTE_IRR;
            Log2(("IOAPIC: ioapicSetEoi: Cleared remote IRR for RTE %u\n", idxRte));
            ioapicSignalIrqForRte(pThis, idxRte);
        }
    }
}


/**
 * @interface_method_impl{PDMIOAPICREG,pfnSetIrqR3}
 * @remarks The device critsect is entered by the caller(s).
 */
PDMBOTHCBDECL(void) ioapicSetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel, uint32_t uTagSrc)
{
    PIOAPIC pThis = PDMINS_2_DATA(pDevIns, PIOAPIC);
    LogFlow(("IOAPIC: ioapicSetIrq: iIrq=%d iLevel=%d uTagSrc=%#x\n", iIrq, iLevel, uTagSrc));

    STAM_COUNTER_INC(&pThis->CTX_SUFF(StatSetIrq));

    if (iIrq >= 0 && iIrq < (int)RT_ELEMENTS(pThis->au64RedirTable))
    {
        uint8_t  const idxRte = iIrq;
        uint64_t const u64Rte = pThis->au64RedirTable[idxRte];
        uint8_t  const u8TriggerMode = IOAPIC_RTE_GET_TRIGGER_MODE(u64Rte);
        uint32_t const uPinMask = UINT32_C(1) << idxRte;

        if (u8TriggerMode == IOAPIC_RTE_TRIGGER_MODE_EDGE)
        {
            /** @todo Consider polarity for edge-triggered interrupts? There
             *        seems to be a conflict between "The Unabridged Pentium"
             *        book and the I/O APIC specs. */
            if (iLevel)
            {
                pThis->uIrr |= uPinMask;
                if (!pThis->au32TagSrc[idxRte])
                    pThis->au32TagSrc[idxRte] = uTagSrc;
                else
                    pThis->au32TagSrc[idxRte] = RT_BIT_32(31);

                ioapicSignalIrqForRte(pThis, idxRte);
            }
            else
                pThis->uIrr &= ~uPinMask;
        }
        else
        {
            Assert(u8TriggerMode == IOAPIC_RTE_TRIGGER_MODE_LEVEL);

            bool fActive = RT_BOOL(iLevel & 1);
            /** @todo Polarity is busted elsewhere, we need to fix that
             *        first. See @bugref{8386#c7}. */
#if 0
            uint8_t const u8Polarity = IOAPIC_RTE_GET_POLARITY(u64Rte);
            fActive ^= u8Polarity; */
#endif

            if (fActive)
            {
                pThis->uIrr |= uPinMask;
                if (!pThis->au32TagSrc[idxRte])
                    pThis->au32TagSrc[idxRte] = uTagSrc;
                else
                    pThis->au32TagSrc[idxRte] = RT_BIT_32(31);

                ioapicSignalIrqForRte(pThis, idxRte);

                if ((iLevel & PDM_IRQ_LEVEL_FLIP_FLOP) == PDM_IRQ_LEVEL_FLIP_FLOP)
                {
                    pThis->uIrr &= ~uPinMask;
                    pThis->au32TagSrc[idxRte] = 0;
                }
            }
            else
                pThis->uIrr &= ~uPinMask;
        }
    }
}


/**
 * @interface_method_impl{PDMIOAPICREG,pfnSendMsiR3}
 * @remarks The device critsect is entered by the caller(s).
 */
PDMBOTHCBDECL(void) ioapicSendMsi(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t uValue, uint32_t uTagSrc)
{
    PCIOAPIC pThis = PDMINS_2_DATA(pDevIns, PCIOAPIC);
    LogFlow(("IOAPIC: ioapicSendMsi: GCPhys=%#RGp uValue=%#RX32\n", GCPhys, uValue));

    /*
     * Parse the message from the physical address.
     * See Intel spec. 10.11.1 "Message Address Register Format".
     */
    uint8_t const u8DestAddr = (GCPhys & VBOX_MSI_ADDR_DEST_ID_MASK) >> VBOX_MSI_ADDR_DEST_ID_SHIFT;
    uint8_t const u8DestMode = (GCPhys >> VBOX_MSI_ADDR_DEST_MODE_SHIFT) & 0x1;
    /** @todo Check if we need to implement Redirection Hint Indicator. */
    /* uint8_t const uRedirectHint  = (GCPhys >> VBOX_MSI_ADDR_REDIRECTION_SHIFT) & 0x1; */

    /*
     * Parse the message data.
     * See Intel spec. 10.11.2 "Message Data Register Format".
     */
    uint8_t const u8Vector       = (uValue & VBOX_MSI_DATA_VECTOR_MASK)  >> VBOX_MSI_DATA_VECTOR_SHIFT;
    uint8_t const u8TriggerMode  = (uValue >> VBOX_MSI_DATA_TRIGGER_SHIFT) & 0x1;
    uint8_t const u8DeliveryMode = (uValue >> VBOX_MSI_DATA_DELIVERY_MODE_SHIFT) & 0x7;

    /*
     * Deliver to the local APIC via the system/3-wire-APIC bus.
     */
    int rc = pThis->CTX_SUFF(pIoApicHlp)->pfnApicBusDeliver(pDevIns,
                                                            u8DestAddr,
                                                            u8DestMode,
                                                            u8DeliveryMode,
                                                            u8Vector,
                                                            0 /* u8Polarity - N/A */,
                                                            u8TriggerMode,
                                                            uTagSrc);
    /* Can't reschedule to R3. */
    Assert(rc == VINF_SUCCESS);
}


/**
 * @callback_method_impl{FNIOMMMIOREAD}
 */
PDMBOTHCBDECL(int) ioapicMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void *pv, unsigned cb)
{
    PIOAPIC pThis = PDMINS_2_DATA(pDevIns, PIOAPIC);
    STAM_COUNTER_INC(&pThis->CTX_SUFF(StatMmioRead));

    IOAPIC_LOCK(pThis, VINF_IOM_R3_MMIO_READ);

    int       rc      = VINF_SUCCESS;
    uint32_t *puValue = (uint32_t *)pv;
    uint32_t  offReg  = GCPhysAddr & IOAPIC_MMIO_REG_MASK;
    switch (offReg)
    {
        case IOAPIC_DIRECT_OFF_INDEX:
            *puValue = ioapicGetIndex(pThis);
            break;

        case IOAPIC_DIRECT_OFF_DATA:
            *puValue = ioapicGetData(pThis);
            break;

        default:
            Log2(("IOAPIC: ioapicMmioRead: Invalid offset. GCPhysAddr=%#RGp offReg=%#x\n", GCPhysAddr, offReg));
            rc = VINF_IOM_MMIO_UNUSED_FF;
            break;
    }

    IOAPIC_UNLOCK(pThis);

    LogFlow(("IOAPIC: ioapicMmioRead: offReg=%#x, returns %#RX32\n", offReg, *puValue));
    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIOWRITE}
 */
PDMBOTHCBDECL(int) ioapicMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void const *pv, unsigned cb)
{
    PIOAPIC pThis = PDMINS_2_DATA(pDevIns, PIOAPIC);

    STAM_COUNTER_INC(&pThis->CTX_SUFF(StatMmioWrite));

    IOAPIC_LOCK(pThis, VINF_IOM_R3_MMIO_WRITE);

    Assert(!(GCPhysAddr & 3));
    Assert(cb == 4);

    uint32_t const uValue = *(uint32_t const *)pv;
    uint32_t const offReg = GCPhysAddr & IOAPIC_MMIO_REG_MASK;

    LogFlow(("IOAPIC: ioapicMmioWrite: pThis=%p GCPhysAddr=%#RGp cb=%u uValue=%#RX32\n", pThis, GCPhysAddr, cb, uValue));

    switch (offReg)
    {
        case IOAPIC_DIRECT_OFF_INDEX:
            ioapicSetIndex(pThis, uValue);
            break;

        case IOAPIC_DIRECT_OFF_DATA:
            ioapicSetData(pThis, uValue);
            break;

#if IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_ICH9
        case IOAPIC_DIRECT_OFF_EOI:
            ioapicSetEoi(pDevIns, uValue);
            break;
#endif

        default:
            Log2(("IOAPIC: ioapicMmioWrite: Invalid offset. GCPhysAddr=%#RGp offReg=%#x\n", GCPhysAddr, offReg));
            break;
    }

    IOAPIC_UNLOCK(pThis);
    return VINF_SUCCESS;
}


#ifdef IN_RING3
/** @interface_method_impl{DBGFREGDESC,pfnGet} */
static DECLCALLBACK(int) ioapicDbgReg_GetIndex(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    pValue->u32 = ioapicGetIndex(PDMINS_2_DATA((PPDMDEVINS)pvUser, PCIOAPIC));
    return VINF_SUCCESS;
}

/** @interface_method_impl{DBGFREGDESC,pfnSet} */
static DECLCALLBACK(int) ioapicDbgReg_SetIndex(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    ioapicSetIndex(PDMINS_2_DATA((PPDMDEVINS)pvUser, PIOAPIC), pValue->u8);
    return VINF_SUCCESS;
}

/** @interface_method_impl{DBGFREGDESC,pfnGet} */
static DECLCALLBACK(int) ioapicDbgReg_GetData(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    pValue->u32 = ioapicGetData((PDMINS_2_DATA((PPDMDEVINS)pvUser, PCIOAPIC)));
    return VINF_SUCCESS;
}

/** @interface_method_impl{DBGFREGDESC,pfnSet} */
static DECLCALLBACK(int) ioapicDbgReg_SetData(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
     ioapicSetData(PDMINS_2_DATA((PPDMDEVINS)pvUser, PIOAPIC), pValue->u32);
     return VINF_SUCCESS;
}

/** @interface_method_impl{DBGFREGDESC,pfnGet} */
static DECLCALLBACK(int) ioapicDbgReg_GetVersion(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    pValue->u32 = ioapicGetVersion();
    return VINF_SUCCESS;
}

/** @interface_method_impl{DBGFREGDESC,pfnGet} */
static DECLCALLBACK(int) ioapicDbgReg_GetArb(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
#if IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_82093AA
    pValue->u32 = ioapicGetArb(PDMINS_2_DATA((PPDMDEVINS)pvUser, PCIOAPIC));
#else
    pValue->u32 = UINT32_C(0xffffffff);
#endif
    return VINF_SUCCESS;
}

/** @interface_method_impl{DBGFREGDESC,pfnGet} */
static DECLCALLBACK(int) ioapicDbgReg_GetRte(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PCIOAPIC pThis = PDMINS_2_DATA((PPDMDEVINS)pvUser, PCIOAPIC);
    pValue->u64 = ioapicGetRedirTableEntry(pThis, pDesc->offRegister);
    return VINF_SUCCESS;
}

/** @interface_method_impl{DBGFREGDESC,pfnSet} */
static DECLCALLBACK(int) ioapicDbgReg_SetRte(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue,
                                                         PCDBGFREGVAL pfMask)
{
    PIOAPIC pThis = PDMINS_2_DATA((PPDMDEVINS)pvUser, PIOAPIC);
    ioapicSetRedirTableEntry(pThis, pDesc->offRegister, pValue->u64);
    return VINF_SUCCESS;
}

/** IOREDTBLn sub fields. */
static DBGFREGSUBFIELD const g_aRteSubs[] =
{
    { "vector",       0,   8,  0,  0, NULL, NULL },
    { "dlvr_mode",    8,   3,  0,  0, NULL, NULL },
    { "dest_mode",    11,  1,  0,  0, NULL, NULL },
    { "dlvr_status",  12,  1,  0,  DBGFREGSUBFIELD_FLAGS_READ_ONLY, NULL, NULL },
    { "polarity",     13,  1,  0,  0, NULL, NULL },
    { "remote_irr",   14,  1,  0,  DBGFREGSUBFIELD_FLAGS_READ_ONLY, NULL, NULL },
    { "trigger_mode", 15,  1,  0,  0, NULL, NULL },
    { "mask",         16,  1,  0,  0, NULL, NULL },
#if IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_ICH9
    { "ext_dest_id",  48,  8,  0,  DBGFREGSUBFIELD_FLAGS_READ_ONLY, NULL, NULL },
#endif
    { "dest",         56,  8,  0,  0, NULL, NULL },
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Register descriptors for DBGF. */
static DBGFREGDESC const g_aRegDesc[] =
{
    { "index",      DBGFREG_END, DBGFREGVALTYPE_U8,  0,  0, ioapicDbgReg_GetIndex, ioapicDbgReg_SetIndex,    NULL, NULL },
    { "data",       DBGFREG_END, DBGFREGVALTYPE_U32, 0,  0, ioapicDbgReg_GetData,  ioapicDbgReg_SetData,     NULL, NULL },
    { "version",    DBGFREG_END, DBGFREGVALTYPE_U32, DBGFREG_FLAGS_READ_ONLY, 0, ioapicDbgReg_GetVersion, NULL, NULL, NULL },
#if IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_82093AA
    { "arb",        DBGFREG_END, DBGFREGVALTYPE_U32, DBGFREG_FLAGS_READ_ONLY, 0, ioapicDbgReg_GetArb,     NULL, NULL, NULL },
#endif
    { "rte0",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  0, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte1",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  1, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte2",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  2, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte3",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  3, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte4",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  4, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte5",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  5, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte6",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  6, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte7",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  7, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte8",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  8, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte9",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  9, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte10",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 10, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte11",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 11, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte12",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 12, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte13",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 13, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte14",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 14, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte15",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 15, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte16",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 16, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte17",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 17, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte18",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 18, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte19",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 19, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte20",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 20, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte21",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 21, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte22",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 22, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte23",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 23, ioapicDbgReg_GetRte, ioapicDbgReg_SetRte, NULL, &g_aRteSubs[0] },
    DBGFREGDESC_TERMINATOR()
};


/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) ioapicR3DbgInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PCIOAPIC pThis = PDMINS_2_DATA(pDevIns, PIOAPIC);
    LogFlow(("IOAPIC: ioapicR3DbgInfo: pThis=%p pszArgs=%s\n", pThis, pszArgs));

    pHlp->pfnPrintf(pHlp, "I/O APIC at %#010x:\n", IOAPIC_MMIO_BASE_PHYSADDR);

    uint32_t const uId = ioapicGetId(pThis);
    pHlp->pfnPrintf(pHlp, "  ID        = %#RX32\n", uId);
    pHlp->pfnPrintf(pHlp, "    ID                      = %#x\n",     IOAPIC_ID_GET_ID(uId));

    uint32_t const uVer = ioapicGetVersion();
    pHlp->pfnPrintf(pHlp, "  Version   = %#RX32\n",  uVer);
    pHlp->pfnPrintf(pHlp, "    Version                 = %#x\n",     IOAPIC_VER_GET_VER(uVer));
    pHlp->pfnPrintf(pHlp, "    Pin Assert Reg. Support = %RTbool\n", IOAPIC_VER_HAS_PRQ(uVer));
    pHlp->pfnPrintf(pHlp, "    Max. Redirection Entry  = %u\n",      IOAPIC_VER_GET_MRE(uVer));

#if IOAPIC_HARDWARE_VERSION == IOAPIC_HARDWARE_VERSION_82093AA
    uint32_t const uArb = ioapicGetArb();
    pHlp->pfnPrintf(pHlp, "  Arb       = %#RX32\n", uArb);
    pHlp->pfnPrintf(pHlp, "    Arb ID                  = %#x\n",     IOAPIC_ARB_GET_ID(uArb));
#endif

    pHlp->pfnPrintf(pHlp, "  Current index             = %#x\n",     ioapicGetIndex(pThis));

    pHlp->pfnPrintf(pHlp, "  I/O Redirection Table and IRR:\n");
    pHlp->pfnPrintf(pHlp, "  idx dst_mode dst_addr mask irr trigger rirr polar dlvr_st dlvr_mode vector\n");

    for (uint8_t idxRte = 0; idxRte < RT_ELEMENTS(pThis->au64RedirTable); idxRte++)
    {
        static const char * const s_apszDeliveryModes[] =
        {
            "Fixed ",
            "LowPri",
            "SMI   ",
            "Rsvd  ",
            "NMI   ",
            "INIT  ",
            "Rsvd  ",
            "ExtINT"
        };

        const uint64_t uEntry = pThis->au64RedirTable[idxRte];
        const char    *pszDestMode       = IOAPIC_RTE_GET_DEST_MODE(uEntry) == 0 ? "phys" : "log ";
        const uint8_t  uDest             = IOAPIC_RTE_GET_DEST(uEntry);
        const uint8_t  uMask             = IOAPIC_RTE_GET_MASK(uEntry);
        const char    *pszTriggerMode    = IOAPIC_RTE_GET_TRIGGER_MODE(uEntry) == 0 ? "edge " : "level";
        const uint8_t  uRemoteIrr        = IOAPIC_RTE_GET_REMOTE_IRR(uEntry);
        const char    *pszPolarity       = IOAPIC_RTE_GET_POLARITY(uEntry) == 0 ? "acthi" : "actlo";
        const char    *pszDeliveryStatus = IOAPIC_RTE_GET_DELIVERY_STATUS(uEntry) == 0 ? "idle" : "pend";
        const uint8_t  uDeliveryMode     = IOAPIC_RTE_GET_DELIVERY_MODE(uEntry);
                                           Assert(uDeliveryMode < RT_ELEMENTS(s_apszDeliveryModes));
        const char    *pszDeliveryMode   = s_apszDeliveryModes[uDeliveryMode];
        const uint8_t  uVector           = IOAPIC_RTE_GET_VECTOR(uEntry);

        pHlp->pfnPrintf(pHlp, "   %02d   %s      %02x     %u    %u   %s   %u   %s  %s     %s   %3u (%016llx)\n",
                        idxRte,
                        pszDestMode,
                        uDest,
                        uMask,
                        (pThis->uIrr >> idxRte) & 1,
                        pszTriggerMode,
                        uRemoteIrr,
                        pszPolarity,
                        pszDeliveryStatus,
                        pszDeliveryMode,
                        uVector,
                        pThis->au64RedirTable[idxRte]);
    }
}


/**
 * @copydoc FNSSMDEVSAVEEXEC
 */
static DECLCALLBACK(int) ioapicR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PCIOAPIC pThis = PDMINS_2_DATA(pDevIns, PCIOAPIC);
    LogFlow(("IOAPIC: ioapicR3SaveExec\n"));

    SSMR3PutU32(pSSM, pThis->uIrr);
    SSMR3PutU8(pSSM,  pThis->u8Id);
    SSMR3PutU8(pSSM,  pThis->u8Index);
    for (uint8_t idxRte = 0; idxRte < RT_ELEMENTS(pThis->au64RedirTable); idxRte++)
        SSMR3PutU64(pSSM, pThis->au64RedirTable[idxRte]);

    return VINF_SUCCESS;
}


/**
 * @copydoc FNSSMDEVLOADEXEC
 */
static DECLCALLBACK(int) ioapicR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PIOAPIC pThis = PDMINS_2_DATA(pDevIns, PIOAPIC);
    LogFlow(("APIC: apicR3LoadExec: uVersion=%u uPass=%#x\n", uVersion, uPass));

    Assert(uPass == SSM_PASS_FINAL);
    NOREF(uPass);

    /* Weed out invalid versions. */
    if (   uVersion != IOAPIC_SAVED_STATE_VERSION
        && uVersion != IOAPIC_SAVED_STATE_VERSION_VBOX_50)
    {
        LogRel(("IOAPIC: ioapicR3LoadExec: Invalid/unrecognized saved-state version %u (%#x)\n", uVersion, uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    if (uVersion == IOAPIC_SAVED_STATE_VERSION)
        SSMR3GetU32(pSSM, &pThis->uIrr);

    SSMR3GetU8(pSSM, &pThis->u8Id);
    SSMR3GetU8(pSSM, &pThis->u8Index);
    for (uint8_t idxRte = 0; idxRte < RT_ELEMENTS(pThis->au64RedirTable); idxRte++)
        SSMR3GetU64(pSSM, &pThis->au64RedirTable[idxRte]);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) ioapicR3Reset(PPDMDEVINS pDevIns)
{
    PIOAPIC pThis = PDMINS_2_DATA(pDevIns, PIOAPIC);
    LogFlow(("IOAPIC: ioapicR3Reset: pThis=%p\n", pThis));

    /* We lock here to prevent concurrent writes from ioapicSetIrq() from device threads. */
    IOAPIC_LOCK_VOID(pThis);

    pThis->uIrr    = 0;
    pThis->u8Index = 0;
    pThis->u8Id    = 0;

    for (uint8_t idxRte = 0; idxRte < RT_ELEMENTS(pThis->au64RedirTable); idxRte++)
    {
        pThis->au64RedirTable[idxRte] = IOAPIC_RTE_MASK;
        pThis->au32TagSrc[idxRte]     = 0;
    }

    IOAPIC_UNLOCK(pThis);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnRelocate}
 */
static DECLCALLBACK(void) ioapicR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    PIOAPIC pThis = PDMINS_2_DATA(pDevIns, PIOAPIC);
    LogFlow(("IOAPIC: ioapicR3Relocate: pThis=%p offDelta=%RGi\n", pThis, offDelta));

    pThis->pDevInsRC    = PDMDEVINS_2_RCPTR(pDevIns);
    pThis->pIoApicHlpRC = pThis->pIoApicHlpR3->pfnGetRCHelpers(pDevIns);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) ioapicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PIOAPIC pThis = PDMINS_2_DATA(pDevIns, PIOAPIC);
    LogFlow(("IOAPIC: ioapicR3Construct: pThis=%p iInstance=%d\n", pThis, iInstance));
    Assert(iInstance == 0);

    /*
     * Initialize the state data.
     */
    pThis->pDevInsR3 = pDevIns;
    pThis->pDevInsR0 = PDMDEVINS_2_R0PTR(pDevIns);
    pThis->pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "NumCPUs|RZEnabled", "");

    uint32_t cCpus;
    int rc = CFGMR3QueryU32Def(pCfg, "NumCPUs", &cCpus, 1);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query integer value \"NumCPUs\""));
    if (cCpus > UINT8_MAX - 2) /* ID 255 is broadcast and the IO-APIC needs one (ID=cCpus). */
    {
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Max %u CPUs, %u specified"), UINT8_MAX - 1, cCpus);
    }
    pThis->cCpus = (uint8_t)cCpus;

    bool fRZEnabled;
    rc = CFGMR3QueryBoolDef(pCfg, "RZEnabled", &fRZEnabled, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to query boolean value \"RZEnabled\""));

    Log2(("IOAPIC: cCpus=%u fRZEnabled=%RTbool\n", cCpus, fRZEnabled));

    /*
     * We shall do locking for this device via IOAPIC helpers.
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Register the IOAPIC.
     */
    PDMIOAPICREG IoApicReg;
    RT_ZERO(IoApicReg);
    IoApicReg.u32Version   = PDM_IOAPICREG_VERSION;
    IoApicReg.pfnSetIrqR3  = ioapicSetIrq;
    IoApicReg.pfnSendMsiR3 = ioapicSendMsi;
    IoApicReg.pfnSetEoiR3  = ioapicSetEoi;
    if (fRZEnabled)
    {
        IoApicReg.pszSetIrqRC  = "ioapicSetIrq";
        IoApicReg.pszSetIrqR0  = "ioapicSetIrq";

        IoApicReg.pszSendMsiRC = "ioapicSendMsi";
        IoApicReg.pszSendMsiR0 = "ioapicSendMsi";

        IoApicReg.pszSetEoiRC = "ioapicSetEoi";
        IoApicReg.pszSetEoiR0 = "ioapicSetEoi";
    }
    rc = PDMDevHlpIOAPICRegister(pDevIns, &IoApicReg, &pThis->pIoApicHlpR3);
    if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("IOAPIC: PDMDevHlpIOAPICRegister failed! rc=%Rrc\n", rc));
        return rc;
    }

    /*
     * Register MMIO callbacks.
     */
    rc = PDMDevHlpMMIORegister(pDevIns, IOAPIC_MMIO_BASE_PHYSADDR, IOAPIC_MMIO_SIZE, pThis,
                               IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED, ioapicMmioWrite, ioapicMmioRead,
                               "I/O APIC");
    if (RT_SUCCESS(rc))
    {
        if (fRZEnabled)
        {
            pThis->pIoApicHlpRC = pThis->pIoApicHlpR3->pfnGetRCHelpers(pDevIns);
            rc = PDMDevHlpMMIORegisterRC(pDevIns, IOAPIC_MMIO_BASE_PHYSADDR, IOAPIC_MMIO_SIZE, NIL_RTRCPTR /* pvUser */,
                                         "ioapicMmioWrite", "ioapicMmioRead");
            AssertRCReturn(rc, rc);

            pThis->pIoApicHlpR0 = pThis->pIoApicHlpR3->pfnGetR0Helpers(pDevIns);
            rc = PDMDevHlpMMIORegisterR0(pDevIns, IOAPIC_MMIO_BASE_PHYSADDR, IOAPIC_MMIO_SIZE, NIL_RTR0PTR /* pvUser */,
                                         "ioapicMmioWrite", "ioapicMmioRead");
            AssertRCReturn(rc, rc);
        }
    }
    else
    {
        LogRel(("IOAPIC: PDMDevHlpMMIORegister failed! rc=%Rrc\n", rc));
        return rc;
    }

    /*
     * Register saved-state callbacks.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, IOAPIC_SAVED_STATE_VERSION, sizeof(*pThis), ioapicR3SaveExec, ioapicR3LoadExec);
    if (RT_FAILURE(rc))
    {
        LogRel(("IOAPIC: PDMDevHlpSSMRegister failed! rc=%Rrc\n", rc));
        return rc;
    }

    /*
     * Register debugger info callback.
     */
    rc = PDMDevHlpDBGFInfoRegister(pDevIns, "ioapic", "Display IO APIC state.", ioapicR3DbgInfo);
    AssertRCReturn(rc, rc);

    /*
     * Register debugger register access.
     */
    rc = PDMDevHlpDBGFRegRegister(pDevIns, g_aRegDesc); AssertRC(rc);
    AssertRCReturn(rc, rc);

#ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioReadR0,  STAMTYPE_COUNTER, "/Devices/IOAPIC/R0/MmioReadG0",  STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO reads in R0.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioWriteR0, STAMTYPE_COUNTER, "/Devices/IOAPIC/R0/MmioWriteR0", STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO writes in R0.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetIrqR0,    STAMTYPE_COUNTER, "/Devices/IOAPIC/R0/SetIrqR0",    STAMUNIT_OCCURENCES, "Number of IOAPIC SetIrq calls in R0.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetEoiR0,    STAMTYPE_COUNTER, "/Devices/IOAPIC/R0/SetEoiR0",    STAMUNIT_OCCURENCES, "Number of IOAPIC SetEoi calls in R0.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioReadR3,  STAMTYPE_COUNTER, "/Devices/IOAPIC/R3/MmioReadR3",  STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO reads in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioWriteR3, STAMTYPE_COUNTER, "/Devices/IOAPIC/R3/MmioWriteR3", STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO writes in R3.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetIrqR3,    STAMTYPE_COUNTER, "/Devices/IOAPIC/R3/SetIrqR3",    STAMUNIT_OCCURENCES, "Number of IOAPIC SetIrq calls in R3.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetEoiR3,    STAMTYPE_COUNTER, "/Devices/IOAPIC/R3/SetEoiR3",    STAMUNIT_OCCURENCES, "Number of IOAPIC SetEoi calls in R3.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioReadRC,  STAMTYPE_COUNTER, "/Devices/IOAPIC/RC/MmioReadRC",  STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO reads in RC.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioWriteRC, STAMTYPE_COUNTER, "/Devices/IOAPIC/RC/MmioWriteRC", STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO writes in RC.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetIrqRC,    STAMTYPE_COUNTER, "/Devices/IOAPIC/RC/SetIrqRC",    STAMUNIT_OCCURENCES, "Number of IOAPIC SetIrq calls in RC.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetEoiRC,    STAMTYPE_COUNTER, "/Devices/IOAPIC/RC/SetEoiRC",    STAMUNIT_OCCURENCES, "Number of IOAPIC SetEoi calls in RC.");
#endif

    /*
     * Init. the device state.
     */
    LogRel(("IOAPIC: Using implementation 2.0!\n"));
    ioapicR3Reset(pDevIns);

    return VINF_SUCCESS;
}


/**
 * IO APIC device registration structure.
 */
const PDMDEVREG g_DeviceIOAPIC =
{
    /* u32Version */
    PDM_DEVREG_VERSION,
    /* szName */
    "ioapic",
    /* szRCMod */
    "VBoxDDRC.rc",
    /* szR0Mod */
    "VBoxDDR0.r0",
    /* pszDescription */
    "I/O Advanced Programmable Interrupt Controller (IO-APIC) Device",
    /* fFlags */
      PDM_DEVREG_FLAGS_HOST_BITS_DEFAULT | PDM_DEVREG_FLAGS_GUEST_BITS_32_64 | PDM_DEVREG_FLAGS_PAE36
    | PDM_DEVREG_FLAGS_RC | PDM_DEVREG_FLAGS_R0,
    /* fClass */
    PDM_DEVREG_CLASS_PIC,
    /* cMaxInstances */
    1,
    /* cbInstance */
    sizeof(IOAPIC),
    /* pfnConstruct */
    ioapicR3Construct,
    /* pfnDestruct */
    NULL,
    /* pfnRelocate */
    ioapicR3Relocate,
    /* pfnMemSetup */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    ioapicR3Reset,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnQueryInterface. */
    NULL,
    /* pfnInitComplete */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32VersionEnd */
    PDM_DEVREG_VERSION
};

#endif /* IN_RING3 */

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */
