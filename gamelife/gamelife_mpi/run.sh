#!/bin/sh

for pr in 3 4 5 6 7 8 9 10 11
do
  	mpirun -np $pr ./ata 1000 150 400
done

