#pragma once

typedef enum {
    /** Selected successfully */
    PICKER_FINISH_REASON_SELECTED,
    /** The selection was cancelled (e.g. via the Escape key)  */
    PICKER_FINISH_REASON_CANCELLED,
    /**
     * The picker was destroyed through external means, such as its
     * output disappearing
     */
    PICKER_FINISH_REASON_DESTROYED
} PickerFinishReason;
