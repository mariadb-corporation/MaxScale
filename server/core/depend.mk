atomic.o: atomic.c
buffer.o: buffer.c /usr/include/stdlib.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h \
 /usr/include/endian.h /usr/include/bits/endian.h \
 /usr/include/bits/byteswap.h /usr/include/sys/types.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/time.h /usr/include/sys/select.h /usr/include/bits/select.h \
 /usr/include/bits/sigset.h /usr/include/bits/time.h \
 /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h \
 /usr/include/alloca.h ../include/buffer.h ../include/atomic.h
spinlock.o: spinlock.c ../include/spinlock.h ../include/thread.h \
 /usr/include/pthread.h /usr/include/features.h /usr/include/sys/cdefs.h \
 /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h \
 /usr/include/gnu/stubs-64.h /usr/include/endian.h \
 /usr/include/bits/endian.h /usr/include/bits/byteswap.h \
 /usr/include/sched.h /usr/include/bits/types.h \
 /usr/include/bits/typesizes.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/time.h /usr/include/bits/sched.h /usr/include/bits/time.h \
 /usr/include/xlocale.h /usr/include/bits/pthreadtypes.h \
 /usr/include/bits/setjmp.h ../include/atomic.h
gateway.o: gateway.c ../include/gw.h /usr/include/stdio.h \
 /usr/include/features.h /usr/include/sys/cdefs.h \
 /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h \
 /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/ctype.h /usr/include/endian.h /usr/include/bits/endian.h \
 /usr/include/bits/byteswap.h /usr/include/xlocale.h /usr/include/errno.h \
 /usr/include/bits/errno.h /usr/include/linux/errno.h \
 /usr/include/asm/errno.h /usr/include/asm-generic/errno.h \
 /usr/include/asm-generic/errno-base.h /usr/include/netdb.h \
 /usr/include/netinet/in.h /usr/include/stdint.h \
 /usr/include/bits/wchar.h /usr/include/sys/socket.h \
 /usr/include/sys/uio.h /usr/include/sys/types.h /usr/include/time.h \
 /usr/include/sys/select.h /usr/include/bits/select.h \
 /usr/include/bits/sigset.h /usr/include/bits/time.h \
 /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h \
 /usr/include/bits/uio.h /usr/include/bits/socket.h \
 /usr/include/bits/sockaddr.h /usr/include/asm/socket.h \
 /usr/include/asm-generic/socket.h /usr/include/asm/sockios.h \
 /usr/include/asm-generic/sockios.h /usr/include/bits/in.h \
 /usr/include/rpc/netdb.h /usr/include/bits/netdb.h /usr/include/fcntl.h \
 /usr/include/bits/fcntl.h /usr/include/bits/stat.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h /usr/include/syslog.h \
 /usr/include/sys/syslog.h /usr/include/bits/syslog-path.h \
 /usr/include/string.h /usr/include/stdlib.h \
 /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h \
 /usr/include/alloca.h /usr/include/pwd.h /usr/include/sys/epoll.h \
 /usr/include/signal.h /usr/include/bits/signum.h \
 /usr/include/bits/siginfo.h /usr/include/bits/sigaction.h \
 /usr/include/bits/sigcontext.h /usr/include/bits/sigstack.h \
 /usr/include/sys/ucontext.h /usr/include/bits/sigthread.h \
 /usr/include/sys/ioctl.h /usr/include/bits/ioctls.h \
 /usr/include/asm/ioctls.h /usr/include/asm-generic/ioctls.h \
 /usr/include/linux/ioctl.h /usr/include/asm/ioctl.h \
 /usr/include/asm-generic/ioctl.h /usr/include/bits/ioctl-types.h \
 /usr/include/sys/ttydefaults.h /usr/include/arpa/inet.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdbool.h \
 ../include/gateway_mysql.h ../include/dcb.h ../include/spinlock.h \
 ../include/thread.h /usr/include/pthread.h /usr/include/sched.h \
 /usr/include/bits/sched.h /usr/include/bits/setjmp.h ../include/buffer.h \
 ../include/gwbitmask.h ../include/mysql_protocol.h ../include/dcb.h \
 ../include/service.h ../include/server.h ../include/session.h \
 ../include/modules.h ../include/config.h ../include/poll.h \
 /home/skysql/mariadb-5.5.32/include/mysql.h \
 /home/skysql/mariadb-5.5.32/include/mysql_version.h \
 /home/skysql/mariadb-5.5.32/include/mysql_com.h \
 /home/skysql/mariadb-5.5.32/include/mysql_time.h \
 /home/skysql/mariadb-5.5.32/include/my_list.h \
 /home/skysql/mariadb-5.5.32/include/typelib.h \
 /home/skysql/mariadb-5.5.32/include/my_alloc.h ../include/monitor.h \
 /usr/include/sys/stat.h /home/skysql/workspace/utils/skygw_utils.h \
 /home/skysql/workspace/utils/skygw_types.h /usr/include/math.h \
 /usr/include/bits/huge_val.h /usr/include/bits/huge_valf.h \
 /usr/include/bits/huge_vall.h /usr/include/bits/inf.h \
 /usr/include/bits/nan.h /usr/include/bits/mathdef.h \
 /usr/include/bits/mathcalls.h /home/skysql/workspace/utils/skygw_debug.h \
 /usr/include/assert.h /home/skysql/workspace/log_manager/log_manager.h
