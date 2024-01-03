
## Build

* Use the predefined ksplit config
```
cp config-ksplit-sok .config
```

* Compile
```
make LLVM=1 LLVM_IAS=0 -j $(nproc)
```

* Once successfully compiled, you can extract the .bc files and create a list
  of bc files in a text file for further parsing.
```
./extract-bc.sh
```

* Use ksplit-boundary to parse and extract the kernel and driver bc files
```
./parse <driver_bc.list> <kernel_bc.list> <liblcd_funcs.txt>
```
