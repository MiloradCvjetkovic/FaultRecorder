/*------------------------------------------------------------------------------
 * MDK - Component ::Fault Recorder
 * Copyright (c) 2022 ARM Germany GmbH. All rights reserved.
 *------------------------------------------------------------------------------
 * Name:    FaultRecorder.c
 * Purpose: Fault Recorder for fault debugging
 * Rev.:    V0.1.0
 *----------------------------------------------------------------------------*/

//lint -e46      "Suppress: field type should be _Bool, unsigned int or signed int [MISRA 2012 Rule 6.1, required]"
//lint -e451     "Suppress: repeatedly included but does not have a standard include guard [MISRA 2012 Directive 4.10, required]"
//lint -e537     "Suppress: Repeated include file 'stddef.h'"
//lint -e750     "Suppress: local macro not referenced [MISRA 2012 Rule 2.5, advisory]"
//lint -e751     "Suppress: local typedef not referenced [MISRA 2012 Rule 2.3, advisory]"
//lint -e754     "Suppress: local struct member not referenced"
//lint -e774     "Suppress: Boolean within 'right side of && within if' always evaluates to True [MISRA 2012 Rule 14.3, required]"
//lint -e835     "Suppress: A zero has been given as left argument to operator '+'"
//lint -e9026    "Suppress: function-like macro defined [MISRA 2012 Directive 4.9, advisory]"
//lint -e9058    "Suppress: tag unused outside of typedefs in standard library headers [MISRA 2012 Rule 2.4, advisory]"
//lint -elib(10) "Suppress: expecting ';'"

//lint -esym(9071, __FAULT_RECORDER_H) "Suppress: defined macro is reserved to the compiler"

#include "FaultRecorder.h"

#include "RTE_Components.h"
#include  CMSIS_device_header

#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Overridable macros
#ifndef FR_PRINT
//lint -esym(586, printf) "Suppress: function 'printf' is deprecated [MISRA 2012 Rule 21.6, required]"
#define FR_PRINT(...)                   printf(__VA_ARGS__)
#endif

// Compiler-specific defines
#if !defined(__NAKED)
  //lint -esym(9071, __NAKED) "Suppress: defined macro is reserved to the compiler"
  #define __NAKED __attribute__((naked))
#endif
#if !defined(__WEAK)
  //lint -esym(9071, __WEAK) "Suppress: defined macro is reserved to the compiler"
  #define __WEAK __attribute__((weak))
#endif
#if !defined(__NO_INIT)
  //lint -esym(9071, __NO_INIT) "Suppress: defined macro is reserved to the compiler"
  #if   defined (__CC_ARM)                                           /* ARM Compiler 4/5 */
    #define __NO_INIT __attribute__ ((section (".bss.noinit"), zero_init))
  #elif defined (__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)    /* ARM Compiler 6 */
    #define __NO_INIT __attribute__ ((section (".bss.noinit")))
  #elif defined (__GNUC__)                                           /* GNU Compiler */
    #define __NO_INIT __attribute__ ((section (".bss.noinit")))
  #else
    #warning "No compiler specific solution for __NO_INIT. __NO_INIT is ignored."
    #define __NO_INIT
  #endif
#endif

// Check if Arm Architecture is supported
#if  ((!defined(__ARM_ARCH_6M__)        || (__ARM_ARCH_6M__        == 0)) && \
      (!defined(__ARM_ARCH_7M__)        || (__ARM_ARCH_7M__        == 0)) && \
      (!defined(__ARM_ARCH_7EM__)       || (__ARM_ARCH_7EM__       == 0)) && \
      (!defined(__ARM_ARCH_8M_BASE__)   || (__ARM_ARCH_8M_BASE__   == 0)) && \
      (!defined(__ARM_ARCH_8M_MAIN__)   || (__ARM_ARCH_8M_MAIN__   == 0)) && \
      (!defined(__ARM_ARCH_8_1M_MAIN__) || (__ARM_ARCH_8_1M_MAIN__ == 0))    )
#error "Unknown or unsupported Arm Architecture!"
#endif

// Determine if fault registers are available
#if   ((defined(__ARM_ARCH_7M__)        && (__ARM_ARCH_7M__        != 0)) || \
       (defined(__ARM_ARCH_7EM__)       && (__ARM_ARCH_7EM__       != 0)) || \
       (defined(__ARM_ARCH_8M_MAIN__)   && (__ARM_ARCH_8M_MAIN__   != 0)) || \
       (defined(__ARM_ARCH_8_1M_MAIN__) && (__ARM_ARCH_8_1M_MAIN__ != 0))    )
#define FR_FAULT_REGS_EXIST    (1)
#else
#define FR_FAULT_REGS_EXIST    (0)
#endif

// Determine if architecture is Armv8/8.1-M architecture
#if   ((defined(__ARM_ARCH_8M_BASE__)   && (__ARM_ARCH_8M_BASE__   != 0)) || \
       (defined(__ARM_ARCH_8M_MAIN__)   && (__ARM_ARCH_8M_MAIN__   != 0)) || \
       (defined(__ARM_ARCH_8_1M_MAIN__) && (__ARM_ARCH_8_1M_MAIN__ != 0))    )
#define FR_ARCH_ARMV8x_M       (1)
#else
#define FR_ARCH_ARMV8x_M       (0)
#endif

// Determine if architecture is Armv8-M Baseline architecture
#if    (defined(__ARM_ARCH_8M_BASE__)   && (__ARM_ARCH_8M_BASE__   != 0))
#define FR_ARCH_ARMV8_M_BASE   (1)
#else
#define FR_ARCH_ARMV8_M_BASE   (0)
#endif

// Determine if architecture is Armv8-M Mainline or Armv8.1 architecture
#if   ((defined(__ARM_ARCH_8M_MAIN__)   && (__ARM_ARCH_8M_MAIN__   != 0)) || \
       (defined(__ARM_ARCH_8_1M_MAIN__) && (__ARM_ARCH_8_1M_MAIN__ != 0))    )
#define FR_ARCH_ARMV8x_M_MAIN  (1)
#else
#define FR_ARCH_ARMV8x_M_MAIN  (0)
#endif

// Determine if the code is compiled for Secure World
#if    (defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3))
#define FR_SECURE              (1)
#else
#define FR_SECURE              (0)
#endif

#if    (FR_FAULT_REGS_EXIST != 0)
// Define CFSR mask for detecting state context stacking failure
#ifndef SCB_CFSR_Stack_Err_Msk
#ifdef  SCB_CFSR_STKOF_Msk
#define SCB_CFSR_Stack_Err_Msk (SCB_CFSR_STKERR_Msk | SCB_CFSR_MSTKERR_Msk | SCB_CFSR_STKOF_Msk)
#else
#define SCB_CFSR_Stack_Err_Msk (SCB_CFSR_STKERR_Msk | SCB_CFSR_MSTKERR_Msk)
#endif
#endif
#endif

