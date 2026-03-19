#include "cm/error.h"
#include "cm/error_detail.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif
#define cm_write _write
#else
#include <unistd.h>
#define cm_write write
#endif

__thread cm_exception_frame_t* cm_current_frame = NULL;

static cm_error_code_t cm_last_error = CM_SUCCESS;
static char cm_error_message[1024] = {0};

void cm_error_set(cm_error_code_t error, const char* message) {
    cm_last_error = error;
    if (message) {
        strncpy(cm_error_message, message, sizeof(cm_error_message) - 1);
        cm_error_message[sizeof(cm_error_message) - 1] = '\0';
    } else {
        cm_error_message[0] = '\0';
    }
}

cm_error_code_t cm_error_get_last(void) {
    return cm_last_error;
}

const char* cm_error_get_message(void) {
    return cm_error_message;
}

void cm_error_clear(void) {
    cm_last_error = CM_SUCCESS;
    cm_error_message[0] = '\0';
}

static void cm_signal_handler(int sig) {
    const char* sig_name = "Unknown Signal";
    cm_error_code_t code = CM_ERROR_UNKNOWN;
    const char* obj_type = "unknown";
    const char* status = "Unknown error";
    const char* fix = "Check error logs for details";
    
    switch(sig) {
        case SIGSEGV: 
            sig_name = "SIGSEGV (Segmentation Fault)";
            code = CM_ERROR_NULL_POINTER;
            obj_type = "pointer";
            status = "Accessed invalid memory address";
            fix = "Check for null pointer dereference or use-after-free";
            break;
        case SIGABRT: 
            sig_name = "SIGABRT (Abort)";
            code = CM_ERROR_RUNTIME;
            status = "Program aborted";
            fix = "Check assertion failures or abort() calls";
            break;
        case SIGFPE:  
            sig_name = "SIGFPE (Floating Point Exception)";
            code = CM_ERROR_DIVISION_BY_ZERO;
            obj_type = "number";
            status = "Division by zero or invalid math operation";
            fix = "Check denominator is not zero before dividing";
            break;
        case SIGILL:  
            sig_name = "SIGILL (Illegal Instruction)";
            code = CM_ERROR_RUNTIME;
            status = "Illegal instruction executed";
            fix = "Check for corrupted binary or invalid code generation";
            break;
    }
    
    /* Build enhanced error message using error_detail system */
    cm_error_detail_t detail;
    cm_error_detail_init(&detail);
    detail.code = code;
    detail.severity = CM_SEVERITY_FATAL;
    cm_error_detail_set_location(&detail, "unknown", 0, 0);
    cm_error_detail_set_object(&detail, obj_type, "unknown");
    cm_error_detail_set_message(&detail, "%s", status);
    cm_error_detail_set_suggestion(&detail, "%s", fix);
    
    /* For critical crashes, we use async-signal-safe write */
    if (sig == SIGSEGV) {
        const char* neon_err = 
            "\n\n\xE2\x9A\xA0\xEF\xB8\x8F  CM Runtime Error:\n"
            "   Location: unknown -> Line 0\n"
            "   Object: ptr pointer unknown\n"
            "   Status: Accessed invalid memory address\n"
            "   Fix: Check for null pointer dereference or use-after-free\n\n";
        cm_write(STDERR_FILENO, neon_err, strlen(neon_err));
    } else {
        const char* header = "\n\n\xF0\x9F\x92\xA5 CRITICAL FATAL ERROR: ";
        cm_write(STDERR_FILENO, header, strlen(header));
        cm_write(STDERR_FILENO, sig_name, strlen(sig_name));
        cm_write(STDERR_FILENO, "\n", 1);
        
        const char* status_msg = "   Status: ";
        cm_write(STDERR_FILENO, status_msg, strlen(status_msg));
        cm_write(STDERR_FILENO, status, strlen(status));
        cm_write(STDERR_FILENO, "\n", 1);
        
        const char* fix_msg = "   Fix: ";
        cm_write(STDERR_FILENO, fix_msg, strlen(fix_msg));
        cm_write(STDERR_FILENO, fix, strlen(fix));
        cm_write(STDERR_FILENO, "\n\n", 2);
    }
    
    _exit(1);
}

void cm_init_error_detector(void) {
    signal(SIGSEGV, cm_signal_handler);
    signal(SIGABRT, cm_signal_handler);
    signal(SIGFPE,  cm_signal_handler);
    signal(SIGILL,  cm_signal_handler);
}
