
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

# sys_rune_next: every UTF-8 decode path
picofuse_test(sys_031
	sys_031/main.c
)

# sys_rune_count and sys_rune_valid, including the U+FFFD-vs-decode-error
# ambiguity that sys_rune_valid has to resolve correctly
picofuse_test(sys_032
	sys_032/main.c
)

# sys_rune_is_digit/is_space/is_alpha/is_upper/is_lower/is_punct/is_control
# classifiers - every ASCII boundary plus non-ASCII/error inputs
picofuse_test(sys_033
	sys_033/main.c
)

# sys_rune_to_upper/to_lower case conversion, ASCII + Latin-1 round trips
# and the no-single-codepoint-mapping cases (sharp s, y diaeresis)
picofuse_test(sys_034
	sys_034/main.c
)

# sys_rune_is_symbol, plus an exhaustive partition check that every
# printable ASCII char is exactly one of digit/alpha/punct/symbol
picofuse_test(sys_035
	sys_035/main.c
)

# sys_rune_isa classification dispatch, including the documented
# space-before-control priority for '\t' and NEL (codepoints where the
# two predicates overlap)
picofuse_test(sys_036
	sys_036/main.c
)

# sys_rune_tokenize_init/tokenize_next: maximal same-class runs, byte vs.
# rune counts, malformed UTF-8 grouping, and full-string contiguous
# coverage
picofuse_test(sys_037
	sys_037/main.c
)

# sys_string_bytes and sys_string_runes
picofuse_test(sys_038
	sys_038/main.c
)

# sys_string_compare 
picofuse_test(sys_039
	sys_039/main.c
)

# sys_string_to_upper/to_lower 
picofuse_test(sys_040
	sys_040/main.c
)

# sys_string_trimspace 
picofuse_test(sys_041
	sys_041/main.c
)

# sys_string_hasprefix / sys_string_hassuffix
picofuse_test(sys_042
	sys_042/main.c
)

# sys_string_contains 
picofuse_test(sys_043
	sys_043/main.c
)

# sys_string_trimprefix / sys_string_trimsuffix
picofuse_test(sys_044
	sys_044/main.c
)

# sys_scanner_init/scanner_next 
picofuse_test(sys_045
	sys_045/main.c
)

# sys_scanner_escapes 
picofuse_test(sys_046
	sys_046/main.c
)

# sys_string_read / sys_iostream_* 
picofuse_test(sys_047
	sys_047/main.c
)

# sys_scanner_newlines 
picofuse_test(sys_048
	sys_048/main.c
)

# sys_scanner_keywords 
picofuse_test(sys_049
	sys_049/main.c
)

# sys_scanner_quotes_single 
picofuse_test(sys_050
	sys_050/main.c
)

# sys_scanner_quotes_double 
picofuse_test(sys_051
	sys_051/main.c
)

# sys_scanner_comments_hash 
picofuse_test(sys_052
	sys_052/main.c
)

# sys_scanner_comments_slash 
picofuse_test(sys_053
	sys_053/main.c
)

# sys_scanner_numbers 
picofuse_test(sys_054
	sys_054/main.c
)

# sys_scanner_numbers_octal 
picofuse_test(sys_055
	sys_055/main.c
)

# sys_scanner_numbers_binary 
picofuse_test(sys_056
	sys_056/main.c
)

# sys_scanner_numbers_hex 
picofuse_test(sys_057
	sys_057/main.c
)

# sys_scanner_numbers_float 
picofuse_test(sys_058
	sys_058/main.c
)

# sys_scanner_punct/_symbol/_newline/_control are always exactly one
# rune per token
picofuse_test(sys_059
	sys_059/main.c
)

# sys_string_parse_escape 
picofuse_test(sys_060
	sys_060/main.c
)

# sys_string_parse_bool 
picofuse_test(sys_061
	sys_061/main.c
)

# sys_string_parse_quoted 
picofuse_test(sys_062
	sys_062/main.c
)

# sys_string_parse_int32 
picofuse_test(sys_063
	sys_063/main.c
)

# sys_string_parse_int64
picofuse_test(sys_064
	sys_064/main.c
)

# sys_string_parse_uint32 
picofuse_test(sys_065
	sys_065/main.c
)

