# interprocedural\_constant\_propagation

For simplicity we decided to follow the same exact process as the previous homework assignments so everything works functionally the same. All these files were simply adapted to fit the final assignment passes.



\## Build



```bash

make

```



This builds `build/interConstPropPass.so` and `intraConstPropPass.so`.



\## Run all tests



```bash

make tests

```





### How to run the llvm IPSCCP Pass





Run the command \[clang -fno-discard-value-names -S -emit-llvm -O0 -Sclang -disable-O0-optnone "INSERT FILE NAME".c -o "INSERT OUTPUT NAME".bc]



Using the output file from before, run \[opt -passes='mem2reg' -S "INSERT PREVIOUS FILE".bc -o "INSERT PREVIOUS FILE"-m2r.ll]



Finally, using the m2r file, run \[opt -passes='ipsccp' -S "INSERT PREVIOUS FILE"-m2r.ll -o "INSERT PREVIOUS FILE"-ipsccp.ll]



You can now view the ll file to dissect how the pass optimized the code

