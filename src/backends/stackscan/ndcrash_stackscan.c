#include "ndcrash_backends.h"
#include "ndcrash_dump.h"
#include "ndcrash_private.h"
#include "ndcrash_memory_map.h"
#include "ndcrash_log.h"
#include <unwind.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <unistd.h>

#ifdef ENABLE_INPROCESS

static uintptr_t ndcrash_pc_from_ucontext(const ucontext_t *context) {
#if (defined(__arm__))
    return context->uc_mcontext.arm_pc;
#elif (defined(__i386__))
    return context->uc_mcontext.gregs[REG_EIP];
#endif
}

static uintptr_t ndcrash_sp_from_ucontext(const ucontext_t *uc) {
#if (defined(__arm__))
    return uc->uc_mcontext.arm_sp;
#elif (defined(__i386__))
    return uc->uc_mcontext.gregs[REG_ESP];
#endif
}

static void ndcrash_try_unwind_frame(uintptr_t pc, int outfile, int *frameno, bool rewind) {
    Dl_info info;
    if (pc && dladdr((void *)pc, &info)) {
        if (rewind) {
#ifdef __arm__
            pc-= 4; //Thumb isn't supported.
#elif defined(__i386__)
            pc -= 1;
#endif
        }
        if (info.dli_sname && info.dli_saddr) {
            ndcrash_dump_backtrace_line_full(
                    outfile,
                     *frameno,
                     (uintptr_t)pc - (uintptr_t)info.dli_fbase,
                     info.dli_fname,
                     info.dli_sname,
                     (uintptr_t)pc - (uintptr_t)info.dli_saddr
            );
        } else {
            ndcrash_dump_backtrace_line_part(
                    outfile,
                     *frameno,
                     (uintptr_t )pc - (uintptr_t)info.dli_fbase,
                     info.dli_fname
            );
        }
        ++(*frameno);
    }
}

typedef struct {
    uintptr_t sp, end;
} ndcrash_stackscan_stack_t;

static void ndcrash_stackscan_maps_callback(uintptr_t start, uintptr_t end, void *data, bool *stop) {
    ndcrash_stackscan_stack_t *stack = (ndcrash_stackscan_stack_t *)data;
    if (start <= stack->sp && stack->sp < end) {
        if (stack->end > end) {
            stack->end = end;
        }
        *stop = true;
    }
}

void ndcrash_in_unwind_stackscan(int outfile, struct ucontext *context) {

    // Program counter is always the first element
    int frameno = 0;
    ndcrash_try_unwind_frame(ndcrash_pc_from_ucontext(context), outfile, &frameno, false);
#ifdef __arm__
    ndcrash_try_unwind_frame(context->uc_mcontext.arm_lr, outfile, &frameno, true);
#endif

    ndcrash_stackscan_stack_t stack;
    stack.sp = ndcrash_sp_from_ucontext(context);
    stack.end = stack.sp + getpagesize();

    // Searching sp value in memory map in order to avoid walking out of stack bounds.
    ndcrash_parse_memory_map(getpid(), &ndcrash_stackscan_maps_callback, &stack);

    uintptr_t *stack_content = (uintptr_t *) stack.sp;
    const uintptr_t *stack_end = (uintptr_t *) stack.end;
    for (; stack_content != stack_end; ++stack_content) {
        ndcrash_try_unwind_frame(*stack_content, outfile, &frameno, true);
    }
}

#endif //ENABLE_INPROCESS
