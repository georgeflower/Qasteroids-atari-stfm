#!/bin/sh
# Fail if the compiler emitted a word/long memory access at an odd displacement
# (an address error on a real 68000; the Linux-targeted gcc assumes the 68020
# can do unaligned access). Only plain base+displacement forms are checked:
# indexed forms and large displacements are nearly always jump tables being
# disassembled as if they were code.
bad=$(m68k-linux-gnu-objdump -d "$1" |
      grep -E "(move|clr|tst|add|sub|cmp|and|or|neg|not)[wl] .*%(sp|a[0-6])@\(-?[0-9]{1,3}[13579]\)")
if [ -n "$bad" ]; then echo "ODD-ALIGNMENT ACCESS in $1:"; echo "$bad"; exit 1; fi