// Armv8/8.1-M Mainline architecture related defines
#if    (FR_ARCH_ARMV8x_M_MAIN != 0)
#ifndef SAU_SFSR_LSERR_Msk
#define SAU_SFSR_LSERR_Msk     (1UL << 7)               // SAU SFSR: LSERR Mask
#endif
#ifndef SAU_SFSR_SFARVALID_Msk
#define SAU_SFSR_SFARVALID_Msk (1UL << 6)               // SAU SFSR: SFARVALID Mask
#endif
#ifndef SAU_SFSR_LSPERR_Msk
#define SAU_SFSR_LSPERR_Msk    (1UL << 5)               // SAU SFSR: LSPERR Mask
#endif
#ifndef SAU_SFSR_INVTRAN_Msk
#define SAU_SFSR_INVTRAN_Msk   (1UL << 4)               // SAU SFSR: INVTRAN Mask
#endif
#ifndef SAU_SFSR_AUVIOL_Msk
#define SAU_SFSR_AUVIOL_Msk    (1UL << 3)               // SAU SFSR: AUVIOL Mask
#endif
#ifndef SAU_SFSR_INVER_Msk
#define SAU_SFSR_INVER_Msk     (1UL << 2)               // SAU SFSR: INVER Mask
#endif
#ifndef SAU_SFSR_INVIS_Msk
#define SAU_SFSR_INVIS_Msk     (1UL << 1)               // SAU SFSR: INVIS Mask
#endif
#ifndef SAU_SFSR_INVEP_Msk
#define SAU_SFSR_INVEP_Msk     (1UL)                    // SAU SFSR: INVEP Mask
#endif

#define FR_ASC_INTEGRITY_SIG   (0xFEFA125AU)            // Additional State Context Integrity Signature
#endif

// Fault Recorder definitions
#define FR_FAULT_INFO_VER_MAJOR (0U)                    // Fault Recorder FaultInfo type version.major
#define FR_FAULT_INFO_VER_MINOR (1U)                    // Fault Recorder FaultInfo type version.minor
#define FR_FAULT_INFO_TYPE     (FR_FAULT_INFO_VER_MINOR /* Fault Recorder FaultInfo type */ \
                             | (FR_FAULT_INFO_VER_MAJOR <<  8) \
                             | (FR_FAULT_REGS_EXIST     << 16) \
                             | (FR_ARCH_ARMV8x_M        << 17) \
                             | (FR_SECURE               << 18) )
#define FR_MAGIC_NUMBER        (0x52746C46U)            // Fault Recorder Magic number (ASCII "FltR")
#define FR_CRC32_INIT_VAL      (0xFFFFFFFFU)            // Fault Recorder CRC-32 initial value
#define FR_CRC32_DATA_PTR      (&FaultInfo.type)        // Fault Recorder CRC-32 data start
#define FR_CRC32_DATA_LEN      (sizeof(FaultInfo) -     /* Fault Recorder CRC-32 data length */ \
                               (sizeof(FaultInfo.magic_number) + sizeof(FaultInfo.crc32)))
#define FR_CRC32_POLYNOM       (0x04C11DB7U)            // Fault Recorder CRC-32 polynom

// Helper functions prototypes
static uint32_t CalcCRC32 (      uint32_t init_val,
                           const uint8_t *data_ptr,
                                 uint32_t data_len,
                                 uint32_t polynom);

// Fault information structure type definition
typedef struct {
  struct {
    uint8_t minor;                      // Fault information structure version: minor
    uint8_t major;                      // Fault information structure version: major
  } version;
  uint16_t fault_regs    :  1;          // == 1 - contains fault registers
  uint16_t armv8m        :  1;          // == 1 - contains Armv8/8.1-M related information
  uint16_t secure        :  1;          // == 1 - recording was done running in Secure World
  uint16_t reserved      : 13;          // Reserved (0)
} FaultInfoType_Type;

// State context (same as Basic Stack Frame) type definition
typedef struct {
  uint32_t R0;                          // R0  register value before exception
  uint32_t R1;                          // R1  register value before exception
  uint32_t R2;                          // R2  register value before exception
  uint32_t R3;                          // R3  register value before exception
  uint32_t R12;                         // R12 register value before exception
  uint32_t LR;                          // Link Register (R14) value before exception
  uint32_t ReturnAddress;               // Return address from exception
  uint32_t xPSR;                        // Program Status Register value before exception
} StateContext_Type;

// Additional state context type definition (only for Armv8/8.1-M arch)
typedef struct {
  uint32_t IntegritySignature;          // Integrity Signature
  uint32_t Reserved;                    // Reserved
  uint32_t R4;                          // R4  register value before exception
  uint32_t R5;                          // R5  register value before exception
  uint32_t R6;                          // R6  register value before exception
  uint32_t R7;                          // R7  register value before exception
  uint32_t R8;                          // R8  register value before exception
  uint32_t R9;                          // R9  register value before exception
  uint32_t R10;                         // R10 register value before exception
  uint32_t R11;                         // R11 register value before exception
} AdditionalStateContext_Type;

// Common Registers type definition
typedef struct {
  uint32_t xPSR;                        // Program Status Register value, in exception handler
  uint32_t EXC_RETURN;                  // Exception Return code (LR), in exception handler
  uint32_t MSP;                         // Main    Stack Pointer value
  uint32_t PSP;                         // Process Stack Pointer value
} CommonRegisters_Type;

// Additional Armv8/8.1-M arch specific Registers type definition
typedef struct {
  uint32_t MSPLIM;                      // Main    Stack Pointer Limit Register value
  uint32_t PSPLIM;                      // Process Stack Pointer Limit Register value
} Armv8mRegisters_Type;

// Fault Registers type definition
typedef struct {
  uint32_t SCB_CFSR;                    // System Control Block - Configurable Fault Status Register value
  uint32_t SCB_HFSR;                    // System Control Block - HardFault          Status Register value
  uint32_t SCB_DFSR;                    // System Control Block - Debug Fault        Status Register value
  uint32_t SCB_MMFAR;                   // System Control Block - MemManage Fault    Status Register value
  uint32_t SCB_BFAR;                    // System Control Block - BusFault           Status Register value
  uint32_t SCB_AFSR;                    // System Control Block - Auxiliary Fault    Status Register value
} FaultRegisters_Type;

// Additional Armv8/8.1-M Mainline arch specific Fault Registers type definition
typedef struct {
  uint32_t SCB_SFSR;                    // System Control Block - Secure Fault Status  Register value
  uint32_t SCB_SFAR;                    // System Control Block - Secure Fault Address Register value
} Armv8mFaultRegisters_Type;

