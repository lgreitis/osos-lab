/* SPDX-License-Identifier: GPL-3.0-only */

/* Build-only records: kind, destination, source offset/count, size, input[32]. */
.macro native_record kind, input, destination, offset, size
    .long \kind, \destination, \offset, \size
.Lname\@:
    .asciz "\input"
    .space 32 - (. - .Lname\@)
.endm

.macro native_thumb_call input, offset, target, bytes:vararg
    native_expect \input, \offset, \bytes
    .pushsection .cfw.native,"",%progbits
    native_record 4, \input, \target, \offset, 4
    .popsection
.endm

.macro native_arm_jump input, offset, target
    .pushsection .cfw.native,"",%progbits
    native_record 5, \input, \target, \offset, 4
    .popsection
.endm

.macro native_ffs_checksum input, offset
    .pushsection .cfw.native,"",%progbits
    native_record 6, \input, 0, \offset, 1
    .popsection
.endm

.macro native_expect_fill input, offset, size, value
    .pushsection .cfw.native,"",%progbits
    native_record 3, \input, 0, \offset, \size
    .fill \size, 1, \value
    .balign 4
    .popsection
.endm

.macro native_module input, size, relocations
.Lmodule\@:
    .pushsection .cfw.native,"",%progbits
    native_record 1, \input, .Lmodule\@, \relocations, \size
    .popsection
.endm

.macro native_copy input, offset, size
.Lcopy\@:
    .pushsection .cfw.native,"",%progbits
    native_record 2, \input, .Lcopy\@, \offset, \size
    .popsection
    .space \size
.endm

.macro native_expect input, offset, bytes:vararg
    .pushsection .cfw.native,"",%progbits
    native_record 3, \input, 0, \offset, (.Lend\@ - .Lbytes\@)
.Lbytes\@:
    .byte \bytes
.Lend\@:
    .balign 4
    .popsection
.endm
