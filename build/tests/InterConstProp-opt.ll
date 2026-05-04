; ModuleID = 'build/tests/InterConstProp-opt.bc'
source_filename = "constantprop-cfg-test-m2r.ll"

define i32 @test_cfg_merge_loop(i1 %cond, i32 %n) {
entry:
  %base = add i32 40, 2
  %seed = add i32 %n, 0
  br i1 %cond, label %then, label %else

then:                                             ; preds = %entry
  %t = add i32 %base, 8
  br label %merge

else:                                             ; preds = %entry
  %e = add i32 %base, 8
  br label %merge

merge:                                            ; preds = %else, %then
  %m = phi i32 [ %t, %then ], [ %e, %else ]
  %sum0 = add i32 %m, 1
  br label %loop

loop:                                             ; preds = %loop, %merge
  %i = phi i32 [ 0, %merge ], [ %inext, %loop ]
  %acc = phi i32 [ %sum0, %merge ], [ %accnext, %loop ]
  %accnext = add i32 %acc, 2
  %inext = add i32 %i, 1
  %done = icmp eq i32 %inext, %n
  br i1 %done, label %exit, label %loop

exit:                                             ; preds = %loop
  ret i32 %acc
}