// Fault information type definition
typedef struct {
  uint32_t                    magic_number;
  uint32_t                    crc32;
  FaultInfoType_Type          type;
  StateContext_Type           state_context;
  CommonRegisters_Type        common_registers;
#if (FR_FAULT_REGS_EXIST != 0)
  FaultRegisters_Type         fault_registers;
#endif
#if (FR_ARCH_ARMV8x_M != 0)
  AdditionalStateContext_Type additonal_state_context;
  Armv8mRegisters_Type        armv8_m_registers;
#endif
#if (FR_ARCH_ARMV8x_M_MAIN != 0)
  Armv8mFaultRegisters_Type   armv8_m_fault_registers;
#endif
} FaultInfo_Type;

// Fault information (FaultInfo)
static FaultInfo_Type         FaultInfo __NO_INIT;

// Fault Recorder callback functions -------------------------------------------

/**
  Callback function called after fault information was recorded.
  Used to provide user specific reaction to fault after it was recorded.
  The default implementation will reset the system via the CMSIS NVIC_SystemReset function.
*/
__WEAK __NO_RETURN void FaultRecordOnExit (void) {
  NVIC_SystemReset();                   // Reset the system
}

// Fault Recorder functions ----------------------------------------------------

/**
  Record fault information.
  Must be called from fault handler with preserved Link Register value, typically
  by branching to this function.
*/
__NAKED void FaultRecord (void) {
  //lint ++flb "Library Begin (excluded from MISRA check)"
  __ASM volatile (
#ifndef __ICCARM__
    ".syntax unified\n\t"
#endif

 /* --- Clear FaultInfo --- */
    "movs  r0,  #0\n"                   // R0 = 0
    "ldr   r1,  =%c[FaultInfo_addr]\n"  // R1 = &FaultInfo
    "movs  r2,  %[FaultInfo_size]\n"    // R2 = sizeof(FaultInfo)/4
    "b     is_clear_done\n"
  "clear_uint32:\n"
    "stm   r1!, {r0}\n"
    "subs  r2,  r2, #1\n"
  "is_clear_done:\n"
    "bne   clear_uint32\n"

    "mov   r12, r4\n"                   // Store R4 to R12 (to use R4 in this function)
    "movs  r4,  #0\n"                   // Clear R4

 /* Determine the beginning of the state context or the additional state context
    (for device with TruztZone) that was stacked upon exception entry and put that
    address into R3.
    For device with TrustZone, also determine if state context was pushed from
    Non-secure World but the exception handling is happening in the Secure World
    and if so, mark it by setting bit [0] of the R4 to value 1, thus indicating usage
    of Non-secure aliases.

    after this section:
      R3          == start of state context or additional state context if that was pushed also
      R4 bit [0]: == 0 - no access to Non-secure aliases or device without TrustZone
                  == 1 -    access to Non-secure aliases

    Determine by analyzing EXC_RETURN (Link Register):
    EXC_RETURN:
      - bit [6] (S):            only on device with TrustZone
                         == 0 - Non-secure stack was used
                         == 1 - Secure     stack was used
      - bit [5] (DCRS):         only on device with TrustZone
                         == 0 - additional state context was also stacked
                         == 1 - only       state context was stacked
      - bit [2] (SPSEL): == 0 - Main    Stack Pointer (MSP) was used for stacking on exception entry
                         == 1 - Process Stack Pointer (PSP) was used for stacking on exception entry */
    "mov   r0,  lr\n"                   // R0 = LR (EXC_RETURN)
    "lsrs  r0,  r0, #3\n"               // Shift bit [2] (SPSEL) into Carry flag
    "bcc   msp_used\n"                  // If    bit [2] (SPSEL) == 0, MSP or MSP_NS was used
                                        // If    bit [2] (SPSEL) == 1, PSP or PSP_NS was used
  "psp_used:\n"
#if (FR_SECURE != 0)                    // If code was compiled for and is running in Secure World
    "mov   r0,  lr\n"                   // R0 = LR (EXC_RETURN)
    "lsrs  r0,  r0, #7\n"               // Shift   bit [6] (S) into Carry flag
    "bcs   load_psp\n"                  // If      bit [6] (S) == 1, jump to load PSP
  "load_psp_ns:\n"                      // else if bit [6] (S) == 0, load PSP_NS
    "mrs   r3,  psp_ns\n"               // R3 = PSP_NS
    "movs  r4,  #1\n"                   // R4 = 1
    "b     r3_points_to_stack\n"        // PSP_NS loaded to R3, exit section
  "load_psp:\n"
#endif
    "mrs   r3,  psp\n"                  // R3 = PSP
    "b     r3_points_to_stack\n"        // PSP loaded to R3, exit section

  "msp_used:\n"
#if (FR_SECURE != 0)                    // If code was compiled for and is running in Secure World
    "mov   r0,  lr\n"                   // R0 = LR (EXC_RETURN)
    "lsrs  r0,  r0, #7\n"               // Shift   bit [6] (S) into Carry flag
    "bcs   load_msp\n"                  // If      bit [6] (S) == 1, jump to load MSP
  "load_msp_ns:\n"                      // else if bit [6] (S) == 0, load MSP_NS
    "mrs   r3,  msp_ns\n"               // R3 = MSP_NS
    "movs  r4,  #1\n"                   // R4 = 1
    "b     r3_points_to_stack\n"        // MSP_NS loaded to R3, exit section
  "load_msp:\n"
#endif
    "mrs   r3,  msp\n"                  // R3 = MSP
    "b     r3_points_to_stack\n"        // MSP loaded to R3, exit section

  "r3_points_to_stack:\n"

 /* Determine if stack contains valid state context (if fault was not a stacking fault).
    If stack information is not valid mark it by setting bit [1] of the R4 to value 1.
    Note: for Armv6-M and Armv8-M Baseline CFSR register is not available, so stack is 
          considered valid although it might not always be so. */
#if (FR_FAULT_REGS_EXIST != 0)          // If fault registers exist
    "ldr   r1,  =%c[cfsr_err_msk]\n"    // R1 = (SCB_CFSR_Stack_Err_Msk)
#if (FR_SECURE != 0)                    // If code was compiled for and is running in Secure World
    "lsrs  r0,  r4, #1\n"               // Shift   bit [0] of R4 into Carry flag
    "bcc   load_cfsr_addr\n"            // If      bit [0] of R4 == 0, jump to load CFSR register address
  "load_cfsr_ns_addr:\n"                // else if bit [0] of R4 == 1, load CFSR_NS register address
    "ldr   r2,  =%c[cfsr_ns_addr]\n"    // R2 = CFSR_NS address
    "b     load_cfsr\n"
  "load_cfsr_addr:\n"
#endif
    "ldr   r2,  =%c[cfsr_addr]\n"       // R2 = CFSR address
  "load_cfsr:\n"
    "ldr   r0,  [r2]\n"                 // R0 = CFSR (or CFSR_NS) register value
    "ands  r0,  r1\n"                   // Mask CFSR value with stacking error bits
    "beq   stack_check_end\n"           // If   no stacking error, jump to stack_check_end
  "stack_check_failed:\n"               // else if stacking error, stack information is invalid
    "adds  r4,  #2\n"                   // R4 |= (1 << 1)
  "stack_check_end:\n"
#endif

 /* --- Type information --- */
    "ldr   r2,  =%c[FaultInfo_type_addr]\n"
    "ldr   r0,  =%c[FaultInfo_type_val]\n"
    "str   r0,  [r2]\n"

 /* --- State Context --- */
 /* Check if state context (also additional state context if it exists) is valid and
    if it is then copy it, otherwise skip copying */
    "lsrs  r0,  r4, #2\n"               // Shift bit [1] of R4 into Carry flag
    "bcs   state_context_end\n"         // If stack is not valid (bit == 1), skip copying information from stack

#if (FR_ARCH_ARMV8x_M != 0)             // If arch is Armv8/8.1-M
 /* If additional state context was stacked upon exception entry, copy it into FaultInfo.additonal_state_context */
    "mov   r0,  lr\n"                   // R0 = LR (EXC_RETURN)
    "lsrs  r0,  r0, #6\n"               // Shift   bit [5] (DCRS) into Carry flag
    "bcs   additional_context_end\n"    // If      bit [5] (DCRS) == 1, skip additional state context
                                        // else if bit [5] (DCRS) == 0, copy additional state context
    "ldr   r2,  =%c[FaultInfo_additonal_ctx_addr]\n"
    "ldm   r3!, {r0, r1}\n"             // Stacked IntegritySignature, Reserved
    "stm   r2!, {r0, r1}\n"
    "ldm   r3!, {r0, r1}\n"             // Stacked R4, R5
    "stm   r2!, {r0, r1}\n"
    "ldm   r3!, {r0, r1}\n"             // Stacked R6, R7
    "stm   r2!, {r0, r1}\n"
    "ldm   r3!, {r0, r1}\n"             // Stacked R8, R9
    "stm   r2!, {r0, r1}\n"
    "ldm   r3!, {r0, r1}\n"             // Stacked R10, R11
    "stm   r2!, {r0, r1}\n"

  "additional_context_end:\n"
#endif

 /* Copy state context stacked on exception entry into FaultInfo.state_context */
    "ldr   r2,  =%c[FaultInfo_state_ctx_addr]\n"
    "ldm   r3!, {r0, r1}\n"             // Stacked R0, R1
    "stm   r2!, {r0, r1}\n"
    "ldm   r3!, {r0, r1}\n"             // Stacked R2, R3
    "stm   r2!, {r0, r1}\n"
    "ldm   r3!, {r0, r1}\n"             // Stacked R12, LR
    "stm   r2!, {r0, r1}\n"
    "ldm   r3!, {r0, r1}\n"             // Stacked ReturnAddress, xPSR
    "stm   r2!, {r0, r1}\n"

  "state_context_end:\n"

 /* --- Common Registers --- */
 /* Store values of Common Registers into FaultInfo.common_registers */
    "ldr   r2,  =%c[FaultInfo_common_regs_addr]\n"
    "mrs   r0,  xpsr\n"                 // R0 = current xPSR
    "mov   r1,  lr\n"                   // R1 = current LR (exception return code)
    "stm   r2!, {r0, r1}\n"
#if (FR_SECURE != 0)                    // If code was compiled for and is running in Secure World
    "lsrs  r0,  r4, #1\n"               // Shift   bit [0] of R4 into Carry flag
    "bcc   load_sps\n"                  // If      bit [0] of R4 == 0, jump to load MSP and PSP
  "load_sps_ns:\n"                      // else if bit [0] of R4 == 1, load MSP_NS and PSP_NS
    "mrs   r0,  msp_ns\n"               // R0 = current MSP_NS
    "mrs   r1,  psp_ns\n"               // R1 = current PSP_NS
    "b     store_sps\n"
#endif
  "load_sps:\n"
    "mrs   r0,  msp\n"                  // R0 = current MSP
    "mrs   r1,  psp\n"                  // R1 = current PSP
  "store_sps:\n"
    "stm   r2!, {r0, r1}\n"             // Store MSP, PSP

 /* --- Armv8/8.1-M specific Registers --- */
 /* Store values of Armv8/8.1-M specific Registers (if they exist) into FaultInfo.armv8_m_registers */
#if (FR_ARCH_ARMV8x_M != 0)             // If arch is Armv8/8.1-M
    "ldr   r2,  =%c[FaultInfo_armv8_m_regs_addr]\n"
#if (FR_SECURE != 0)                    // If code was compiled for and is running in Secure World
    "lsrs  r0,  r4, #1\n"               // Shift   bit [0] of R4 into Carry flag
    "bcc   load_splims\n"               // If      bit [0] of R4 == 0, jump to load MSPLIM and PSPLIM
#if (FR_ARCH_ARMV8_M_BASE !=0)          // If arch is Armv8-M Baseline
    "b     splims_end\n"                // MSPLIM_NS and PSPLIM_NS do not exist, skip loading and storing them
#else                                   // Else if arch is Armv8/8.1-M Mainline
  "load_splims_ns:\n"                   // else if bit [0] of R4 == 1, load MSPLIM_NS and PSPLIM_NS
    "mrs   r0,  msplim_ns\n"            // R0 = current MSPLIM_NS
    "mrs   r1,  psplim_ns\n"            // R1 = current PSPLIM_NS
    "b     store_splims\n"
#endif
#endif
  "load_splims:\n"
    "mrs   r0,  msplim\n"               // R0 = current MSP
    "mrs   r1,  psplim\n"               // R1 = current PSP
  "store_splims:\n"
    "stm   r2!, {r0, r1}\n"
  "splims_end:\n"
#endif

 /* --- Fault Registers --- */
 /* Store values of Fault Registers (if they exist) into FaultInfo.fault_registers */
#if (FR_FAULT_REGS_EXIST != 0)          // If fault registers exist
    "ldr   r2,  =%c[FaultInfo_fault_regs_addr]\n"
#if (FR_SECURE != 0)                    // If code was compiled for and is running in Secure World
    "lsrs  r0,  r4, #1\n"               // Shift   bit [0] of R4 into Carry flag
    "bcc   load_scb_addr\n"             // If      bit [0] of R4 == 0, jump to load SCB address
  "load_scb_ns_addr:\n"                 // else if bit [0] of R4 == 1, load SCB_NS address
    "ldr   r3,  =%c[scb_ns_addr]\n"
    "b     load_fault_regs\n"
  "load_scb_addr:\n"
#endif
    "ldr   r3,  =%c[scb_addr]\n"
  "load_fault_regs:\n"
    "ldr   r0,  [r3, %[cfsr_ofs]]\n"    // R0 = CFSR
    "ldr   r1,  [r3, %[hfsr_ofs]]\n"    // R1 = HFSR
    "stm   r2!, {r0, r1}\n"
    "ldr   r0,  [r3, %[dfsr_ofs]]\n"    // R0 = DFSR
    "ldr   r1,  [r3, %[mmfar_ofs]]\n"   // R1 = MMFAR
    "stm   r2!, {r0, r1}\n"
    "ldr   r0,  [r3, %[bfar_ofs]]\n"    // R0 = BFSR
    "ldr   r1,  [r3, %[afsr_ofs]]\n"    // R1 = AFSR
    "stm   r2!, {r0, r1}\n"

 /* --- Armv8/8.1-M Fault Registers --- */
 /* Store values of Armv8/8.1-M Fault Registers (if they exist) and if code is running in Secure World
    into FaultInfo.armv8_m_fault_registers */
#if (FR_SECURE != 0)                    // If code was compiled for and is running in Secure World
    "ldr   r2,  =%c[FaultInfo_armv8_m_fault_regs_addr]\n"
    "ldr   r3,  =%c[scb_addr]\n"
    "ldr   r0,  [r3, %[sfsr_ofs]]\n"    // R0 = SFSR
    "ldr   r1,  [r3, %[sfar_ofs]]\n"    // R1 = SFAR
    "stm   r2!, {r0, r1}\n"
#endif
#endif

 /* Calculate CRC-32 on FaultInfo structure (excluding magic_number and crc32 fields) and
    store it into FaultInfo.crc32 */
    "ldr   r0,  =%c[crc_init_val]\n"    // R0 = init_val parameter
    "ldr   r1,  =%c[crc_data_ptr]\n"    // R1 = data_ptr parameter
    "ldr   r2,  =%c[crc_data_len]\n"    // R2 = data_len parameter
    "ldr   r3,  =%c[crc_polynom]\n"     // R3 = polynom  parameter
    "bl    CalcCRC32\n"                 // Call CalcCRC32 function
    "ldr   r2,  =%c[FaultInfo_crc32_addr]\n"
    "str   r0,  [r2]\n"                 // Store CRC-32

 /* Store magic number into FaultInfo.magic_number */
    "ldr   r2,  =%c[FaultInfo_magic_number_addr]\n"
    "ldr   r0,  =%c[FaultInfo_magic_number_val]\n"
    "str   r0,  [r2]\n"

    "mov   r4,  r12\n"                  // Restore R4 from R12

    "bl    FaultRecordOnExit\n"         // Call FaultRecordOnExit function

 /* Inline assembly template operands */
 :  /* no outputs */
 :  /* inputs */
    [FaultInfo_addr]                    "i"     (&FaultInfo)
  , [FaultInfo_size]                    "i"     (sizeof(FaultInfo)/4)
  , [FaultInfo_magic_number_addr]       "i"     (&FaultInfo.magic_number)
  , [FaultInfo_magic_number_val]        "i"     (FR_MAGIC_NUMBER)
  , [FaultInfo_crc32_addr]              "i"     (&FaultInfo.crc32)
  , [FaultInfo_type_addr]               "i"     (&FaultInfo.type)
  , [FaultInfo_type_val]                "i"     (FR_FAULT_INFO_TYPE)
  , [FaultInfo_state_ctx_addr]          "i"     (&FaultInfo.state_context)
  , [FaultInfo_common_regs_addr]        "i"     (&FaultInfo.common_registers)
#if (FR_FAULT_REGS_EXIST != 0)
  , [FaultInfo_fault_regs_addr]         "i"     (&FaultInfo.fault_registers)
  , [cfsr_err_msk]                      "i"     (SCB_CFSR_Stack_Err_Msk)
  , [scb_addr]                          "i"     (SCB_BASE)
  , [cfsr_addr]                         "i"     (SCS_BASE + offsetof(SCB_Type, CFSR))
  , [cfsr_ofs]                          "i"     (offsetof(SCB_Type, CFSR ))
  , [hfsr_ofs]                          "i"     (offsetof(SCB_Type, HFSR ))
  , [dfsr_ofs]                          "i"     (offsetof(SCB_Type, DFSR ))
  , [mmfar_ofs]                         "i"     (offsetof(SCB_Type, MMFAR))
  , [bfar_ofs]                          "i"     (offsetof(SCB_Type, BFAR ))
  , [afsr_ofs]                          "i"     (offsetof(SCB_Type, AFSR ))
#if (FR_SECURE != 0)
  , [scb_ns_addr]                       "i"     (SCB_BASE_NS)
  , [cfsr_ns_addr]                      "i"     (SCS_BASE_NS + offsetof(SCB_Type, CFSR))
#endif
#endif
#if (FR_ARCH_ARMV8x_M != 0)
  , [FaultInfo_additonal_ctx_addr]      "i"     (&FaultInfo.additonal_state_context)
  , [FaultInfo_armv8_m_regs_addr]       "i"     (&FaultInfo.armv8_m_registers)
#endif
#if (FR_ARCH_ARMV8x_M_MAIN !=0)
  , [FaultInfo_armv8_m_fault_regs_addr] "i"     (&FaultInfo.armv8_m_fault_registers)
  , [sfsr_ofs]                          "i"     (offsetof(SCB_Type, SFSR ))
  , [sfar_ofs]                          "i"     (offsetof(SCB_Type, SFAR ))
#endif
  , [crc_init_val]                      "i"     (FR_CRC32_INIT_VAL)
  , [crc_data_ptr]                      "i"     (FR_CRC32_DATA_PTR)
  , [crc_data_len]                      "i"     (FR_CRC32_DATA_LEN)
  , [crc_polynom]                       "i"     (FR_CRC32_POLYNOM)
 :  /* clobber list */
    "r0", "r1", "r2", "r3", "r4", "r12", "lr" , "cc", "memory");
  //lint --flb "Library End (excluded from MISRA check)"
}

