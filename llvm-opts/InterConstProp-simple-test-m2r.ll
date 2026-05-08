; ModuleID = 'InterConstProp-simple-test.bc'
source_filename = "InterConstProp-simple-inter.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [10 x i8] c"%d %d %d\0A\00", align 1

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @get_ten() #0 {
entry:
  ret i32 10
}

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @double_ten() #0 {
entry:
  %call = call i32 @get_ten()
  %add = add nsw i32 %call, %call
  ret i32 %add
}

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @add(i32 noundef %a, i32 noundef %b) #0 {
entry:
  %add = add nsw i32 %a, %b
  ret i32 %add
}

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @combine() #0 {
entry:
  %call = call i32 @get_ten()
  %call1 = call i32 @add(i32 noundef %call, i32 noundef 5)
  ret i32 %call1
}

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @main() #0 {
entry:
  %call = call i32 @get_ten()
  %call1 = call i32 @double_ten()
  %call2 = call i32 @combine()
  %call3 = call i32 (ptr, ...) @printf(ptr noundef @.str, i32 noundef %call, i32 noundef %call1, i32 noundef %call2)
  ret i32 0
}

declare i32 @printf(ptr noundef, ...) #1

attributes #0 = { noinline nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 21.1.8 (https://github.com/llvm/llvm-project 2078da43e25a4623cab2d0d60decddf709aaea28)"}
