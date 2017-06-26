define internal void @ctor3() {
  call void @func1()
  ret void
}

define internal void @ctor4() {
  ret void
}

define void @func1() {
  ret void
}

@llvm.global_dtors = appending global[2 x{i32, void() *, i8 * }] [
    {i32, void() *, i8 * } { i32 1, void() *@ctor3, i8 *null},
    {i32, void() *, i8 * } { i32 9, void() *@ctor4, i8 *null}]
