
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
