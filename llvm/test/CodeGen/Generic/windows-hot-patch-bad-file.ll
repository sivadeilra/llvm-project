; RUN: not --crash llc -mtriple=x86_64-windows -windows-hot-patch-function-file /dev/does-not-exist < %s 2>&1 | FileCheck %s

; CHECK: LLVM ERROR: Windows hot patching couldn't load file '/dev/does-not-exist'

define i32 @main() #0 {
  ret i32 0
}
