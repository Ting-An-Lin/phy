cmd_syslib.o = icc -Wp,-MD,./.syslib.o.d.tmp  -m64 -pthread -I/home/obi/DPDK/dpdk-19.11/lib/librte_eal/linux/eal/include  -march=native -DRTE_MACHINE_CPUFLAG_SSE -DRTE_MACHINE_CPUFLAG_SSE2 -DRTE_MACHINE_CPUFLAG_SSE3 -DRTE_MACHINE_CPUFLAG_SSSE3 -DRTE_MACHINE_CPUFLAG_SSE4_1 -DRTE_MACHINE_CPUFLAG_SSE4_2 -DRTE_MACHINE_CPUFLAG_AES -DRTE_MACHINE_CPUFLAG_PCLMULQDQ -DRTE_MACHINE_CPUFLAG_AVX -DRTE_MACHINE_CPUFLAG_RDRAND -DRTE_MACHINE_CPUFLAG_RDSEED -DRTE_MACHINE_CPUFLAG_F16C -DRTE_MACHINE_CPUFLAG_AVX2  -I/home/obi/odu-low/phy/wls_lib/include -DRTE_USE_FUNCTION_VERSIONING -I/home/obi/DPDK/dpdk-19.11/x86_64-native-linuxapp-icc/include -include /home/obi/DPDK/dpdk-19.11/x86_64-native-linuxapp-icc/include/rte_config.h -D_GNU_SOURCE -Wall -fstack-protector -fPIC   -g -o syslib.o -c syslib.c 
