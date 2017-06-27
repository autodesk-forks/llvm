; RUN: llvm-link -S                           %s %p/Inputs/only-needed-dtors.ll | FileCheck %s --check-prefix=CHECK --check-prefix=NO-INTERNALIZE
; RUN: llvm-link -S              -internalize %s %p/Inputs/only-needed-dtors.ll | FileCheck %s --check-prefix=CHECK --check-prefix=INTERNALIZE
; RUN: llvm-link -S -only-needed              %s %p/Inputs/only-needed-dtors.ll | FileCheck %s --check-prefix=CHECK --check-prefix=NO-INTERNALIZE
; RUN: llvm-link -S -only-needed -internalize %s %p/Inputs/only-needed-dtors.ll | FileCheck %s --check-prefix=CHECK --check-prefix=INTERNALIZE

; Empty destination module!


; CHECK:          @llvm.global_dtors = appending global [2 x { i32, void ()*, i8* }] [{ i32, void ()*, i8* } { i32 2, void ()* @ctor1, i8* null }, { i32, void ()*, i8* } { i32 7, void ()* @ctor2, i8* null }]
; CHECK:          define internal void @ctor1()
; CHECK:          define internal void @ctor2()
; NO-INTERNALIZE: define void @func1()
; INTERNALIZE:    define internal void @func1()