gateway_mysql_protocol.o: gateway_mysql_protocol.c ../include/gw.h \
 /usr/include/stdio.h /usr/include/features.h /usr/include/sys/cdefs.h \
 /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h \
 /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/ctype.h /usr/include/endian.h /usr/include/bits/endian.h \
 /usr/include/bits/byteswap.h /usr/include/xlocale.h /usr/include/errno.h \
 /usr/include/bits/errno.h /usr/include/linux/errno.h \
 /usr/include/asm/errno.h /usr/include/asm-generic/errno.h \
 /usr/include/asm-generic/errno-base.h /usr/include/netdb.h \
 /usr/include/netinet/in.h /usr/include/stdint.h \
 /usr/include/bits/wchar.h /usr/include/sys/socket.h \
 /usr/include/sys/uio.h /usr/include/sys/types.h /usr/include/time.h \
 /usr/include/sys/select.h /usr/include/bits/select.h \
 /usr/include/bits/sigset.h /usr/include/bits/time.h \
 /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h \
 /usr/include/bits/uio.h /usr/include/bits/socket.h \
 /usr/include/bits/sockaddr.h /usr/include/asm/socket.h \
 /usr/include/asm-generic/socket.h /usr/include/asm/sockios.h \
 /usr/include/asm-generic/sockios.h /usr/include/bits/in.h \
 /usr/include/rpc/netdb.h /usr/include/bits/netdb.h /usr/include/fcntl.h \
 /usr/include/bits/fcntl.h /usr/include/bits/stat.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h /usr/include/syslog.h \
 /usr/include/sys/syslog.h /usr/include/bits/syslog-path.h \
 /usr/include/string.h /usr/include/stdlib.h \
 /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h \
 /usr/include/alloca.h /usr/include/pwd.h /usr/include/sys/epoll.h \
 /usr/include/signal.h /usr/include/bits/signum.h \
 /usr/include/bits/siginfo.h /usr/include/bits/sigaction.h \
 /usr/include/bits/sigcontext.h /usr/include/bits/sigstack.h \
 /usr/include/sys/ucontext.h /usr/include/bits/sigthread.h \
 /usr/include/sys/ioctl.h /usr/include/bits/ioctls.h \
 /usr/include/asm/ioctls.h /usr/include/asm-generic/ioctls.h \
 /usr/include/linux/ioctl.h /usr/include/asm/ioctl.h \
 /usr/include/asm-generic/ioctl.h /usr/include/bits/ioctl-types.h \
 /usr/include/sys/ttydefaults.h /usr/include/arpa/inet.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdbool.h \
 ../include/gateway_mysql.h ../include/dcb.h ../include/spinlock.h \
 ../include/thread.h /usr/include/pthread.h /usr/include/sched.h \
 /usr/include/bits/sched.h /usr/include/bits/setjmp.h ../include/buffer.h \
 ../include/gwbitmask.h ../include/mysql_protocol.h ../include/dcb.h \
 ../include/session.h /usr/include/openssl/sha.h \
 /usr/include/openssl/e_os2.h /usr/include/openssl/opensslconf.h \
 /usr/include/openssl/opensslconf-x86_64.h
