#!/bin/bash
# Set the variables below according to your Buildroot configuration
BRPATH=../BR_i_qemu/buildroot-2019.08.2/
CROSS_ARCH=arm64 #BR2_ARCH
CROSS_PREFIX=aarch64-linux-gnu- #BR2_TOOLCHAIN_EXTERNAL_PREFIX
KERNEL_SRC=$BRPATH/output/build/linux-4.19.16
(
  PATH=$BRPATH/output/host/usr/bin:$PATH
  echo $KPATH
  echo $PWD
  make ARCH=$CROSS_ARCH CROSS_COMPILE=$CROSS_PREFIX -C $KERNEL_SRC \
     modules M=$PWD
  make ARCH=$CROSS_ARCH CROSS_COMPILE=$CROSS_PREFIX demo
  #make BRDTS=$BRDTS enc1-board.dtb
)



