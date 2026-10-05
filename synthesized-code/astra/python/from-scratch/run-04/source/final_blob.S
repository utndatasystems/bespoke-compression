.section .rodata
.balign 16
.global fitted_trace
.hidden fitted_trace
fitted_trace:
.incbin "/source/final_trace.bin"
.global fitted_trace_end
.hidden fitted_trace_end
fitted_trace_end:
.section .note.GNU-stack,"",@progbits
