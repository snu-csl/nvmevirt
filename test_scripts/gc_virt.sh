ssd="/dev/nvme5n1"
size="4K"
engine=libaio
time=60
#gb="512G"
gb="28G"
dir="./"
target=$1
postfix=""
fill_log_detail=$dir$target"fill"$postfix
fill_log_detail1=$dir$target"after_fill1"$postfix
fill_log_detail2=$dir$target"after_fill2"$postfix
run_log_detail=$dir$target"run"$postfix

fill_log_sum=$dir$target"fill_iops_summary"$postfix
run_log_sum=$dir$target"run_iops_summary"$postfix


#echo "format" $ssd
#sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches '
#sudo nvme format -s1 $ssd 
#sudo sync
#sleep 300

echo "start write" $ssd $size $gb
#sudo fio --filename=$ssd --time_based=1 --runtime=$time --direct=1 --rw=randwrite --ioengine=$engine --bs=$size --iodepth=64 --name=fio_direct_write_test --status-interval=1
#sudo fio --filename=$ssd --time_based=1 --runtime=$time --direct=1 --rw=randwrite --ioengine=$engine --bs=$size --iodepth=64 --name=fio_direct_write_test --norandommap --log_avg_msec=5000 --write_iops_log=blockKV
#sudo fio --filename=$ssd --time_based=1 --runtime=$time --direct=1 --rw=randwrite --ioengine=$engine --bs=$size --iodepth=64 --name=fio_direct_write_test --status-interval=1 --norandommap --gtod_reduce=1 --refill_buffers

##sudo fio --filename=$ssd --time_based=1 --runtime=$time --direct=1 --rw=randwrite --ioengine=$engine --bs=$size --iodepth=64 --name=fio_direct_write_test --norandommap --refill_buffers --log_avg_msec=5000 --write_iops_log=970-pro
#sudo fio --filename=$ssd --size=$gb --direct=1 --rw=randwrite --ioengine=$engine --bs=$size --iodepth=64 --name=fio_direct_write_test --norandommap --refill_buffers --log_avg_msec=5000 --write_iops_log=970-pro 2>&1 | tee gc_result

echo
echo "## Start fill phase ##"
sudo fio --filename=$ssd --direct=1 --rw=write --ioengine=$engine --bs=512K --iodepth=128 --numjobs=1 --group_reporting=1 --name=fio_direct_write_test --log_avg_msec=5000 --write_iops_log=$fill_log_detail 2>&1 #| tee $fill_log_sum

echo
echo "## Start run phase ##"
sudo fio --filename=$ssd --time_based=1 --size=$gb --runtime=$time --direct=1 --rw=randwrite --ioengine=$engine --bs=$size --iodepth=128 --numjobs=4 -name=fio_direct_write_test --randrepeat=0 --random_generator=tausworthe64 --random_distribution=random --log_avg_msec=5000 --group_reporting=1 --write_iops_log=$run_log_detail 2>&1 #| tee $run_log_sum
#sudo fio --filename=$ssd --time_based=1 --runtime=$time --direct=1 --rw=randwrite --ioengine=$engine --bs=$size --iodepth=1 --numjobs=64 --name=fio_direct_write_test --random_distribution=random --log_avg_msec=5000 --write_iops_log=970-pro-run 2>&1 | tee gc_result


sudo fio --filename=$ssd --direct=1 --rw=write --ioengine=$engine --bs=512K --iodepth=128 --numjobs=1 --group_reporting=1 --name=fio_direct_write_test --log_avg_msec=5000 --write_iops_log=$fill_log_detail 2>&1 #| tee $fill_log_sum


sudo fio --filename=$ssd --direct=1 --rw=write --ioengine=$engine --bs=512K --iodepth=128 --numjobs=1 --group_reporting=1 --name=fio_direct_write_test --log_avg_msec=5000 --write_iops_log=$fill_log_detail 2>&1 #| tee $fill_log_sum

echo "end write"
