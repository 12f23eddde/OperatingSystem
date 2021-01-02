#!/bin/bash
# [12f23eddde] Make life easier for CLion users
if [[ ! -e build_modified_nachos.sh ]];then
    cd ../../../Lab/Lab0_BuildNachos || exit
fi
bash build_modified_nachos.sh
docker run -it nachos gdb --args nachos/nachos-3.4/code/filesys/nachos "$@"