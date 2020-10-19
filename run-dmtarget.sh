#! /bin/bash

DM=dm-invert
LOOPD=/dev/loop0
DISK=/tmp/mydisk

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
  dd if=/dev/zero of=${DISK} bs=4k count=1 # 4k file
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

test1() {
  # write simply
  echo -n ${FUNCNAME}
  res=`cmp zero.f ${DISK}`
  if [ $? == 0 ]; then
    OK
  else
    NG
  fi
}

test2() {
  # initialized status
  echo -n ${FUNCNAME}
  res=`sudo dmsetup status ${DM}`
  exp=`echo '0 1 invert 7:0 read raw data'`
  if [ "${res}" = "${exp}" ]; then
    OK
  else
    NG
  fi
}

test3() {
  # change status
  echo -n ${FUNCNAME}
  sudo dmsetup message ${DM} 0 enable
  res=`sudo dmsetup status ${DM}`
  exp=`echo '0 1 invert 7:0 read correctly'`
  if [ "${res}" = "${exp}" ]; then
    OK
  else
    NG
  fi
}

while getopts ':tsdfrxh' option;
do
  case "$option" in
    t)
      # simple test
      setup
      test1
      test2
      test3
      teardown
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
