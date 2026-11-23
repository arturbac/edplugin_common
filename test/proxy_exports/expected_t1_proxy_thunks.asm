; t1 proxy exports, made by edplugin_common_proxy_exports from list.txt
; Each thunk is one indirect jmp through its slot: no prologue, the caller's frame and arguments
; reach the target as they are.

.DATA

PUBLIC t1_proxy_real_procs
t1_proxy_real_procs QWORD 3 DUP(0)

.CODE

PUBLIC t1_proxy_unresolved
t1_proxy_unresolved PROC
    xor eax, eax
    ret
t1_proxy_unresolved ENDP

PUBLIC t1_proxy_thunk_0
t1_proxy_thunk_0 PROC
    jmp QWORD PTR [t1_proxy_real_procs + 0]
t1_proxy_thunk_0 ENDP

PUBLIC t1_proxy_thunk_1
t1_proxy_thunk_1 PROC
    jmp QWORD PTR [t1_proxy_real_procs + 8]
t1_proxy_thunk_1 ENDP

PUBLIC t1_proxy_thunk_2
t1_proxy_thunk_2 PROC
    jmp QWORD PTR [t1_proxy_real_procs + 16]
t1_proxy_thunk_2 ENDP

END