# sys_string_parse_uint64 
picofuse_test(sys_066
	sys_066/main.c
)

# sys_string_parse_float32 
picofuse_test(sys_067
	sys_067/main.c
)

# sys_string_parse_float64 
picofuse_test(sys_068
	sys_068/main.c
)

# sys_hash_djb2 
picofuse_test(sys_069
	sys_069/main.c
)

# sys_hash_* (MD5/SHA-256 lifecycle) (openssl on darwin/linux, mbedtls on pico);
get_property(_picofuse_hash_backend GLOBAL PROPERTY PICOFUSE_HASH_BACKEND)
if(_picofuse_hash_backend STREQUAL "openssl" OR _picofuse_hash_backend STREQUAL "mbedtls")
	picofuse_test(sys_070
		sys_070/main.c
	)
endif()

# sys_date_get_time_utc/_local and sys_date_get_date_utc/_local
picofuse_test(sys_071
	sys_071/main.c
)

# sys_date_set_time_utc/_set_date_utc and sys_date_compare_ns
picofuse_test(sys_072
	sys_072/main.c
)

# sys_date_get_now/_set_now
picofuse_test(sys_073
	sys_073/main.c
)

# sys_env_signalhandler
picofuse_test(sys_074
	sys_074/main.c
)

# sys_env_serial/_name/_system/_version 
picofuse_test(sys_075
	sys_075/main.c
)

# sys_env_arg_parse/_arg_parse_*/_arg_usage 
if(NOT DEFINED PICO_BOARD)
	# sys_env_arg_parse
	picofuse_test(sys_076
		sys_076/main.c
	)

	# sys_env_arg_parse_bool/_int32/_uint32/_int64/_uint64/_float32/
	# _float64/_string
	picofuse_test(sys_077
		sys_077/main.c
	)
endif()

# sys_string_open
picofuse_test(sys_078
	sys_078/main.c
)

if(NOT DEFINED PICO_BOARD)
	# sys_env_arg_usage
	picofuse_test(sys_079
		sys_079/main.c
	)
endif()

# sys_timer_init/_start/_deinit lifecycle
picofuse_test(sys_080
	sys_080/main.c
)

# sys_timer_* periodic firing and sys_timer_userdata.
picofuse_test(sys_081
	sys_081/main.c
)

# sys_timer_* one-shot timer (self-deinit from within the callback).
picofuse_test(sys_082
	sys_082/main.c
)

# sys_timer_deinit blocking on an in-flight callback, and overlapping
# fires being serialized/skipped rather than run concurrently. Host-only
# for now: on Pico this starts its timer from core 1 (to test cross-core
# callback affinity per timer.h's platform note), but a timer armed on
# core 1's lazily-created alarm pool never actually fires there - open
# investigation, see project notes. Once fixed, drop this guard.
if(NOT DEFINED PICO_BOARD)
	picofuse_test(sys_083
		sys_083/main.c
	)
endif()

# sys_mem_arena_init argument validation: size/malloc_fn/free_fn rules for
# both the first arena in a chain and appended ones.
picofuse_test(sys_084
	sys_084/main.c
)

# sys_mem_arena_alloc/_free basics: alignment, distinct regions, stats.
picofuse_test(sys_085
	sys_085/main.c
)

# Gap reuse after freeing a middle allocation (first-fit, address order).
picofuse_test(sys_086
	sys_086/main.c
)

# Arena exhaustion and recovery once space is freed.
picofuse_test(sys_087
	sys_087/main.c
)

# sys_mem_arena_realloc
picofuse_test(sys_088
	sys_088/main.c
)

# sys_mem_arena_next traversal
picofuse_test(sys_089
	sys_089/main.c
)

# sys_mem_arena_delete
picofuse_test(sys_090
	sys_090/main.c
)

# Concurrent sys_mem_arena_alloc/_free from multiple threads/cores
picofuse_test(sys_091
	sys_091/main.c
)

# Concurrent sys_malloc/_calloc/_realloc/_free from multiple threads/cores
picofuse_test(sys_092
	sys_092/main.c
)

# sys_event_queue_init and sys_event_queue_deinit
picofuse_test(sys_093
	sys_093/main.c
)