gw_utils.o: gw_utils.c ../include/gw.h /usr/include/stdio.h \
 /usr/include/features.h /usr/include/sys/cdefs.h \
 /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h \
 /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/ctype.h /usr/include/endian.h /usr/include/bits/endian.h \
 /usr/include/bits/byteswap.h /usr/include/xlocale.h /usr/include/errno.h \
 /usr/include/bits/errno.h /usr/include/linux/errno.h \
 /usr/include/asm/errno.h /usr/include/asm-generic/errno.h \
 /usr/include/asm-generic/errno-base.h /usr/include/netdb.h \
 /usr/include/netinet/in.h /usr/include/stdint.h \
 /usr/include/bits/wchar.h /usr/include/sys/socket.h \
 /usr/include/sys/uio.h /usr/include/sys/types.h /usr/include/time.h \
 /usr/include/sys/select.h /usr/include/bits/select.h \
 /usr/include/bits/sigset.h /usr/include/bits/time.h \
 /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h \
 /usr/include/bits/uio.h /usr/include/bits/socket.h \
 /usr/include/bits/sockaddr.h /usr/include/asm/socket.h \
 /usr/include/asm-generic/socket.h /usr/include/asm/sockios.h \
 /usr/include/asm-generic/sockios.h /usr/include/bits/in.h \
 /usr/include/rpc/netdb.h /usr/include/bits/netdb.h /usr/include/fcntl.h \
 /usr/include/bits/fcntl.h /usr/include/bits/stat.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h /usr/include/syslog.h \
 /usr/include/sys/syslog.h /usr/include/bits/syslog-path.h \
 /usr/include/string.h /usr/include/stdlib.h \
 /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h \
 /usr/include/alloca.h /usr/include/pwd.h /usr/include/sys/epoll.h \
 /usr/include/signal.h /usr/include/bits/signum.h \
 /usr/include/bits/siginfo.h /usr/include/bits/sigaction.h \
 /usr/include/bits/sigcontext.h /usr/include/bits/sigstack.h \
 /usr/include/sys/ucontext.h /usr/include/bits/sigthread.h \
 /usr/include/sys/ioctl.h /usr/include/bits/ioctls.h \
 /usr/include/asm/ioctls.h /usr/include/asm-generic/ioctls.h \
 /usr/include/linux/ioctl.h /usr/include/asm/ioctl.h \
 /usr/include/asm-generic/ioctl.h /usr/include/bits/ioctl-types.h \
 /usr/include/sys/ttydefaults.h /usr/include/arpa/inet.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdbool.h \
 ../include/gateway_mysql.h ../include/dcb.h ../include/spinlock.h \
 ../include/thread.h /usr/include/pthread.h /usr/include/sched.h \
 /usr/include/bits/sched.h /usr/include/bits/setjmp.h ../include/buffer.h \
 ../include/gwbitmask.h ../include/mysql_protocol.h ../include/dcb.h \
 ../include/session.h
utils.o: utils.c ../include/gw.h /usr/include/stdio.h \
 /usr/include/features.h /usr/include/sys/cdefs.h \
 /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h \
 /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/ctype.h /usr/include/endian.h /usr/include/bits/endian.h \
 /usr/include/bits/byteswap.h /usr/include/xlocale.h /usr/include/errno.h \
 /usr/include/bits/errno.h /usr/include/linux/errno.h \
 /usr/include/asm/errno.h /usr/include/asm-generic/errno.h \
 /usr/include/asm-generic/errno-base.h /usr/include/netdb.h \
 /usr/include/netinet/in.h /usr/include/stdint.h \
 /usr/include/bits/wchar.h /usr/include/sys/socket.h \
 /usr/include/sys/uio.h /usr/include/sys/types.h /usr/include/time.h \
 /usr/include/sys/select.h /usr/include/bits/select.h \
 /usr/include/bits/sigset.h /usr/include/bits/time.h \
 /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h \
 /usr/include/bits/uio.h /usr/include/bits/socket.h \
 /usr/include/bits/sockaddr.h /usr/include/asm/socket.h \
 /usr/include/asm-generic/socket.h /usr/include/asm/sockios.h \
 /usr/include/asm-generic/sockios.h /usr/include/bits/in.h \
 /usr/include/rpc/netdb.h /usr/include/bits/netdb.h /usr/include/fcntl.h \
 /usr/include/bits/fcntl.h /usr/include/bits/stat.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h /usr/include/syslog.h \
 /usr/include/sys/syslog.h /usr/include/bits/syslog-path.h \
 /usr/include/string.h /usr/include/stdlib.h \
 /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h \
 /usr/include/alloca.h /usr/include/pwd.h /usr/include/sys/epoll.h \
 /usr/include/signal.h /usr/include/bits/signum.h \
 /usr/include/bits/siginfo.h /usr/include/bits/sigaction.h \
 /usr/include/bits/sigcontext.h /usr/include/bits/sigstack.h \
 /usr/include/sys/ucontext.h /usr/include/bits/sigthread.h \
 /usr/include/sys/ioctl.h /usr/include/bits/ioctls.h \
 /usr/include/asm/ioctls.h /usr/include/asm-generic/ioctls.h \
 /usr/include/linux/ioctl.h /usr/include/asm/ioctl.h \
 /usr/include/asm-generic/ioctl.h /usr/include/bits/ioctl-types.h \
 /usr/include/sys/ttydefaults.h /usr/include/arpa/inet.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdbool.h \
 ../include/gateway_mysql.h ../include/dcb.h ../include/spinlock.h \
 ../include/thread.h /usr/include/pthread.h /usr/include/sched.h \
 /usr/include/bits/sched.h /usr/include/bits/setjmp.h ../include/buffer.h \
 ../include/gwbitmask.h ../include/mysql_protocol.h ../include/dcb.h \
 ../include/session.h ../include/mysql_protocol.h \
 /usr/include/openssl/sha.h /usr/include/openssl/e_os2.h \
 /usr/include/openssl/opensslconf.h \
 /usr/include/openssl/opensslconf-x86_64.h ../include/poll.h \
 /home/skysql/workspace/utils/skygw_utils.h \
 /home/skysql/workspace/utils/skygw_types.h /usr/include/math.h \
 /usr/include/bits/huge_val.h /usr/include/bits/huge_valf.h \
 /usr/include/bits/huge_vall.h /usr/include/bits/inf.h \
 /usr/include/bits/nan.h /usr/include/bits/mathdef.h \
 /usr/include/bits/mathcalls.h /home/skysql/workspace/utils/skygw_debug.h \
 /usr/include/assert.h /home/skysql/workspace/log_manager/log_manager.h
