#!/bin/bash

sst_dump=$2
wait_for_no_more_deletes=$3
wait_for_no_more_rows=$4
zenfs_util=$5
zenfs_device=$6
num_retries=240
retry=0

echo "wait_for_delete: $wait_for_no_more_deletes"

while : ; do
  TOTAL_D=0
  TOTAL_E=0
  if [ $zenfs_util != "" ]
  then
    rm -rf $1
    mkdir -p $1/mysqld.1/data
    $zenfs_util backup --zbd=$zenfs_device --path=$1/mysqld.1/data --backup_path=./ --force > /dev/null
  fi
  for f in `ls $1/mysqld.1/data/.rocksdb/*.sst`
  do
    # excluding system cf
    DELETED=`$sst_dump --command=scan --output_hex --file=$f | \
      perl -ne 'print  if(/''(\d\d\d\d\d\d\d\d)/ && $1 >= 11)' | \
      grep -e ", type:0" -e ", type:7" | wc -l`
    EXISTS=`$sst_dump --command=scan --output_hex --file=$f | \
      perl -ne 'print  if(/''(\d\d\d\d\d\d\d\d)/ && $1 >= 11)' | \
      grep ", type:1" | wc -l`
    TOTAL_D=$(($TOTAL_D+$DELETED))
    TOTAL_E=$(($TOTAL_E+$EXISTS))
    # echo "${f##*/} $DELETED $EXISTS"
  done
  if [ $TOTAL_E != "0" ] || ([ $wait_for_no_more_rows = "1" ] && [ $TOTAL_E = "0" ])
  then
    if [ $TOTAL_D = "0" ] || [ $wait_for_no_more_deletes = "0" ]
    then
      break
    fi
  fi
  if [ $retry -ge $num_retries ]
  then
    break
  fi
  sleep 1
  retry=$(($retry + 1))
done

if [ $zenfs_util != "" ]
then
  rm -rf $1
fi

if [ "$TOTAL_E" = "0" ]
then
  echo "No records in the database"
  exit
fi

if [ "$TOTAL_D" = "0" ]
then
  echo "No more deletes left"
else
  echo "There are deletes left"
fi