# sys_event_queue_push/_try_push/_pop/_try_pop 
picofuse_test(sys_094
	sys_094/main.c
)

# sys_event_queue_push, try_push() fails 
picofuse_test(sys_095
	sys_095/main.c
)

# sys_event_queue_peek() alongside sys_event_queue_lock/_unlock(), and
# sys_event_queue_size/_empty() across a push/pop lifecycle.
picofuse_test(sys_096
	sys_096/main.c
)

# sys_event_queue_timed_pop(): the timeout_ms==0 alias, timing out with no
# event, and waking early from a push on another thread/core.
picofuse_test(sys_097
	sys_097/main.c
)

# sys_event_queue_shutdown(): wakes a consumer blocked in pop(), fails all
# future pushes, and drains already-queued events before pop() starts
# returning NULL.
picofuse_test(sys_098
	sys_098/main.c
)

# NULL-safety across the whole event queue API: a NULL queue never crashes
# and always reports a safe value, and NULL is rejected as an event.
picofuse_test(sys_099
	sys_099/main.c
)

# Concurrent producers/consumer against one small event queue - exercises
# try_push()'s full path and pop()'s blocking/wakeup path under real
# contention, and verifies no event is ever lost, duplicated, or corrupted.
picofuse_test(sys_100
	sys_100/main.c
)

# sys_runloop_run() entry-argument validation (NULL queue/event_fn) and a
# basic single-worker round trip through sys_runloop_shutdown()'s exit value.
picofuse_test(sys_101
	sys_101/main.c
)

# Calling sys_runloop_run() again while already running fails immediately
# (0) instead of recursing, and doesn't corrupt the singleton for later runs.
picofuse_test(sys_102
	sys_102/main.c
)

# init_fn/exit_fn hooks: called exactly once per worker that actually
# started (including worker 0), regardless of how work happened to be
# split between them.
picofuse_test(sys_103
	sys_103/main.c
)

# sys_runloop_post(): fails before the loop is running and after it stops,
# succeeds (from another thread/core) while running, and preserves FIFO
# order alongside events pre-seeded directly on the queue.
picofuse_test(sys_104
	sys_104/main.c
)

# sys_runloop_shutdown(): a safe no-op when nothing is running, wakes an
# idle loop with nothing queued, and drains already-pending events before
# the loop actually stops.
picofuse_test(sys_105
	sys_105/main.c
)

# poll_fn fires on a steady cadence while worker 0 is otherwise idle, with
# no events ever posted.
picofuse_test(sys_106
	sys_106/main.c
)

# Concurrent producers posting via sys_runloop_post() against a single
# small queue while the run loop consumes - verifies no event is ever
# lost, duplicated, or corrupted.
picofuse_test(sys_107
	sys_107/main.c
)

