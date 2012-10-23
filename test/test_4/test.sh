./run_routed.sh 1
./run_routed.sh 2
./run_routed.sh 3
./run_routed.sh 4
./run_routed.sh 5

sleep 10

nc localhost 15000 < req > tmp.res
diff -s tmp.res res
rm -f tmp.res

killall routed

