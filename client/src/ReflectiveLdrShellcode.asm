; Reflective DLL loader shellcode (x64 MASM)
; Calls DllMain(hinstDLL, DLL_PROCESS_ATTACH, NULL)
;
; Entry: RCX = pointer to ReflectiveLoaderParams
;   [rcx+0] = hinstDLL (base address of manually-mapped DLL)
;   [rcx+8] = DllMain address (base + AddressOfEntryPoint)
;
; Compile with: ml64 /c /FoReflectiveLdrShellcode.obj ReflectiveLdrShellcode.asm

.code

ReflectiveEntry PROC
    mov     r10, [rcx]          ; r10 = hinstDLL
    mov     r11, [rcx+8]        ; r11 = DllMain address
    xor     r8d, r8d            ; r8 = NULL (3rd arg: lpvReserved)
    mov     edx, 1              ; rdx = DLL_PROCESS_ATTACH (2nd arg)
    mov     rcx, r10            ; rcx = hinstDLL (1st arg)
    sub     rsp, 40             ; Shadow space (32) + alignment (8)
    call    r11                 ; call DllMain(hinstDLL, 1, NULL)
    add     rsp, 40             ; Restore stack
    xor     eax, eax            ; return 0
    ret
ReflectiveEntry ENDP

END
