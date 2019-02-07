#!/bin/bash
read -p "Name of SQL file to create (in test/trace_processor): " sqlfile
read -p "Path to trace file: " tracefile

ABSPATH=$(readlink -f $0)
ABSDIR=$(dirname $ABSPATH)
ROOTDIR=$(dirname $ABSDIR)
TRACE_PROC_PATH=$ROOTDIR/test/trace_processor

SQL_FILE_NAME=${sqlfile%.*}

echo "Creating $TRACE_PROC_PATH/$sqlfile"
touch $TRACE_PROC_PATH/$sqlfile

RELTRACE=$(realpath -s $tracefile --relative-to=$TRACE_PROC_PATH --relative-base=$ROOTDIR)
TRACE_BASE=$(basename $RELTRACE)
TRACE_FILE_NAME=${TRACE_BASE%.*}
OUT_FILE="$SQL_FILE_NAME""_$TRACE_FILE_NAME.out"

echo "Creating $TRACE_PROC_PATH/$OUT_FILE"
touch $TRACE_PROC_PATH/$OUT_FILE

echo "Now add the following line to $ROOTDIR/test/tace_processor/index"
echo "$RELTRACE $sqlfile $OUT_FILE"
