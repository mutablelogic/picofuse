
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

# sys_rune_next: every UTF-8 decode path, especially malformed sequences -
# overlong encodings, surrogates, out-of-range continuation bytes,
# truncation, invalid lead bytes, and error re-sync behaviour
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
# control-before-space priority for NEL (the one codepoint where the two
# predicates disagree)
picofuse_test(sys_036
	sys_036/main.c
)

# sys_rune_tokenize_init/tokenize_next: maximal same-class runs, byte vs.
# rune counts, malformed UTF-8 grouping, and full-string contiguous
# coverage
picofuse_test(sys_037
	sys_037/main.c
)

# sys_string_bytes and sys_string_runes (the latter cross-checked against
# sys_rune_count directly)
picofuse_test(sys_038
	sys_038/main.c
)

# sys_string_compare - strcmp-style three-way byte comparison, including
# unsigned-byte-ordering and UTF-8 byte/codepoint order equivalence
picofuse_test(sys_039
	sys_039/main.c
)

# sys_string_to_upper/to_lower - destructive in-place case conversion,
# including the Latin-1 2-byte-width-preserving rewrite and malformed
# bytes passing through untouched
picofuse_test(sys_040
	sys_040/main.c
)

# sys_string_trimspace - in-place leading/trailing whitespace trim,
# including multi-byte NBSP and internal-whitespace preservation
picofuse_test(sys_041
	sys_041/main.c
)

# sys_string_hasprefix / sys_string_hassuffix
picofuse_test(sys_042
	sys_042/main.c
)

# sys_string_contains - byte-offset substring search
picofuse_test(sys_043
	sys_043/main.c
)

# sys_string_trimprefix / sys_string_trimsuffix
picofuse_test(sys_044
	sys_044/main.c
)

# sys_scanner_init/scanner_next - flag-driven tokenization, the
# sys_scanner_nospaces flag skipping whitespace runs entirely
picofuse_test(sys_045
	sys_045/main.c
)

# sys_scanner_escapes - JSON-style \-escape sequences as single
# tokens, malformed-escape fallback, and the punctuation-run-absorption
# guard
picofuse_test(sys_046
	sys_046/main.c
)

# sys_string_read / sys_iostream_* - peek/read/write semantics, EOF and
# NULL handling, independent streams, and pool exhaustion/reuse
picofuse_test(sys_047
	sys_047/main.c
)

# sys_scanner_newlines - '\n' classified separately from ordinary
# whitespace, run-merging, and composition with nospace/escapes
picofuse_test(sys_048
	sys_048/main.c
)

# sys_scanner_keywords / sys_scanner_keywords_withunderscores /
# sys_scanner_keywords_withdashes - identifier tokens mixing letter/digit
# classes, the modifier flags including the base flag, and each
# modifier's character correctly gated behind its own flag
picofuse_test(sys_049
	sys_049/main.c
)

# sys_scanner_quotes_single - single-quoted string tokens, backslash escaping
# whatever follows it (not just \'), unterminated-at-EOF best-effort
# behavior, and independence from sys_scanner_escapes
picofuse_test(sys_050
	sys_050/main.c
)

# sys_scanner_quotes_double - same rules as sys_scanner_quotes_single but for "-quoted
# strings, plus both flags combined: each string only closes on its own
# matching quote character
picofuse_test(sys_051
	sys_051/main.c
)

# sys_scanner_comments_hash - '#' line comments running to (not
# including) the newline or end of stream, composition with
# sys_scanner_newlines/nospace, and the punctuation-absorption guard
picofuse_test(sys_052
	sys_052/main.c
)

# sys_scanner_comments_slash - '//' line comments, the single-'/'
# lookahead fallback (both at EOF and merging back into a punct run),
# and composition with sys_scanner_comments_hash/newlines/nospace
picofuse_test(sys_053
	sys_053/main.c
)

# sys_scanner_numbers - whole numbers optionally signed with '-'/'+',
# the lone-sign lookahead fallback, the punctuation-absorption guard,
# and composition with sys_scanner_nospaces/keywords
picofuse_test(sys_054
	sys_054/main.c
)

# sys_scanner_numbers_octal - both the "0o"/"0O" prefixed (Python/Rust/
# Swift) and bare leading-zero (C) octal forms, the '8'/'9'-isn't-octal
# fallback to an ordinary decimal number, and the octal-run-stops-cleanly
# split ("0779" -> "077" then "9")
picofuse_test(sys_055
	sys_055/main.c
)

# sys_scanner_numbers_binary - "0b"/"0B" prefixed binary digit runs and
# the no-digit-after-prefix fallback to the bare number "0"
picofuse_test(sys_056
	sys_056/main.c
)

# sys_scanner_numbers_hex - "0x"/"0X" prefixed hex digit runs, the
# no-digit-after-prefix fallback, and non-interference with an
# in-progress sys_scanner_keywords run
picofuse_test(sys_057
	sys_057/main.c
)

# sys_scanner_numbers_float - a '.' fractional suffix immediately
# followed by decimal digits, an "e"/"E"-with-optional-sign exponent
# suffix (with or without a fractional part first), the no-digit-after
# fallback for both, no leading-dot numbers, and only one fractional
# suffix per number
picofuse_test(sys_058
	sys_058/main.c
)
