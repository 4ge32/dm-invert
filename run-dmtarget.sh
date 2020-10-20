#! /bin/bash

DM=dm-invert
LOOPD=/dev/loop0
DISK=/tmp/mydisk
REF=

usage () {
  echo "usage: `basename $0` [-sdfrh]"
  echo "      -t            test dm-invert"
  echo "      -f            test using filesystem"
  echo "      -s            setup device mapper target"
  echo "      -d            test using dd"
  echo "      -r            remove device mapper target"
  echo "      -x            show dm status"
  echo "      -h            display help"
}

if ( ! getopts ":sdfrh" option )
then
  usage
  exit 1
fi

zzz() {
  echo "${LOOP}"
}

setup() {
  dd if=/dev/zero of=${DISK} bs=512 count=1 # 4k file
  sudo losetup ${LOOPD} ${DISK} # losetup -f
  make
  sudo insmod ./${DM}.ko
  # echo <starting logical sector number> <logical size of device in terms of sector> <target name> <device path> <unsed paramter> | dmsetup create <mapper  name>
  sudo dmsetup create ${DM} --table "0 1 invert ${LOOPD} 0 512"
}

OK () {
  echo -e "\t [OK]"
}

NG() {
  echo -e "\t [FAILED]"
}

teardown() {
  if [ -d /mnt/mapper ]
  then
    umount /mnt/mapper
    rm -rf /mnt/mapper
  fi
  sudo dmsetup remove ${DM}
  sudo losetup -d ${LOOPD}
  sudo rmmod ${DM}
  rm ${DISK}
}

setup_fs() {
  if [ ! -d /mnt/mapper ]
  then
    mkdir -p /mnt/mapper
  fi
  modprobe ext4
  mkfs.ext4 -q /dev/mapper/my_device_mapper
  mount /dev/mapper/my_device_mapper /mnt/mapper
  cd /mnt/mapper
  touch test.txt
  cp test.txt copy.txt
  ls
}

show_dm() {
  sudo dmsetup status ${DM}
}

prelude() {
  printf "%16s" ${1}
}

test1() {
  # read simply
  prelude ${FUNCNAME}
  echo > /dev/null
  if [ $? == 0 ]; then
    OK
  else
    NG
  fi
}

test2() {
  # initialized status
  prelude ${FUNCNAME}
  res=`sudo dmsetup status ${DM}`
  exp=`echo '0 1 invert 7:0 read raw data'`
  if [ "${res}" = "${exp}" ]; then
    OK
  else
    NG
  fi
}

test_change_status() {
  # change status
  prelude ${FUNCNAME}
  sudo dmsetup message ${DM} 0 enable
  res=`sudo dmsetup status ${DM}`
  exp=`echo '0 1 invert 7:0 read correctly'`
  # cleanup
  sudo dmsetup message ${DM} 0 disable
  if [ "${res}" = "${exp}" ]; then
    OK
  else
    NG
  fi
}

test_writeZero_readOne() {
  # write simply
  prelude ${FUNCNAME}
  # write zero
  sudo dd if=/dev/zero of=/dev/mapper/dm-invert bs=512 count=1 > /dev/null 2>&1
  # read disk, and save it.
  sudo dd if=/dev/mapper/dm-invert of=tmp bs=512 count=1 > /dev/null 2>&1
  # compare with one files
  res=`cmp ${REF}simple_write.f tmp`
  sudo rm tmp
  if [ $? == 0 ]; then
    OK
  else
    NG
  fi
}

test_readable() {
  # write and enable to readable option, read it.
  prelude ${FUNCNAME}
  # write zero
  sudo dd if=/dev/zero of=/dev/mapper/dm-invert bs=512 count=1 > /dev/null 2>&1
  # enable readable
  sudo dmsetup message ${DM} 0 enable
  # read disk, and save it.
  sudo dd if=/dev/mapper/dm-invert of=tmp bs=512 count=1 > /dev/null 2>&1
  # compare with zero files
  res=`cmp ${REF}simple_readable.f tmp`
  # cleanup
  sudo dmsetup message ${DM} 0 disable
  sudo rm tmp
  if [ $? == 0 ]; then
    OK
  else
    NG
  fi
}

while getopts ':tksdfrxh' option;
do
  case "$option" in
    t)
      # simple test
      setup
      test1
      test2
      test_change_status
      test_writeZero_readOne
      test_readable
      teardown
      ;;
    k)
      setup
      test_readable
      ;;
    s)
      setup
      ;;
    d)
      sudo dd if=/dev/zero of=/dev/mapper/dm-invert bs=512 count=1
      ;;
    f)
      setup_fs
      ;;
    r)
      teardown
      ;;
    x)
      show_dm
      ;;
    h)
      usage
      exit
      ;;
    \?)
      printf "illegal option: -%s\n" "$OPTARG" >&2
      usage
      exit 1
      ;;
  esac
done
shift $((OPTIND - 1))