/**
  Print the recorded fault information.
  Should be called when system is running in normal operating mode with
  standard input/output fully functional.
*/
void FaultRecordPrint (void) {
  int8_t fault_info_valid = 0;
  int8_t state_context_valid = 1;

  // Check if magic number is valid
  if (FaultInfo.magic_number == FR_MAGIC_NUMBER) {
    const FaultInfoType_Type *ptr_fi_type = &FaultInfo.type;

    fault_info_valid = 1;
    FR_PRINT("\n--- Last recorded Fault information (v%u.%u) ---\n\n", ptr_fi_type->version.major, ptr_fi_type->version.minor);
  }

  // Check if CRC of the FaultInfo is correct
  if (fault_info_valid != 0) {
    if (FaultInfo.crc32 != CalcCRC32(FR_CRC32_INIT_VAL, (const uint8_t *)FR_CRC32_DATA_PTR, FR_CRC32_DATA_LEN, FR_CRC32_POLYNOM)) {
      fault_info_valid = 0;
      FR_PRINT("\n  Invalid CRC of the recorded fault information !!!\n\n");
    }
  }

  // Check if state context was stacked properly if CFSR is available
#if (FR_FAULT_REGS_EXIST != 0)
  if ((FaultInfo.fault_registers.SCB_CFSR & (SCB_CFSR_Stack_Err_Msk)) != 0U) {
    state_context_valid = 0;
  }
#endif

  // Decode: Exception which recorded the fault information
  if (fault_info_valid != 0) {
    uint32_t exc_num = FaultInfo.common_registers.xPSR & IPSR_ISR_Msk;

    FR_PRINT("  Exception Handler: ");

#if (FR_ARCH_ARMV8x_M != 0)
    if (FaultInfo.type.secure != 0U) {
      FR_PRINT("Secure - ");
    } else {
      FR_PRINT("Non-Secure - ");
    }
#endif

    switch (exc_num) {
      case 3:
        FR_PRINT("HardFault");
        break;
      case 4:
        FR_PRINT("MemManage fault");
        break;
      case 5:
        FR_PRINT("BusFault");
        break;
      case 6:
        FR_PRINT("UsageFault");
        break;
      case 7:
        FR_PRINT("SecureFault");
        break;
      default:
        FR_PRINT("unknown, exception number = %u", exc_num);
        break;
    }

    FR_PRINT("\n");
  }

#if (FR_ARCH_ARMV8x_M != 0)
  // Decode: State in which fault occurred
  if (fault_info_valid != 0) {
    uint32_t exc_return = FaultInfo.common_registers.EXC_RETURN;

    FR_PRINT("  State:             ");

    if ((exc_return & EXC_RETURN_S) != 0U) {
      FR_PRINT("Secure");
    } else {
      FR_PRINT("Non-Secure");
    }

    FR_PRINT("\n");
  }
#endif

  // Decode: Mode in which fault occurred
  if (fault_info_valid != 0) {
    uint32_t exc_return = FaultInfo.common_registers.EXC_RETURN;

    FR_PRINT("  Mode:              ");

    if ((exc_return & (1UL << 2)) == 0U) {
      FR_PRINT("Handler");
    } else {
      FR_PRINT("Thread");
    }

    FR_PRINT("\n");
  }

#if (FR_FAULT_REGS_EXIST != 0)
  /* Decode: HardFault */
  if ((fault_info_valid != 0) && (FaultInfo.type.fault_regs != 0U)) {
    uint32_t scb_hfsr = FaultInfo.fault_registers.SCB_HFSR;

    if ((scb_hfsr & (SCB_HFSR_VECTTBL_Msk   |
                     SCB_HFSR_FORCED_Msk    |
                     SCB_HFSR_DEBUGEVT_Msk  )) != 0U) {

      FR_PRINT("  Fault:             HardFault - ");

      if ((scb_hfsr & SCB_HFSR_VECTTBL_Msk) != 0U) {
        FR_PRINT("Bus error on vector read");
      }
      if ((scb_hfsr & SCB_HFSR_FORCED_Msk) != 0U) {
        FR_PRINT("Escalated fault (original fault was disabled or it caused another lower priority fault)");
      }
      if ((scb_hfsr & SCB_HFSR_DEBUGEVT_Msk) != 0U) {
        FR_PRINT("Breakpoint hit with Debug Monitor disabled");
      }

      FR_PRINT("\n");
    }
  }

  /* Decode: MemManage fault */
  if ((fault_info_valid != 0) && (FaultInfo.type.fault_regs != 0U)) {
    uint32_t scb_cfsr  = FaultInfo.fault_registers.SCB_CFSR;
    uint32_t scb_mmfar = FaultInfo.fault_registers.SCB_MMFAR;

    if ((scb_cfsr & (SCB_CFSR_IACCVIOL_Msk  |
                     SCB_CFSR_DACCVIOL_Msk  |
                     SCB_CFSR_MUNSTKERR_Msk |
#ifdef SCB_CFSR_MLSPERR_Msk
                     SCB_CFSR_MLSPERR_Msk   |
#endif
                     SCB_CFSR_MSTKERR_Msk   )) != 0U) {

      FR_PRINT("  Fault:             MemManage - ");

      if ((scb_cfsr & SCB_CFSR_IACCVIOL_Msk) != 0U) {
        FR_PRINT("Instruction execution failure due to MPU violation or fault");
      }
      if ((scb_cfsr & SCB_CFSR_DACCVIOL_Msk) != 0U) {
        FR_PRINT("Data access failure due to MPU violation or fault");
      }
      if ((scb_cfsr & SCB_CFSR_MUNSTKERR_Msk) != 0U) {
        FR_PRINT("Exception exit unstacking failure due to MPU access violation");
      }
      if ((scb_cfsr & SCB_CFSR_MSTKERR_Msk) != 0U) {
        FR_PRINT("Exception entry stacking failure due to MPU access violation");
      }
#ifdef SCB_CFSR_MLSPERR_Msk
      if ((scb_cfsr & SCB_CFSR_MLSPERR_Msk) != 0U) {
        FR_PRINT("Floating-point lazy stacking failure due to MPU access violation");
      }
#endif
      if ((scb_cfsr & SCB_CFSR_MMARVALID_Msk) != 0U) {
        FR_PRINT(", fault address 0x%08X", scb_mmfar);
      }

      FR_PRINT("\n");
    }
  }

  /* Decode: BusFault */
  if ((fault_info_valid != 0) && (FaultInfo.type.fault_regs != 0U)) {
    uint32_t scb_cfsr = FaultInfo.fault_registers.SCB_CFSR;
    uint32_t scb_bfar = FaultInfo.fault_registers.SCB_BFAR;

    if ((scb_cfsr & (SCB_CFSR_IBUSERR_Msk     |
                     SCB_CFSR_PRECISERR_Msk   |
                     SCB_CFSR_IMPRECISERR_Msk |
                     SCB_CFSR_UNSTKERR_Msk    |
#ifdef SCB_CFSR_LSPERR_Msk
                     SCB_CFSR_LSPERR_Msk      |
#endif
                     SCB_CFSR_STKERR_Msk      )) != 0U) {

      FR_PRINT("  Fault:             BusFault - ");

      if ((scb_cfsr & SCB_CFSR_IBUSERR_Msk) != 0U) {
        FR_PRINT("Instruction prefetch failure due to bus fault");
      }
      if ((scb_cfsr & SCB_CFSR_PRECISERR_Msk) != 0U) {
        FR_PRINT("Data access failure due to bus fault (precise)");
      }
      if ((scb_cfsr & SCB_CFSR_IMPRECISERR_Msk) != 0U) {
        FR_PRINT("Data access failure due to bus fault (imprecise)");
      }
      if ((scb_cfsr & SCB_CFSR_UNSTKERR_Msk) != 0U) {
        FR_PRINT("Exception exit unstacking failure due to bus fault");
      }
      if ((scb_cfsr & SCB_CFSR_STKERR_Msk) != 0U) {
        FR_PRINT("Exception entry stacking failure due to bus fault");
      }
#ifdef SCB_CFSR_LSPERR_Msk
      if ((scb_cfsr & SCB_CFSR_LSPERR_Msk) != 0U) {
        FR_PRINT("Floating-point lazy stacking failure due to bus fault");
      }
#endif
      if ((scb_cfsr & SCB_CFSR_BFARVALID_Msk) != 0U) {
        FR_PRINT(", fault address 0x%08X", scb_bfar);
      }

      FR_PRINT("\n");
    }
  }

  /* Decode: UsageFault */
  if ((fault_info_valid != 0) && (FaultInfo.type.fault_regs != 0U)) {
    uint32_t scb_cfsr = FaultInfo.fault_registers.SCB_CFSR;

    if ((scb_cfsr & (SCB_CFSR_UNDEFINSTR_Msk |
                     SCB_CFSR_INVSTATE_Msk   |
                     SCB_CFSR_INVPC_Msk      |
                     SCB_CFSR_NOCP_Msk       |
#ifdef SCB_CFSR_STKOF_Msk
                     SCB_CFSR_STKOF_Msk      |
#endif
                     SCB_CFSR_UNALIGNED_Msk  |
                     SCB_CFSR_DIVBYZERO_Msk  )) != 0U) {

      FR_PRINT("  Fault:             UsageFault - ");

      if ((scb_cfsr & SCB_CFSR_UNDEFINSTR_Msk) != 0U) {
        FR_PRINT("Execution of undefined instruction");
      }
      if ((scb_cfsr & SCB_CFSR_INVSTATE_Msk) != 0U) {
        FR_PRINT("Execution of Thumb instruction with Thumb mode turned off");
      }
      if ((scb_cfsr & SCB_CFSR_INVPC_Msk) != 0U) {
        FR_PRINT("Invalid exception return value");
      }
      if ((scb_cfsr & SCB_CFSR_NOCP_Msk) != 0U) {
        FR_PRINT("Coprocessor instruction with coprocessor disabled or non-existent");
      }
#ifdef SCB_CFSR_STKOF_Msk
      if ((scb_cfsr & SCB_CFSR_STKOF_Msk) != 0U) {
        FR_PRINT("Stack overflow");
      }
#endif
      if ((scb_cfsr & SCB_CFSR_UNALIGNED_Msk) != 0U) {
        FR_PRINT("Unaligned load/store");
      }
      if ((scb_cfsr & SCB_CFSR_DIVBYZERO_Msk) != 0U) {
        FR_PRINT("Divide by 0");
      }

      FR_PRINT("\n");
    }
  }

#if (FR_ARCH_ARMV8x_M_MAIN != 0)
  /* Decode: SecureFault */
  if ((fault_info_valid != 0) && (FaultInfo.type.secure != 0U)) {
    uint32_t scb_sfsr = FaultInfo.armv8_m_fault_registers.SCB_SFSR;
    uint32_t scb_sfar = FaultInfo.armv8_m_fault_registers.SCB_SFAR;

    if ((scb_sfsr & (SAU_SFSR_INVEP_Msk   |
                     SAU_SFSR_INVIS_Msk   |
                     SAU_SFSR_INVER_Msk   |
                     SAU_SFSR_AUVIOL_Msk  |
                     SAU_SFSR_INVTRAN_Msk |
                     SAU_SFSR_LSPERR_Msk  |
                     SAU_SFSR_LSERR_Msk   )) != 0U) {

      FR_PRINT("  Fault:             SecureFault - ");

      if ((scb_sfsr & SAU_SFSR_INVEP_Msk) != 0U) {
        FR_PRINT("Invalid entry point due to invalid attempt to enter Secure state");
      }
      if ((scb_sfsr & SAU_SFSR_INVIS_Msk) != 0U) {
        FR_PRINT("Invalid integrity signature in exception stack frame found on unstacking");
      }
      if ((scb_sfsr & SAU_SFSR_INVER_Msk) != 0U) {
        FR_PRINT("Invalid exception return due to mismatch on EXC_RETURN.DCRS or EXC_RETURN.ES");
      }
      if ((scb_sfsr & SAU_SFSR_AUVIOL_Msk) != 0U) {
        FR_PRINT("Attribution unit violation due to Non-secure access to Secure address space");
      }
      if ((scb_sfsr & SAU_SFSR_INVTRAN_Msk) != 0U) {
        FR_PRINT("Invalid transaction caused by domain crossing branch not flagged as such");
      }
      if ((scb_sfsr & SAU_SFSR_LSPERR_Msk) != 0U) {
        FR_PRINT("Lazy stacking preservation failure due to SAU or IDAU violation");
      }
      if ((scb_sfsr & SAU_SFSR_LSERR_Msk) != 0U) {
        FR_PRINT("Lazy stacking activation or deactivation failure");
      }
      if ((scb_sfsr & SAU_SFSR_SFARVALID_Msk) != 0U) {
        FR_PRINT(", fault address 0x%08X", scb_sfar);
      }

      FR_PRINT("\n");
    }
  }
#endif
#endif

  // Print: Program Counter, MSP (if TrustZone also MSPLIM), PSP (if TrustZone also PSPLIM)
  if (fault_info_valid != 0) {

    FR_PRINT("\n");

#if (FR_FAULT_REGS_EXIST != 0)
    FR_PRINT("   - PC:             ");
    if ((FaultInfo.fault_registers.SCB_CFSR & (SCB_CFSR_Stack_Err_Msk)) == 0U) {
      FR_PRINT("0x%08X\n", FaultInfo.state_context.ReturnAddress);
    } else {
      FR_PRINT("unknown\n");
    }
#else
    FR_PRINT("   - PC:             0x%08X\n", FaultInfo.state_context.ReturnAddress);
#endif
    FR_PRINT("   - MSP:            0x%08X\n", FaultInfo.common_registers.MSP);
#if (FR_ARCH_ARMV8x_M     != 0)
#if (FR_ARCH_ARMV8_M_BASE != 0)
    if ((FaultInfo.common_registers.EXC_RETURN & EXC_RETURN_S) != 0) {
      FR_PRINT("   - MSPLIM:         0x%08X\n", FaultInfo.armv8_m_registers.MSPLIM);
    }
#else
    FR_PRINT("   - MSPLIM:         0x%08X\n", FaultInfo.armv8_m_registers.MSPLIM);
#endif
#endif
    FR_PRINT("   - PSP:            0x%08X\n", FaultInfo.common_registers.PSP);
#if (FR_ARCH_ARMV8x_M     != 0)
#if (FR_ARCH_ARMV8_M_BASE != 0)
    if ((FaultInfo.common_registers.EXC_RETURN & EXC_RETURN_S) != 0) {
      FR_PRINT("   - PSPLIM:         0x%08X\n", FaultInfo.armv8_m_registers.PSPLIM);
    }
#else
    FR_PRINT("   - PSPLIM:         0x%08X\n", FaultInfo.armv8_m_registers.PSPLIM);
#endif
#endif

    FR_PRINT("\n");
  }

  /* Print state context information */
  if ((fault_info_valid != 0) && (state_context_valid != 0))  {
    const StateContext_Type *ptr_state_ctx = &FaultInfo.state_context;

    FR_PRINT("  Exception stacked state context:\n");
    FR_PRINT("   - R0:             0x%08X\n", ptr_state_ctx->R0);
    FR_PRINT("   - R1:             0x%08X\n", ptr_state_ctx->R1);
    FR_PRINT("   - R2:             0x%08X\n", ptr_state_ctx->R2);
    FR_PRINT("   - R3:             0x%08X\n", ptr_state_ctx->R3);
  }

#if (FR_ARCH_ARMV8x_M != 0)
  if ((fault_info_valid != 0) && (state_context_valid != 0) && (FaultInfo.type.armv8m != 0U))  {
    /* Print additional state context (if it exists) */
    const AdditionalStateContext_Type *ptr_asc = &FaultInfo.additonal_state_context;

    if ((ptr_asc->IntegritySignature & 0xFFFFFFFEU) == FR_ASC_INTEGRITY_SIG) {
      FR_PRINT("   - R4:             0x%08X\n", ptr_asc->R4);
      FR_PRINT("   - R5:             0x%08X\n", ptr_asc->R5);
      FR_PRINT("   - R6:             0x%08X\n", ptr_asc->R6);
      FR_PRINT("   - R7:             0x%08X\n", ptr_asc->R7);
      FR_PRINT("   - R8:             0x%08X\n", ptr_asc->R8);
      FR_PRINT("   - R9:             0x%08X\n", ptr_asc->R9);
      FR_PRINT("   - R10:            0x%08X\n", ptr_asc->R10);
      FR_PRINT("   - R11:            0x%08X\n", ptr_asc->R11);
    }
  }
#endif

  if ((fault_info_valid != 0) && (state_context_valid != 0))  {
    const StateContext_Type *ptr_state_ctx = &FaultInfo.state_context;

    FR_PRINT("   - R12:            0x%08X\n", ptr_state_ctx->R12);
    FR_PRINT("   - LR:             0x%08X\n", ptr_state_ctx->LR);
    FR_PRINT("   - ReturnAddress:  0x%08X\n", ptr_state_ctx->ReturnAddress);
    FR_PRINT("   - xPSR:           0x%08X\n", ptr_state_ctx->xPSR);

    FR_PRINT("\n");
  }

#if (FR_FAULT_REGS_EXIST  != 0)
  /* Print fault registers */
  if (fault_info_valid != 0) {
    const FaultRegisters_Type *ptr_fault_regs = &FaultInfo.fault_registers;

    FR_PRINT("  Fault registers:\n");

    FR_PRINT("   - CFSR:           0x%08X\n", ptr_fault_regs->SCB_CFSR);
    FR_PRINT("   - HFSR:           0x%08X\n", ptr_fault_regs->SCB_HFSR);
    FR_PRINT("   - DFSR:           0x%08X\n", ptr_fault_regs->SCB_DFSR);
    FR_PRINT("   - MMFAR:          0x%08X\n", ptr_fault_regs->SCB_MMFAR);
    FR_PRINT("   - BFAR:           0x%08X\n", ptr_fault_regs->SCB_BFAR);
    FR_PRINT("   - AFSR:           0x%08X\n", ptr_fault_regs->SCB_AFSR);
#if (FR_ARCH_ARMV8x_M_MAIN != 0)
    if (FaultInfo.type.secure != 0U) {
      const Armv8mFaultRegisters_Type *ptr_armv8_m_regs = &FaultInfo.armv8_m_fault_registers;

      FR_PRINT("   - SFSR:           0x%08X\n", ptr_armv8_m_regs->SCB_SFSR);
      FR_PRINT("   - SFAR:           0x%08X\n", ptr_armv8_m_regs->SCB_SFAR);
    }
#endif

    FR_PRINT("\n");
  }
#endif
}

