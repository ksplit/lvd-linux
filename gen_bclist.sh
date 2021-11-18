
find $(pwd) -name "*.ko" > modules_ko.txt

while read line; do extract-bc ${line};done < modules_ko.txt 



find $(pwd) -name "*.ko.bc" > driver_bc.list

find $(pwd) -name "*.o.bc" > kernel_bc.list
