# Amlogic Crash-Tools Changelog


## v1.0 (2025-03-25)
### init repo for crash static
- crash8.0.6+, gdb-16.2, static, strip, support --vmap

## v1.1 (2025-06-12)
### fix ubuntu22 float error
- build on ubuntu22 (glibc v2.35), fix for glibc float error:
	Fatal signal: Floating point exception
	----- Backtrace -----
	0x51c4f6 ???
	0x6433f7 ???
	0xcff09f ???
	0x7efe0e428d49 __nptl_tls_static_size_for_stack
			../nptl/nptl-stack.h:58
	0x7efe0e428d49 __pthread_early_init
			../sysdeps/nptl/pthread_early_init.h:46
	0x7efe0e428d49 __libc_early_init
			./elf/libc_early_init.c:44
	0xd964a6 ???
	0xd8dd58 ???
	0xd956ce ???
	0xd8dd58 ???
	0xd95a71 ???
	0x43e4a7 ???
	。。。。。。
	0xce7217 ???
	0xce93df ???
	0x44b544 ???
	---------------------
	A fatal error internal to GDB has been detected, further
	debugging is not possible.  GDB will now terminate.
	
	This is a bug, please report it.