/**
  Clear the recorded fault information.
*/
void FaultRecordClear (void) {
  memset(&FaultInfo, 0, sizeof(FaultInfo));
}

// Helper functions

#ifdef __ICCARM__
#pragma diag_suppress=Pe940
#endif

//lint ++flb "Library Begin (excluded from MISRA check)"

/**
  Calculate CRC-32 on data block in memory
  \param[in]    init_val        initial CRC value
  \param[in]    data_ptr        pointer to data
  \param[in]    data_len        data length (in bytes)
  \param[in]    polynom         CRC polynom
  \return       CRC-32 value (32-bit)
*/
static __NAKED uint32_t CalcCRC32 (      uint32_t init_val,
                                   const uint8_t *data_ptr,
                                         uint32_t data_len,
                                         uint32_t polynom) {
  __ASM volatile (
#ifndef __ICCARM__
    ".syntax unified\n"
#endif
    "mov   r12, r4\n"
    "b     check\n"
  "loop:\n"
    "ldrb  r4,  [r1]\n"
    "lsls  r4,  r4, #24\n"
    "eors  r0,  r0, r4\n"
    "lsls  r0,  r0, #1\n"
    "bcc   skip_xor_7\n"
    "eors  r0,  r0, r3\n"
  "skip_xor_7:\n"
    "lsls  r0,  r0, #1\n"
    "bcc   skip_xor_6\n"
    "eors  r0,  r0, r3\n"
  "skip_xor_6:\n"
    "lsls  r0,  r0, #1\n"
    "bcc   skip_xor_5\n"
    "eors  r0,  r0, r3\n"
  "skip_xor_5:\n"
    "lsls  r0,  r0, #1\n"
    "bcc   skip_xor_4\n"
    "eors  r0,  r0, r3\n"
  "skip_xor_4:\n"
    "lsls  r0,  r0, #1\n"
    "bcc   skip_xor_3\n"
    "eors  r0,  r0, r3\n"
  "skip_xor_3:\n"
    "lsls  r0,  r0, #1\n"
    "bcc   skip_xor_2\n"
    "eors  r0,  r0, r3\n"
  "skip_xor_2:\n"
    "lsls  r0,  r0, #1\n"
    "bcc   skip_xor_1\n"
    "eors  r0,  r0, r3\n"
  "skip_xor_1:\n"
    "lsls  r0,  r0, #1\n"
    "bcc   skip_xor_0\n"
    "eors  r0,  r0, r3\n"
  "skip_xor_0:\n"
    "adds  r1,  r1, #1\n"
    "subs  r2,  r2, #1\n"
  "check:\n"
    "cmp   r2,  #0\n"
    "bne   loop\n"
    "mov   r4,  r12\n"
    "bx    lr\n"
 :::
    "r0", "r1", "r2", "r3", "r4", "r12", "cc");
}

//lint --flb "Library End (excluded from MISRA check)"

#ifdef __ICCARM__
#pragma diag_warning=Pe940
#endif
