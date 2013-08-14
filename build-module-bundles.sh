#/bin/bash

KERNEL_VERSION=2.6.29-ts4700-00
INSTALL_MOD_PATH=/home/`whoami`/src/ts-4700/dist/linux-2.6.29-ts4700-00/modules-install
INITRD_PATH=${INSTALL_MOD_PATH}/initrd-modules
TRIM_PATH=${INITRD_PATH}/lib/modules/${KERNEL_VERSION}/kernel

rm -rf ${INSTALL_MOD_PATH}

if [ ! -d ${INITRD_PATH} ]; then mkdir -p ${INITRD_PATH}; fi

# Copy all modules.

INSTALL_MOD_PATH=${INSTALL_MOD_PATH} make modules_install
cp -r ${INSTALL_MOD_PATH}/lib ${INITRD_PATH}

# Remove all modules but those we need in the 'initrd'.

rm -rf ${TRIM_PATH}/{crypto,fs,lib,net}
rm -rf ${TRIM_PATH}/drivers/{block,i2c,input,misc,spi}

# Bundle up the reduced set necessary for the 'initrd'.

sudo chown -R root: ${INITRD_PATH}/lib
tar czf ${INITRD_PATH}/modules.tar.gz -C ${INITRD_PATH} lib
sudo chown -R `whoami`: ${INITRD_PATH}/lib

# Bundle up the full set of modules, for the Debian image.

sudo chown -R root: ${INSTALL_MOD_PATH}/lib
tar czf ${INSTALL_MOD_PATH}/modules-${KERNEL_VERSION}.tgz -C ${INSTALL_MOD_PATH} lib
sudo chown -R `whoami`: ${INSTALL_MOD_PATH}/lib