dcb.o: dcb.c /usr/include/stdio.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/stdlib.h /usr/include/bits/waitflags.h \
 /usr/include/bits/waitstatus.h /usr/include/endian.h \
 /usr/include/bits/endian.h /usr/include/bits/byteswap.h \
 /usr/include/sys/types.h /usr/include/time.h /usr/include/sys/select.h \
 /usr/include/bits/select.h /usr/include/bits/sigset.h \
 /usr/include/bits/time.h /usr/include/sys/sysmacros.h \
 /usr/include/bits/pthreadtypes.h /usr/include/alloca.h \
 /usr/include/string.h /usr/include/xlocale.h ../include/dcb.h \
 ../include/spinlock.h ../include/thread.h /usr/include/pthread.h \
 /usr/include/sched.h /usr/include/bits/sched.h \
 /usr/include/bits/setjmp.h ../include/buffer.h ../include/gwbitmask.h \
 ../include/server.h ../include/session.h ../include/service.h \
 ../include/modules.h ../include/router.h /usr/include/errno.h \
 /usr/include/bits/errno.h /usr/include/linux/errno.h \
 /usr/include/asm/errno.h /usr/include/asm-generic/errno.h \
 /usr/include/asm-generic/errno-base.h ../include/gw.h \
 /usr/include/ctype.h /usr/include/netdb.h /usr/include/netinet/in.h \
 /usr/include/stdint.h /usr/include/bits/wchar.h \
 /usr/include/sys/socket.h /usr/include/sys/uio.h /usr/include/bits/uio.h \
 /usr/include/bits/socket.h /usr/include/bits/sockaddr.h \
 /usr/include/asm/socket.h /usr/include/asm-generic/socket.h \
 /usr/include/asm/sockios.h /usr/include/asm-generic/sockios.h \
 /usr/include/bits/in.h /usr/include/rpc/netdb.h \
 /usr/include/bits/netdb.h /usr/include/fcntl.h /usr/include/bits/fcntl.h \
 /usr/include/bits/stat.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h /usr/include/syslog.h \
 /usr/include/sys/syslog.h /usr/include/bits/syslog-path.h \
 /usr/include/pwd.h /usr/include/sys/epoll.h /usr/include/signal.h \
 /usr/include/bits/signum.h /usr/include/bits/siginfo.h \
 /usr/include/bits/sigaction.h /usr/include/bits/sigcontext.h \
 /usr/include/bits/sigstack.h /usr/include/sys/ucontext.h \
 /usr/include/bits/sigthread.h /usr/include/sys/ioctl.h \
 /usr/include/bits/ioctls.h /usr/include/asm/ioctls.h \
 /usr/include/asm-generic/ioctls.h /usr/include/linux/ioctl.h \
 /usr/include/asm/ioctl.h /usr/include/asm-generic/ioctl.h \
 /usr/include/bits/ioctl-types.h /usr/include/sys/ttydefaults.h \
 /usr/include/arpa/inet.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdbool.h \
 ../include/gateway_mysql.h ../include/mysql_protocol.h ../include/dcb.h \
 ../include/poll.h ../include/atomic.h \
 /home/skysql/workspace/utils/skygw_utils.h \
 /home/skysql/workspace/utils/skygw_types.h /usr/include/math.h \
 /usr/include/bits/huge_val.h /usr/include/bits/huge_valf.h \
 /usr/include/bits/huge_vall.h /usr/include/bits/inf.h \
 /usr/include/bits/nan.h /usr/include/bits/mathdef.h \
 /usr/include/bits/mathcalls.h /home/skysql/workspace/utils/skygw_debug.h \
 /usr/include/assert.h /home/skysql/workspace/log_manager/log_manager.h
