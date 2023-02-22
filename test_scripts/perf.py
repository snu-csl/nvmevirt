import os
import sys
import time

class fio_perf_tester()  :
    def __init__(self, fio_path, device) :
        self.fio = fio_path
        self.device = device
        self.log = None
        self.dir = time.strftime("%Y%m%d-%H%M%S")
        os.system('mkdir ' + self.dir)
    def __set_log(self, file_name) :
        self.log = file_name

    def __make_and_set_log_name(self, access_pattern, s_bs, e_bs, s_thread, e_thread, s_qd, e_qd, fsize) :
        file_name = self.dir + '/'
        file_name += access_pattern + "_"
        file_name += 'BS' + str(s_bs) + '-' + str(e_bs) + '_'
        file_name += 'T' + str(s_thread) + '-' + str(e_thread) + '_'
        file_name += 'QD' + str(s_qd) + '-' + str(e_qd) + '_'
        file_name += 'FS' + str(fsize)
        self.__set_log(file_name)

    def __setup_common_parameter(self, wl, qd, t, bs, fs):
        file_name = wl + str(t) + "t_" + str(qd) + "qd_" + bs + '_' + fs
        file_name += '.fio'
        
        f = open(file_name, 'w')

        f.write("[global]\n")
        f.write("filename=" + self.device + '\n')
        f.write('rw='+ wl +'\n')
        f.write('bs='+bs+'\n')
        f.write('size='+fs+ '\n')
        f.write('iodepth='+str(qd)+'\n')
        f.write('direct=1\n')
        f.write('group_reporting=1\n')

        if (qd == 1):
            f.write('ioengine=sync\n')
        else :
            f.write('ioengine=libaio\n')
        
        return f, file_name
    
    def __create_fio_seq_read(self, start_off, queue_depth, num_thread, block_size, file_size):
        f, file_name = self.__setup_common_parameter("read", queue_depth, num_thread, block_size, file_size)
        
        for i in range(num_thread) :
            f.write('[file' + str(i) +']'+ '\n')
            f.write("offset="+start_off+'+'+str(i)+'*'+file_size + '\n')

        return file_name
    
    def __create_fio_seq_write(self, start_off, queue_depth, num_thread, block_size, file_size):
     
        f, file_name = self.__setup_common_parameter("write", queue_depth, num_thread, block_size, file_size)

        for i in range(num_thread) :
            f.write('[file' + str(i) +']'+ '\n')
            f.write("offset="+start_off+'+'+str(i)+'*'+file_size + '\n')
        
        return file_name

    def __create_fio_rand_write(self, queue_depth, num_thread, block_size, file_size, time):       
        f, file_name = self.__setup_common_parameter("randwrite", queue_depth, num_thread, block_size, file_size)

        f.write("numjobs=" + str(num_thread)+'\n')

        f.write("time_based\n")  
        f.write("runtime=" + str(time) + '\n')

        f.write("[file]\n")
        f.write("offset=0 \n")
        return file_name

    def __create_fio_rand_read(self, queue_depth, num_thread, block_size, file_size, time):
        
        f, file_name = self.__setup_common_parameter("randread", queue_depth, num_thread, block_size, file_size)

        f.write("numjobs=" + str(num_thread)+'\n')

        f.write("time_based\n")  
        f.write("runtime=" + str(time) + '\n')

        f.write("[file]\n")
        f.write("offset=0 \n")
        return file_name
    
    def __run_fio(self, script):
        command = "sudo " + self.fio +  " " + script
        if self.log :
            command = command + " | tee -a " + self.log

        os.system(command)
        
    def seq_write(self, off, t, qd, bs, fs) :
        s = self.__create_fio_seq_write(start_off=off, queue_depth=qd, num_thread=t, block_size=bs, file_size=fs)
        self.__run_fio(script=s)

    def seq_read(self, off, t, qd, bs, fs) :
        s = self.__create_fio_seq_read(start_off=off, queue_depth=qd, num_thread=t, block_size=bs, file_size=fs)
        self.__run_fio(script=s)

    def rand_read(self, t, qd, bs, fs, time) :
        s = self.__create_fio_rand_read(queue_depth=qd, num_thread=t, block_size=bs, file_size=fs, time=time)
        self.__run_fio(script=s)
    
    def rand_write(self, t, qd, bs, fs, time) :
        s = self.__create_fio_rand_write(queue_depth=qd, num_thread=t, block_size=bs, file_size=fs, time=time)
        self.__run_fio(script=s)

    def test_rand_read_increasing_thread(self, qd, bs, fs, time, num_test) :
        self.__make_and_set_log_name("RR", s_bs=bs, e_bs=bs, s_thread=1, e_thread=num_test, s_qd=qd, e_qd=qd, fsize=fs)
        t = 1
        for i in range(num_test):
            self.rand_read(t = t, qd = qd, bs = bs, fs = fs, time = time)
            t = t * 2

    def test_rand_read_increasing_qd(self, t, bs, fs, time, num_test) :
        self.__make_and_set_log_name("RR", s_bs=bs, e_bs=bs, s_thread=t, e_thread=t, s_qd=1, e_qd=2**num_test, fsize=fs)
        qd = 1

        for i in range(num_test):
            self.rand_read(t = t, qd = qd, bs = bs, fs = fs, time = time)
            qd = qd * 2
        
    def test_seq_read_increasing_thread(self, qd, bs, fs, num_test) :
        self.__make_and_set_log_name("SR", s_bs=bs, e_bs=bs, s_thread=1, e_thread=2**num_test, s_qd=qd, e_qd=qd, fsize=fs)
        script = []
        t = 1
        for i in range(num_test):
            self.seq_write(queue_depth=qd, num_thread=t, block_size=bs, file_size=fs)
            t = t * 2
            
    def test_rand_read_qd1_increasing_bs(self, fs, time, num_test) :
        self.__make_and_set_log_name("RR", s_bs='4K', e_bs=2**num_test, s_thread=1, e_thread=1, s_qd=1, e_qd=1, fsize=fs)
        bs = 4096
        for i in range(num_test):
            self.rand_read(t = 1, qd = 1, bs = str(bs), fs = fs, time = time)
            bs = bs * 2
    
    def test_seq_write_increasing_thread(self, qd, bs, fs, num_test) :
        self.__make_and_set_log_name("SW", s_bs=bs, e_bs=bs, s_thread=1, e_thread=2**num_test, s_qd=qd, e_qd=qd, fsize=fs)
        t = 1
        for i in range(num_test):
            self.seq_write(off='0M', t = t, qd = qd, bs = bs, fs = fs)
            t = t * 2

    def test_seq_write_increasing_bs(self, qd, t, fs, num_test) :
        self.__make_and_set_log_name("SW", s_bs=1, e_bs=2**num_test, s_thread=t, e_thread=t, s_qd=qd, e_qd=qd, fsize=fs)
        bs = 4096
        for i in range(num_test):
            self.seq_write(off='0M', t = t, qd = qd, bs = str(bs), fs = fs)
            bs = bs * 2
    
    def test_rand_write_increasing_bs(self, qd, t, fs, time, num_test) :
        self.__make_and_set_log_name("RW", s_bs=1, e_bs=2**num_test, s_thread=t, e_thread=t, s_qd=qd, e_qd=qd, fsize=fs)
        bs = 4096
        for i in range(num_test):
            self.rand_write(t = t, qd = qd, bs = str(bs), fs = fs, time = time)
            bs = bs * 2

