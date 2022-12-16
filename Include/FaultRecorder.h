/*------------------------------------------------------------------------------
 * MDK - Component ::Fault Recorder
 * Copyright (c) 2022 ARM Germany GmbH. All rights reserved.
 *------------------------------------------------------------------------------
 * Name:    FaultRecorder.h
 * Purpose: Fault Recorder Header File
 * Rev.:    V0.1.0
 *----------------------------------------------------------------------------*/

#ifndef __FAULT_RECORDER_H
#define __FAULT_RECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

// Fault Recorder callback functions -------------------------------------------

/// Callback function called after fault information was recorded.
extern void FaultRecordOnExit (void);

// Fault Recorder functions ----------------------------------------------------

/// Record fault information.
extern void FaultRecord (void);

/// Print recorded fault information.
extern void FaultRecordPrint (void);

/// Clear recorded fault information.
extern void FaultRecordClear (void);

#ifdef __cplusplus
}
#endif

#endif /* __FAULT_RECORDER_H */