load_utils.o: load_utils.c /usr/include/sys/param.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/limits.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/syslimits.h \
 /usr/include/limits.h /usr/include/features.h /usr/include/sys/cdefs.h \
 /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h \
 /usr/include/gnu/stubs-64.h /usr/include/bits/posix1_lim.h \
 /usr/include/bits/local_lim.h /usr/include/linux/limits.h \
 /usr/include/bits/posix2_lim.h /usr/include/linux/param.h \
 /usr/include/asm/param.h /usr/include/asm-generic/param.h \
 /usr/include/sys/types.h /usr/include/bits/types.h \
 /usr/include/bits/typesizes.h /usr/include/time.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/endian.h /usr/include/bits/endian.h \
 /usr/include/bits/byteswap.h /usr/include/sys/select.h \
 /usr/include/bits/select.h /usr/include/bits/sigset.h \
 /usr/include/bits/time.h /usr/include/sys/sysmacros.h \
 /usr/include/bits/pthreadtypes.h /usr/include/stdio.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/stdlib.h /usr/include/bits/waitflags.h \
 /usr/include/bits/waitstatus.h /usr/include/alloca.h \
 /usr/include/unistd.h /usr/include/bits/posix_opt.h \
 /usr/include/bits/environments.h /usr/include/bits/confname.h \
 /usr/include/getopt.h /usr/include/string.h /usr/include/xlocale.h \
 /usr/include/dlfcn.h /usr/include/bits/dlfcn.h ../include/modules.h \
 ../include/dcb.h ../include/spinlock.h ../include/thread.h \
 /usr/include/pthread.h /usr/include/sched.h /usr/include/bits/sched.h \
 /usr/include/bits/setjmp.h ../include/buffer.h ../include/gwbitmask.h \
 /home/skysql/workspace/utils/skygw_utils.h \
 /home/skysql/workspace/utils/skygw_types.h /usr/include/math.h \
 /usr/include/bits/huge_val.h /usr/include/bits/huge_valf.h \
 /usr/include/bits/huge_vall.h /usr/include/bits/inf.h \
 /usr/include/bits/nan.h /usr/include/bits/mathdef.h \
 /usr/include/bits/mathcalls.h /home/skysql/workspace/utils/skygw_debug.h \
 /usr/include/assert.h /home/skysql/workspace/log_manager/log_manager.h
session.o: session.c /usr/include/stdio.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/stdlib.h /usr/include/bits/waitflags.h \
 /usr/include/bits/waitstatus.h /usr/include/endian.h \
 /usr/include/bits/endian.h /usr/include/bits/byteswap.h \
 /usr/include/sys/types.h /usr/include/time.h /usr/include/sys/select.h \
 /usr/include/bits/select.h /usr/include/bits/sigset.h \
 /usr/include/bits/time.h /usr/include/sys/sysmacros.h \
 /usr/include/bits/pthreadtypes.h /usr/include/alloca.h \
 /usr/include/unistd.h /usr/include/bits/posix_opt.h \
 /usr/include/bits/environments.h /usr/include/bits/confname.h \
 /usr/include/getopt.h /usr/include/string.h /usr/include/xlocale.h \
 ../include/session.h ../include/service.h ../include/spinlock.h \
 ../include/thread.h /usr/include/pthread.h /usr/include/sched.h \
 /usr/include/bits/sched.h /usr/include/bits/setjmp.h ../include/dcb.h \
 ../include/buffer.h ../include/gwbitmask.h ../include/server.h \
 ../include/router.h ../include/atomic.h
service.o: service.c /usr/include/stdio.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/stdlib.h /usr/include/bits/waitflags.h \
 /usr/include/bits/waitstatus.h /usr/include/endian.h \
 /usr/include/bits/endian.h /usr/include/bits/byteswap.h \
 /usr/include/sys/types.h /usr/include/time.h /usr/include/sys/select.h \
 /usr/include/bits/select.h /usr/include/bits/sigset.h \
 /usr/include/bits/time.h /usr/include/sys/sysmacros.h \
 /usr/include/bits/pthreadtypes.h /usr/include/alloca.h \
 /usr/include/string.h /usr/include/xlocale.h ../include/session.h \
 ../include/service.h ../include/spinlock.h ../include/thread.h \
 /usr/include/pthread.h /usr/include/sched.h /usr/include/bits/sched.h \
 /usr/include/bits/setjmp.h ../include/dcb.h ../include/buffer.h \
 ../include/gwbitmask.h ../include/server.h ../include/router.h \
 ../include/modules.h ../include/users.h ../include/hashtable.h \
 ../include/atomic.h ../include/dbusers.h ../include/poll.h \
 /home/skysql/workspace/utils/skygw_utils.h \
 /home/skysql/workspace/utils/skygw_types.h /usr/include/math.h \
 /usr/include/bits/huge_val.h /usr/include/bits/huge_valf.h \
 /usr/include/bits/huge_vall.h /usr/include/bits/inf.h \
 /usr/include/bits/nan.h /usr/include/bits/mathdef.h \
 /usr/include/bits/mathcalls.h /home/skysql/workspace/utils/skygw_debug.h \
 /usr/include/assert.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h \
 /home/skysql/workspace/log_manager/log_manager.h
