
# Just print "hello world" to verify that the test framework is working.
picofuse_test(sys_001
	sys_001/main.c
)

# Attempt to grab a mutex from the pool.
picofuse_test(sys_002
	sys_002/main.c
)

# Exhaust the mutex pool, up to its compiled-in SYS_MUTEX_CAPACITY.
picofuse_test(sys_003
	sys_003/main.c
)

# A second thread must actually block on a held mutex until it's released.
picofuse_test(sys_004
	sys_004/main.c
)

# printf %c formatting
picofuse_test(sys_005
	sys_005/main.c
)

# printf %s formatting
picofuse_test(sys_006
	sys_006/main.c
)

# printf %d/%u/%x/%X/%b/%o formatting, 32- and 64-bit paths
picofuse_test(sys_007
	sys_007/main.c
)

# printf %f/%F/%e/%E/%g/%G formatting
picofuse_test(sys_008
	sys_008/main.c
)

# printf %x/%X/%o/%b formatting
picofuse_test(sys_009
	sys_009/main.c
)

# printf %p formatting
picofuse_test(sys_010
	sys_010/main.c
)

# printf custom format handler (sys_sprintf_ex)
picofuse_test(sys_011
	sys_011/main.c
)

# printf %% and a trailing lone '%' at end of format string
picofuse_test(sys_012
	sys_012/main.c
)

# sys_sleep_ms/sys_timestamp_ms
picofuse_test(sys_013
	sys_013/main.c
)

# sys_random_uint32/sys_random_uint64 bit-coverage smoke test
picofuse_test(sys_014
	sys_014/main.c
)

# sys_thread_numcores/sys_thread_create(_on_core) + sys_waitgroup_*
picofuse_test(sys_015
	sys_015/main.c
)

# sys_atomic_inc under real concurrent load from many threads/cores at once
picofuse_test(sys_016
	sys_016/main.c
)

# sys_atomic_set_bits/clear_bits with distinct bits set/cleared concurrently
picofuse_test(sys_017
	sys_017/main.c
)

# sys_mutex_lock/unlock under real concurrent contention from many threads
picofuse_test(sys_018
	sys_018/main.c
)

# Multiple concurrent waiters on the same sys_waitgroup_t, all released together
picofuse_test(sys_019
	sys_019/main.c
)

# sys_waitgroup_add/done edge cases, non-blocking wait, and pool exhaustion
picofuse_test(sys_020
	sys_020/main.c
)

# Reusing the same sys_waitgroup_t across multiple add/dispatch/wait rounds
picofuse_test(sys_021
	sys_021/main.c
)

# sys_mutex_trylock, including real cross-thread contention
picofuse_test(sys_022
	sys_022/main.c
)

# sys_atomic_inc/sys_atomic_dec return-value correctness, single- and
# multi-threaded
picofuse_test(sys_023
	sys_023/main.c
)

# Thread pool/capacity exhaustion: dispatch fails when full, succeeds again
# once a slot frees up
picofuse_test(sys_024
	sys_024/main.c
)

# sys_thread_create/create_on_core correctness per the documented contract
picofuse_test(sys_025
	sys_025/main.c
)

# Basic sys_cond_* producer/consumer handoff across real threads/cores
picofuse_test(sys_026
	sys_026/main.c
)

# sys_cond_broadcast wakes every waiter, and signal/broadcast with no
# waiters is a true no-op 
picofuse_test(sys_027
	sys_027/main.c
)

# sys_cond_signal/timedwait under a real waiter/signal-count imbalance
# (more waiters than signals)
picofuse_test(sys_028
	sys_028/main.c
)

# Remaining sys_cond_* code paths: bare wait(), timedwait signaled-quickly,
# timeout_ms=0, deinit's internal safety broadcast, pool exhaustion
picofuse_test(sys_029
	sys_029/main.c
)

# sys_waitgroup_deinit's safety net releases a genuinely-stranded waiter
picofuse_test(sys_030
	sys_030/main.c
)
