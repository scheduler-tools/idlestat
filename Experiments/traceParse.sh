#!/bin/bash

TRACE=${1:-trace.dat}


for i in 0 1; do
  for c in 0 1 2 3 4; do
    echo -n "Idle$i, CPU$c hits: "
    grep cpu_idle $TRACE | grep state=0 | grep cpu_id=$c | wc -l
  done
done

for f in 350000 400000 500000 600000 700000 800000 900000 1000000; do
  for c in 1 2; do
    echo -n "Freq1, CPU$c hits: "
    grep cpu_frequency $TRACE | grep state=$f | grep cpu_id=$c | wc -l
  done
done