server.o: server.c /usr/include/stdio.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/stdlib.h /usr/include/bits/waitflags.h \
 /usr/include/bits/waitstatus.h /usr/include/endian.h \
 /usr/include/bits/endian.h /usr/include/bits/byteswap.h \
 /usr/include/sys/types.h /usr/include/time.h /usr/include/sys/select.h \
 /usr/include/bits/select.h /usr/include/bits/sigset.h \
 /usr/include/bits/time.h /usr/include/sys/sysmacros.h \
 /usr/include/bits/pthreadtypes.h /usr/include/alloca.h \
 /usr/include/string.h /usr/include/xlocale.h ../include/session.h \
 ../include/server.h ../include/dcb.h ../include/spinlock.h \
 ../include/thread.h /usr/include/pthread.h /usr/include/sched.h \
 /usr/include/bits/sched.h /usr/include/bits/setjmp.h ../include/buffer.h \
 ../include/gwbitmask.h /home/skysql/workspace/utils/skygw_utils.h \
 /home/skysql/workspace/utils/skygw_types.h /usr/include/math.h \
 /usr/include/bits/huge_val.h /usr/include/bits/huge_valf.h \
 /usr/include/bits/huge_vall.h /usr/include/bits/inf.h \
 /usr/include/bits/nan.h /usr/include/bits/mathdef.h \
 /usr/include/bits/mathcalls.h /home/skysql/workspace/utils/skygw_debug.h \
 /usr/include/assert.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h \
 /home/skysql/workspace/log_manager/log_manager.h
poll.o: poll.c /usr/include/stdio.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/string.h /usr/include/xlocale.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h /usr/include/stdlib.h \
 /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h \
 /usr/include/endian.h /usr/include/bits/endian.h \
 /usr/include/bits/byteswap.h /usr/include/sys/types.h \
 /usr/include/time.h /usr/include/sys/select.h /usr/include/bits/select.h \
 /usr/include/bits/sigset.h /usr/include/bits/time.h \
 /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h \
 /usr/include/alloca.h /usr/include/sys/epoll.h /usr/include/stdint.h \
 /usr/include/bits/wchar.h ../include/poll.h ../include/dcb.h \
 ../include/spinlock.h ../include/thread.h /usr/include/pthread.h \
 /usr/include/sched.h /usr/include/bits/sched.h \
 /usr/include/bits/setjmp.h ../include/buffer.h ../include/gwbitmask.h \
 ../include/atomic.h
config.o: config.c /usr/include/stdio.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/string.h /usr/include/xlocale.h /usr/include/stdlib.h \
 /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h \
 /usr/include/endian.h /usr/include/bits/endian.h \
 /usr/include/bits/byteswap.h /usr/include/sys/types.h \
 /usr/include/time.h /usr/include/sys/select.h /usr/include/bits/select.h \
 /usr/include/bits/sigset.h /usr/include/bits/time.h \
 /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h \
 /usr/include/alloca.h ../inih/ini.h ../include/config.h \
 ../include/service.h ../include/spinlock.h ../include/thread.h \
 /usr/include/pthread.h /usr/include/sched.h /usr/include/bits/sched.h \
 /usr/include/bits/setjmp.h ../include/dcb.h ../include/buffer.h \
 ../include/gwbitmask.h ../include/server.h ../include/users.h \
 ../include/hashtable.h ../include/atomic.h ../include/monitor.h \
 /home/skysql/workspace/utils/skygw_utils.h \
 /home/skysql/workspace/utils/skygw_types.h /usr/include/math.h \
 /usr/include/bits/huge_val.h /usr/include/bits/huge_valf.h \
 /usr/include/bits/huge_vall.h /usr/include/bits/inf.h \
 /usr/include/bits/nan.h /usr/include/bits/mathdef.h \
 /usr/include/bits/mathcalls.h /home/skysql/workspace/utils/skygw_debug.h \
 /usr/include/assert.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h \
 /home/skysql/workspace/log_manager/log_manager.h
users.o: users.c /usr/include/stdio.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/stdlib.h /usr/include/bits/waitflags.h \
 /usr/include/bits/waitstatus.h /usr/include/endian.h \
 /usr/include/bits/endian.h /usr/include/bits/byteswap.h \
 /usr/include/sys/types.h /usr/include/time.h /usr/include/sys/select.h \
 /usr/include/bits/select.h /usr/include/bits/sigset.h \
 /usr/include/bits/time.h /usr/include/sys/sysmacros.h \
 /usr/include/bits/pthreadtypes.h /usr/include/alloca.h \
 /usr/include/string.h /usr/include/xlocale.h ../include/users.h \
 ../include/hashtable.h ../include/spinlock.h ../include/thread.h \
 /usr/include/pthread.h /usr/include/sched.h /usr/include/bits/sched.h \
 /usr/include/bits/setjmp.h ../include/atomic.h ../include/dcb.h \
 ../include/buffer.h ../include/gwbitmask.h
