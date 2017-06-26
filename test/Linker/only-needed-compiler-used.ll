; RUN: llvm-link -S -only-needed              %s %p/Inputs/only-needed-compiler-used.src.ll | FileCheck %s --check-prefix=CHECK --check-prefix=NO-INTERNALIZE
; RUN: llvm-link -S -only-needed -internalize %s %p/Inputs/only-needed-compiler-used.src.ll | FileCheck %s --check-prefix=CHECK --check-prefix=INTERNALIZE

; Empty destination module!


; CHECK:          @llvm.compiler.used = appending global [2 x i8*] [i8* @used1, i8* bitcast (i32* @used2 to i8*)], section "llvm.metadata"
; NO-INTERNALIZE: @used1 = global i8 4
; INTERNALIZE:    @used1 = internal global i8 4
; NO-INTERNALIZE: @used2 = global i32 123
; INTERNALIZE:    @used2 = internal global i32 123