if __name__ == "__main__" :
    targ_device = '/dev/nvme5n1'
    tester = fio_perf_tester('fio', targ_device)
    
    fs='27G'
    num_test=8
    t = 6
    #tester.seq_read(off="0M",t=1, qd=32, bs='8K', fs='1M')
    #tester.seq_write(off='0M', t=1, qd=2, bs='256k', fs=fs)

    tester.rand_write(t=1,qd=1,bs='256K', fs=fs, time=10)
    tester.rand_write(t=1,qd=1,bs='64K', fs=fs, time=10)
    #tester.test_seq_write_increasing_bs(qd=1, t=1, fs=fs, num_test=num_test)
    fs='3G'
    ##tester.test_rand_read_qd1_increasing_bs(fs=fs, time=t, num_test=9)
    """
    tester.test_rand_read_increasing_thread(qd=1, bs='4K', fs=fs, time=t, num_test=8)
    tester.test_rand_read_increasing_thread(qd=1, bs='8K', fs=fs, time=t, num_test=num_test)
    tester.test_rand_read_increasing_thread(qd=1, bs='16K', fs=fs, time=t, num_test=num_test)
    tester.test_rand_read_increasing_thread(qd=1, bs='32K', fs=fs, time=t, num_test=num_test)
    tester.test_rand_read_increasing_thread(qd=1, bs='64K', fs=fs, time=t, num_test=num_test) 
    tester.test_rand_read_increasing_thread(qd=1, bs='128K', fs=fs, time=t, num_test=num_test)   
    tester.test_rand_read_increasing_thread(qd=1, bs='256K', fs=fs, time=t, num_test=num_test)
    #tester.test_rand_read_increasing_thread(qd=1, bs='4K', fs=fs, time=t, num_test=num_test)
    """
    #tester.test_rand_read_increasing_thread(qd=1, bs='128K', fs=fs, time=t, num_test=num_tes    t)

    