hashtable.o: hashtable.c /usr/include/stdio.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/stdlib.h /usr/include/bits/waitflags.h \
 /usr/include/bits/waitstatus.h /usr/include/endian.h \
 /usr/include/bits/endian.h /usr/include/bits/byteswap.h \
 /usr/include/sys/types.h /usr/include/time.h /usr/include/sys/select.h \
 /usr/include/bits/select.h /usr/include/bits/sigset.h \
 /usr/include/bits/time.h /usr/include/sys/sysmacros.h \
 /usr/include/bits/pthreadtypes.h /usr/include/alloca.h \
 /usr/include/string.h /usr/include/xlocale.h ../include/hashtable.h \
 ../include/spinlock.h ../include/thread.h /usr/include/pthread.h \
 /usr/include/sched.h /usr/include/bits/sched.h \
 /usr/include/bits/setjmp.h ../include/atomic.h ../include/dcb.h \
 ../include/buffer.h ../include/gwbitmask.h
dbusers.o: dbusers.c /usr/include/stdio.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /home/skysql/mariadb-5.5.32/include/mysql.h /usr/include/sys/types.h \
 /usr/include/time.h /usr/include/endian.h /usr/include/bits/endian.h \
 /usr/include/bits/byteswap.h /usr/include/sys/select.h \
 /usr/include/bits/select.h /usr/include/bits/sigset.h \
 /usr/include/bits/time.h /usr/include/sys/sysmacros.h \
 /usr/include/bits/pthreadtypes.h \
 /home/skysql/mariadb-5.5.32/include/mysql_version.h \
 /home/skysql/mariadb-5.5.32/include/mysql_com.h \
 /home/skysql/mariadb-5.5.32/include/mysql_time.h \
 /home/skysql/mariadb-5.5.32/include/my_list.h \
 /home/skysql/mariadb-5.5.32/include/typelib.h \
 /home/skysql/mariadb-5.5.32/include/my_alloc.h ../include/dcb.h \
 ../include/spinlock.h ../include/thread.h /usr/include/pthread.h \
 /usr/include/sched.h /usr/include/bits/sched.h /usr/include/xlocale.h \
 /usr/include/bits/setjmp.h ../include/buffer.h ../include/gwbitmask.h \
 ../include/service.h ../include/server.h ../include/users.h \
 ../include/hashtable.h ../include/atomic.h \
 /home/skysql/workspace/utils/skygw_utils.h \
 /home/skysql/workspace/utils/skygw_types.h /usr/include/math.h \
 /usr/include/bits/huge_val.h /usr/include/bits/huge_valf.h \
 /usr/include/bits/huge_vall.h /usr/include/bits/inf.h \
 /usr/include/bits/nan.h /usr/include/bits/mathdef.h \
 /usr/include/bits/mathcalls.h /home/skysql/workspace/utils/skygw_debug.h \
 /usr/include/assert.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h \
 /home/skysql/workspace/log_manager/log_manager.h ../include/secrets.h \
 /usr/include/string.h /usr/include/stdlib.h \
 /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h \
 /usr/include/alloca.h /usr/include/sys/stat.h /usr/include/bits/stat.h \
 /usr/include/fcntl.h /usr/include/bits/fcntl.h /usr/include/errno.h \
 /usr/include/bits/errno.h /usr/include/linux/errno.h \
 /usr/include/asm/errno.h /usr/include/asm-generic/errno.h \
 /usr/include/asm-generic/errno-base.h /usr/include/openssl/aes.h \
 /usr/include/openssl/opensslconf.h \
 /usr/include/openssl/opensslconf-x86_64.h
thread.o: thread.c ../include/thread.h /usr/include/pthread.h \
 /usr/include/features.h /usr/include/sys/cdefs.h \
 /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h \
 /usr/include/gnu/stubs-64.h /usr/include/endian.h \
 /usr/include/bits/endian.h /usr/include/bits/byteswap.h \
 /usr/include/sched.h /usr/include/bits/types.h \
 /usr/include/bits/typesizes.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/time.h /usr/include/bits/sched.h /usr/include/bits/time.h \
 /usr/include/xlocale.h /usr/include/bits/pthreadtypes.h \
 /usr/include/bits/setjmp.h
gwbitmask.o: gwbitmask.c /usr/include/stdlib.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h \
 /usr/include/endian.h /usr/include/bits/endian.h \
 /usr/include/bits/byteswap.h /usr/include/sys/types.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/time.h /usr/include/sys/select.h /usr/include/bits/select.h \
 /usr/include/bits/sigset.h /usr/include/bits/time.h \
 /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h \
 /usr/include/alloca.h /usr/include/string.h /usr/include/xlocale.h \
 ../include/gwbitmask.h ../include/spinlock.h ../include/thread.h \
 /usr/include/pthread.h /usr/include/sched.h /usr/include/bits/sched.h \
 /usr/include/bits/setjmp.h
