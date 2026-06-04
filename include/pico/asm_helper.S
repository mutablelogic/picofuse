/* Minimal asm_helper.S stub for baremetal boot stage 2 builds.
   Provides only the macros used by boot2_*.S without pulling in
   pico/config_autogen.h (a generated file not present in the submodule). */

.macro pico_default_asm_setup
.syntax unified
.thumb
.endm

.macro regular_func x
.global \x
.type \x,%function
.thumb_func
\x:
.endm

.macro weak_func x
.weak \x
.type \x,%function
.thumb_func
\x:
.endm