# hw_gpio_* basic lifecycle
if(NOT APPLE)
	picofuse_test(hw_001
		hw_001/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_gpio_count/_init argument validation
picofuse_test(hw_002
	hw_002/main.c
	LIBRARIES picofuse-hw
)

# hw_adc_* basic lifecycle
if(DEFINED PICO_BOARD)
	picofuse_test(hw_003
		hw_003/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_adc_read_dma() round-robin capture
if(DEFINED PICO_BOARD)
	picofuse_test(hw_004
		hw_004/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_adc_read_dma() driven from both cores at once
if(DEFINED PICO_BOARD)
	picofuse_test(hw_005
		hw_005/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_i2c_* basic lifecycle
if(DEFINED PICO_BOARD)
	picofuse_test(hw_006
		hw_006/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_i2c_detect() swept across every 7-bit address
if(DEFINED PICO_BOARD)
	picofuse_test(hw_007
		hw_007/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_spi_* basic lifecycle
if(DEFINED PICO_BOARD)
	picofuse_test(hw_008
		hw_008/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_pwm_* wrap-callback timing: measured interval between wraps tracks the
# configured period. Pico-only - no other backend supports wrap callbacks.
if(DEFINED PICO_BOARD)
	picofuse_test(hw_009
		hw_009/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_pwm_* configuration/control lifecycle and argument validation
if(DEFINED PICO_BOARD)
	picofuse_test(hw_010
		hw_010/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_led_* against the board's default on-board LED (GPIO/PWM/Wi-Fi/
# NeoPixel, whichever it resolves to) - exits cleanly if none is available,
# see hw_011/main.c.
if(DEFINED PICO_BOARD)
	picofuse_test(hw_011
		hw_011/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_led_init_gpio() against a single GPIO pin - no LED needs to be
# attached, see hw_012/main.c. Skips on Darwin (stub GPIO backend).
if(NOT APPLE)
	picofuse_test(hw_012
		hw_012/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_led_init_pwm() against a single PWM channel - no LED needs to be
# attached, see hw_013/main.c. Skips on Darwin (no PWM backend) and on
# Linux (hw_pwm_init() is a stub there - only hw_pwm_init_device() is
# real).
if(NOT APPLE)
	picofuse_test(hw_013
		hw_013/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_led_init_neopixel() against a small in-memory chain - no physical
# NeoPixels need to be attached, see hw_014/main.c. PIO is Pico-only.
if(DEFINED PICO_BOARD)
	picofuse_test(hw_014
		hw_014/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_led_init_device("PWR") against a real Linux LED-class entry
picofuse_test(hw_015
	hw_015/main.c
	LIBRARIES picofuse-hw
)

# hw_wifi_init_client()/hw_wifi_deinit() lifecycle plus a real
# hw_wifi_scan()
if(DEFINED PICO_BOARD)
	picofuse_test(hw_016
		hw_016/main.c
		LIBRARIES picofuse-hw
		TESTRUNNER_TIMEOUT 30
	)
endif()

# hw_wifi_connect()/hw_wifi_disconnect() against a real network - skips if
# WIFI_SSID isn't set, see hw_017/main.c.
if(DEFINED PICO_BOARD)
	picofuse_test(hw_017
		hw_017/main.c
		LIBRARIES picofuse-hw
		TESTRUNNER_TIMEOUT 60
	)

	# Wi-Fi credentials from environment (export WIFI_SSID and WIFI_PASSWORD)
	set(WIFI_SSID "$ENV{WIFI_SSID}")
	set(WIFI_PASSWORD "$ENV{WIFI_PASSWORD}")

	# Add Wi-Fi credentials as compile definitions
	target_compile_definitions(hw_017 PRIVATE
		WIFI_SSID="${WIFI_SSID}"
		WIFI_PASSWORD="${WIFI_PASSWORD}"
	)
endif()

# hw_wifi_init_accesspoint()/hw_wifi_deinit() lifecycle, plus confirming
# scan/connect/disconnect are rejected on an AP handle, see hw_018/main.c.
if(DEFINED PICO_BOARD)
	picofuse_test(hw_018
		hw_018/main.c
		LIBRARIES picofuse-hw
	)
endif()

# hw_uart_init()/flush() lifecycle plus sys_iostream_read/write/peek/
# set_callback/close() against a real UART instance - see hw_019/main.c
if(DEFINED PICO_BOARD)
	picofuse_test(hw_019
		hw_019/main.c
		LIBRARIES picofuse-hw
	)
	target_link_libraries(hw_019 PRIVATE hardware_uart_headers)
endif()

# hw_uart_init_device() lifecycle plus a real write/flush/callback smoke
# test against a real serial device - skips if HW_UART_TEST_DEVICE isn't
# set, see hw_020/main.c. Darwin/Linux only - Pico has no device-path UART.
if(NOT DEFINED PICO_BOARD)
	picofuse_test(hw_020
		hw_020/main.c
		LIBRARIES picofuse-hw
	)

	set(HW_UART_TEST_DEVICE "$ENV{HW_UART_TEST_DEVICE}")
	target_compile_definitions(hw_020 PRIVATE
		HW_UART_TEST_DEVICE="${HW_UART_TEST_DEVICE}"
	)
endif()

# dev_bme680_* against a real sensor, detected over I2C (both addresses)
# or SPI - see the detection order/reasoning in dev_bme680/main.c.
if(DEFINED PICO_BOARD)
	picofuse_test(dev_bme680
		dev_bme680/main.c
		LIBRARIES picofuse-hw picofuse-dev
	)
endif()