monitor.o: monitor.c /usr/include/stdio.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/stdlib.h /usr/include/bits/waitflags.h \
 /usr/include/bits/waitstatus.h /usr/include/endian.h \
 /usr/include/bits/endian.h /usr/include/bits/byteswap.h \
 /usr/include/sys/types.h /usr/include/time.h /usr/include/sys/select.h \
 /usr/include/bits/select.h /usr/include/bits/sigset.h \
 /usr/include/bits/time.h /usr/include/sys/sysmacros.h \
 /usr/include/bits/pthreadtypes.h /usr/include/alloca.h \
 /usr/include/string.h /usr/include/xlocale.h ../include/monitor.h \
 ../include/server.h ../include/dcb.h ../include/spinlock.h \
 ../include/thread.h /usr/include/pthread.h /usr/include/sched.h \
 /usr/include/bits/sched.h /usr/include/bits/setjmp.h ../include/buffer.h \
 ../include/gwbitmask.h ../include/modules.h \
 /home/skysql/workspace/utils/skygw_utils.h \
 /home/skysql/workspace/utils/skygw_types.h /usr/include/math.h \
 /usr/include/bits/huge_val.h /usr/include/bits/huge_valf.h \
 /usr/include/bits/huge_vall.h /usr/include/bits/inf.h \
 /usr/include/bits/nan.h /usr/include/bits/mathdef.h \
 /usr/include/bits/mathcalls.h /home/skysql/workspace/utils/skygw_debug.h \
 /usr/include/assert.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h \
 /home/skysql/workspace/log_manager/log_manager.h
adminusers.o: adminusers.c /usr/include/stdio.h /usr/include/features.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/stdlib.h /usr/include/bits/waitflags.h \
 /usr/include/bits/waitstatus.h /usr/include/endian.h \
 /usr/include/bits/endian.h /usr/include/bits/byteswap.h \
 /usr/include/sys/types.h /usr/include/time.h /usr/include/sys/select.h \
 /usr/include/bits/select.h /usr/include/bits/sigset.h \
 /usr/include/bits/time.h /usr/include/sys/sysmacros.h \
 /usr/include/bits/pthreadtypes.h /usr/include/alloca.h \
 /usr/include/string.h /usr/include/xlocale.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h /usr/include/crypt.h \
 ../include/users.h ../include/hashtable.h ../include/spinlock.h \
 ../include/thread.h /usr/include/pthread.h /usr/include/sched.h \
 /usr/include/bits/sched.h /usr/include/bits/setjmp.h ../include/atomic.h \
 ../include/dcb.h ../include/buffer.h ../include/gwbitmask.h \
 ../include/adminusers.h /home/skysql/workspace/utils/skygw_utils.h \
 /home/skysql/workspace/utils/skygw_types.h /usr/include/math.h \
 /usr/include/bits/huge_val.h /usr/include/bits/huge_valf.h \
 /usr/include/bits/huge_vall.h /usr/include/bits/inf.h \
 /usr/include/bits/nan.h /usr/include/bits/mathdef.h \
 /usr/include/bits/mathcalls.h /home/skysql/workspace/utils/skygw_debug.h \
 /usr/include/assert.h /home/skysql/workspace/log_manager/log_manager.h
secrets.o: secrets.c ../include/secrets.h /usr/include/stdio.h \
 /usr/include/features.h /usr/include/sys/cdefs.h \
 /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h \
 /usr/include/gnu/stubs-64.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stddef.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/x86_64-redhat-linux/4.4.7/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/string.h /usr/include/xlocale.h /usr/include/stdlib.h \
 /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h \
 /usr/include/endian.h /usr/include/bits/endian.h \
 /usr/include/bits/byteswap.h /usr/include/sys/types.h \
 /usr/include/time.h /usr/include/sys/select.h /usr/include/bits/select.h \
 /usr/include/bits/sigset.h /usr/include/bits/time.h \
 /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h \
 /usr/include/alloca.h /usr/include/sys/stat.h /usr/include/bits/stat.h \
 /usr/include/fcntl.h /usr/include/bits/fcntl.h /usr/include/errno.h \
 /usr/include/bits/errno.h /usr/include/linux/errno.h \
 /usr/include/asm/errno.h /usr/include/asm-generic/errno.h \
 /usr/include/asm-generic/errno-base.h /usr/include/openssl/aes.h \
 /usr/include/openssl/opensslconf.h \
 /usr/include/openssl/opensslconf-x86_64.h \
 /home/skysql/workspace/utils/skygw_utils.h \
 /home/skysql/workspace/utils/skygw_types.h /usr/include/math.h \
 /usr/include/bits/huge_val.h /usr/include/bits/huge_valf.h \
 /usr/include/bits/huge_vall.h /usr/include/bits/inf.h \
 /usr/include/bits/nan.h /usr/include/bits/mathdef.h \
 /usr/include/bits/mathcalls.h /home/skysql/workspace/utils/skygw_debug.h \
 /usr/include/assert.h /usr/include/pthread.h /usr/include/sched.h \
 /usr/include/bits/sched.h /usr/include/bits/setjmp.h \
 /usr/include/unistd.h /usr/include/bits/posix_opt.h \
 /usr/include/bits/environments.h /usr/include/bits/confname.h \
 /usr/include/getopt.h /home/skysql/workspace/log_manager/log_manager.h \
 /usr/include/ctype.h
