#!/bin/bash

dir=`pwd`
cd ${MDBCI_VM_PATH}/${name}
vagrant destroy -f
cd $dir

rm -rf 	${MDBCI_VM_PATH}/${name}
rm -rf 	${MDBCI_VM_PATH}/${name}.json
rm -rf 	${MDBCI_VM_PATH}/${name}_network_config
