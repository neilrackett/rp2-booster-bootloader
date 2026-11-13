; Firmware loader from cartridge
; (C) 2023-2025 by Diego Parrilla
; License: GPL v3

; Some technical info about the header format https://www.atari-forum.com/viewtopic.php?t=14086

; $FA0000 - CA_MAGIC. Magic number, always $abcdef42 for ROM cartridge. There is a special magic number for testing: $fa52235f.
; $FA0004 - CA_NEXT. Address of next program in cartridge, or 0 if no more.
; $FA0008 - CA_INIT. Address of optional init. routine. See below for details.
; $FA000C - CA_RUN. Address of program start. All optional inits are done before. This is required only if program runs under GEMDOS.
; $FA0010 - CA_TIME. File's time stamp. In GEMDOS format.
; $FA0012 - CA_DATE. File's date stamp. In GEMDOS format.
; $FA0014 - CA_SIZE. Lenght of app. in bytes. Not really used.
; $FA0018 - CA_NAME. DOS/TOS filename 8.3 format. Terminated with 0 .

; CA_INIT holds address of optional init. routine. Bits 24-31 aren't used for addressing, and ensure in which moment by system init prg. will be initialized and/or started. Bits have following meanings, 1 means execution:
; bit 24: Init. or start of cartridge SW after succesfull HW init. System variables and vectors are set, screen is set, Interrupts are disabled - level 7.
; bit 25: As by bit 24, but right after enabling interrupts on level 3. Before GEMDOS init.
; bit 26: System init is done until setting screen resolution. Otherwise as bit 24.
; bit 27: After GEMDOS init. Before booting from disks.
; bit 28: -
; bit 29: Program is desktop accessory - ACC .	 
; bit 30: TOS application .
; bit 31: TTP

ROM4_ADDR			equ $FA0000
FRAMEBUFFER_ADDR	equ $FA8000
FRAMEBUFFER_SIZE 	equ 8000	; 8Kbytes of a 320x200 monochrome screen
SCREEN_SIZE			equ (-4096)	; Use the memory before the screen memory to store the copied code
COLS_HIGH			equ 20		; 16 bit columns in the ST
ROWS_HIGH			equ 200		; 200 rows in the ST
BYTES_ROW_HIGH		equ 80		; 80 bytes per row in the ST
PRE_RESET_WAIT		equ $FFFFF
TRANSTABLE			equ $FA1000	; Translation table for high resolution

; XBIOS Getrez results. The screen is drawn in ST low or ST high only: any
; other resolution on a colour monitor is switched to ST low while the screen
; is shown, and back when the boot continues.
REZ_ST_LOW			equ 0
REZ_ST_HIGH			equ 2
REZ_FALCON			equ 3		; A Falcon mode that is not ST compatible (see below)
REZ_TT_HIGH			equ 6		; Mono monitor: it cannot show ST low

; A TT's internal speaker is switched off while the screen is shown, and put
; back as TOS had it when the boot continues. Setting bit 6 of the YM's I/O
; port A switches it off; TOS leaves the bit clear, with the speaker on.
; Only on a TT: other machines use that bit for something else.
MCH_TT				equ 2		; _MCH cookie high word on a TT
YM_PORT_A			equ 14		; YM register: I/O port A
YM_TT_SPEAKER_OFF	equ $40		; Port A bit 6: set, the TT's internal speaker is off

; If 1, the display will not use the framebuffer and will write directly to the
; display memory. This is useful to reduce the memory usage in the rp2040
; When not using the framebuffer, the endianess swap must be done in the atari ST
DISPLAY_BYPASS_FRAMEBUFFER 	equ 1

CMD_NOP				equ 0		; No operation command
CMD_RESET			equ 1		; Reset command
CMD_BOOT_GEM		equ 2		; Boot GEM command
CMD_TERMINAL		equ 3		; Terminal command

_conterm			equ $484	; Conterm device number


; Constants needed for the commands
RANDOM_TOKEN_ADDR:        equ (ROM4_ADDR + $F000) 	      ; Random token address at $FAF000
RANDOM_TOKEN_SEED_ADDR:   equ (RANDOM_TOKEN_ADDR + 4) 	  ; RANDOM_TOKEN_ADDR + 4 bytes
RANDOM_TOKEN_POST_WAIT:   equ $1        		      	  ; Wait this cycles after the random number generator is ready
COMMAND_TIMEOUT           equ $0000FFFF                   ; Timeout for the command

SHARED_VARIABLES:     	  equ (RANDOM_TOKEN_ADDR + $200)  ; random token + 512 bytes to the shared variables area: $FAF200

ROMCMD_START_ADDR:        equ $FB0000					  ; We are going to use ROM3 address
CMD_MAGIC_NUMBER    	  equ ($ABCD) 					  ; Magic number header to identify a command
CMD_RETRIES_COUNT	  	  equ 3							  ; Number of retries for the command
CMD_SET_SHARED_VAR		  equ 1							  ; This is a fake command to set the shared variables
														  ; Used to store the system settings
; App commands for the terminal
APP_TERMINAL 				equ $0 ; The terminal app

; App terminal commands
APP_TERMINAL_START   		equ $0 ; Start terminal command
APP_TERMINAL_KEYSTROKE 		equ $1 ; Keystroke command

_dskbufp                equ $4c6                            ; Address of the disk buffer pointer    


	include inc/sidecart_macros.s
	include inc/tos.s



; Macros
; XBIOS Vsync wait
vsync_wait          macro
					move.w #37,-(sp)
					trap #14
					addq.l #2,sp
                    endm    

; XBIOS GetRez
; Return the current screen resolution in D0
get_rez				macro
					move.w #4,-(sp)
					trap #14
					addq.l #2,sp
					endm

; XBIOS Get Screen Base
; Return the screen memory address in D0
get_screen_base		macro
					move.w #2,-(sp)
					trap #14
					addq.l #2,sp
					endm

; XBIOS Setscreen, resolution only: both screen addresses stay as they are.
; \1 is the resolution, \2 the Falcon mode, read only when \1 is 3.
set_rez				macro
					move.w \2,-(sp)			; Falcon mode
					move.w \1,-(sp)			; Resolution
					move.l #-1,-(sp)		; Physical screen unchanged
					move.l #-1,-(sp)		; Logical screen unchanged
					move.w #5,-(sp)			; XBIOS Setscreen
					trap #14
					lea 14(sp),sp
					endm

; XBIOS VsetMode(-1), Falcon only
; Return the current video mode in D0
get_falcon_mode		macro
					move.w #-1,-(sp)
					move.w #88,-(sp)
					trap #14
					addq.l #4,sp
					endm

; XBIOS Giaccess, read
; Return YM register \1 in D0
read_ym				macro
					move.w \1,-(sp)			; Register number, bit 7 clear: read
					clr.w -(sp)				; Data, unused for a read
					move.w #28,-(sp)
					trap #14
					addq.l #6,sp
					endm

; XBIOS Offgibit and Ongibit: AND or OR YM port A with \1, interrupts masked
port_a_and			macro
					move.w \1,-(sp)
					move.w #29,-(sp)
					trap #14
					addq.l #4,sp
					endm

port_a_or			macro
					move.w \1,-(sp)
					move.w #30,-(sp)
					trap #14
					addq.l #4,sp
					endm

; Switch a TT's speaker back on if TOS had it on. D7 holds port A as TOS left
; it, or -1 when this is not a TT.
speaker_restore		macro
					tst.w d7
					bmi.s .\@done				; Not a TT
					btst #6, d7
					bne.s .\@done				; TOS had it off
					port_a_and #~YM_TT_SPEAKER_OFF&$FF
.\@done:
					endm

; Check the left or right shift key. If pressed, exit.
check_shift_keys	macro
					move.w #-1, -(sp)			; Read all key status
					move.w #$b, -(sp)			; BIOS Get shift key status
					trap #13
					addq.l #4,sp

					btst #1,d0					; Left shift skip and boot GEM
					bne boot_gem

					btst #0,d0					; Right shift skip and boot GEM
					bne boot_gem

					endm

; Check the keys pressed
check_keys			macro

					gemdos	Cconis,2		; Check if a key is pressed
					tst.l d0
					beq .\@no_key

					gemdos	Cnecin,2		; Read the key pressed

					cmp.b #27, d0		; Check if the key is ESC
					beq .\@esc_key	; If it is, send terminal command

					move.l d0, d3
					send_sync APP_TERMINAL_KEYSTROKE, 4

					bra .\@no_key
.\@esc_key:
					send_sync APP_TERMINAL_START, 0

.\@no_key:

					endm

check_commands		macro
					move.l (FRAMEBUFFER_ADDR + FRAMEBUFFER_SIZE), d6	; Store in the D6 register the remote command value
					cmp.l #CMD_TERMINAL, d6		; Check if the command is a terminal command
					bne.s .\@check_reset

					; Check the keys for the terminal emulation
					check_keys
					bra .\@bypass
.\@check_reset:
					cmp.l #CMD_RESET, d6		; Check if the command is a reset
					beq .reset					; If it is, reset the computer
					cmp.l #CMD_BOOT_GEM, d6		; Check if the command is to boot GEM
					beq boot_gem				; If it is, boot GEM

					; If we are here, the command is a NOP
					; If the command is a NOP, check the shift keys to bypass the command
					check_shift_keys
					check_keys
.\@bypass:
					endm

	section

;Rom cartridge

	org ROM4_ADDR

	dc.l $abcdef42 					; magic number
first:
;	dc.l second
	dc.l 0
	dc.l $08000000 + pre_auto		; After GEMDOS init (before booting from disks)
	dc.l 0
	dc.w GEMDOS_TIME 				;time
	dc.w GEMDOS_DATE 				;date
	dc.l end_pre_auto - pre_auto
	dc.b "TERM",0
    even

pre_auto:
; Relocate the content of the cartridge ROM to the RAM

; Get the screen memory address to display
	get_screen_base
	move.l d0, a2

	lea SCREEN_SIZE(a2), a2		; Move to the end of the screen memory
	move.l a2, a3				; Save the screen memory address in A3
	; Copy the code out of the ROM to avoid unstable behavior
    move.l #end_rom_code - start_rom_code + 3, d6	; Copied in longs: round up
    lea start_rom_code, a1    ; a1 points to the start of the code in ROM
    lsr.w #2, d6
    subq #1, d6
.copy_rom_code:
    move.l (a1)+, (a2)+
    dbf d6, .copy_rom_code
	jmp (a3)

start_rom_code:
; We assume the screen memory address is in D0 after the get_screen_base call
	move.l d0, a6				; Save the screen memory address in A6

; Enable bconin to return shift key status
	or.b #%1000, _conterm.w

; On a TT, switch the internal speaker off. d7 keeps port A as TOS left it,
; to put the speaker back when the boot continues, or -1 when this is not
; a TT. d7 survives the TOS calls, and send_sync saves it.
	moveq #-1, d7
	move.l #'_MCH', d1
	bsr find_cookie
	swap d0
	cmp.w #MCH_TT, d0
	bne.s .speaker_done
	read_ym #YM_PORT_A
	moveq #0, d7
	move.b d0, d7				; Port A as TOS left it
	port_a_or #YM_TT_SPEAKER_OFF
.speaker_done:

; Get the resolution of the screen
	get_rez
	move.w d0, d3				; Keep it: d3 survives the TOS calls

; On a Falcon, Getrez only describes the ST compatible modes: in 640x480 in 16
; colours it says ST high. Any other Falcon mode counts as REZ_FALCON, and the
; exact mode is kept in a4 to put it back.
	move.l #'_VDO', d1
	bsr find_cookie
	swap d0
	cmp.w #3, d0				; $0003xxxx: Falcon video
	bne.s .rez_known
	get_falcon_mode
	move.w d0, a4				; Remember the Falcon mode
	btst #7, d0					; An ST compatible mode: Getrez is right
	bne.s .rez_known
	moveq #REZ_FALCON, d3
.rez_known:
	move.w d3, d0

	move.w #-1, a5				; Resolution to restore in boot_gem: none yet
	cmp.w #REZ_ST_HIGH, d0		; Check if the resolution is 640x400 (high resolution)
	beq .print_loop_high		; If it is, print the message in high resolution
	tst.w d0					; ST low is drawn as it is
	beq.s .print_loop_low
	cmp.w #REZ_TT_HIGH, d0		; TT high is left alone: its mono monitor cannot show ST low
	beq.s .print_loop_low

; Any other resolution (ST medium, TT medium, TT low, a Falcon mode) is
; switched to ST low. a4 and a5 survive the TOS calls and nothing here uses them.
	move.w d0, a5				; Remember the resolution
	set_rez #REZ_ST_LOW, #0

.print_loop_low:
	vsync_wait

; We must move from the cartridge ROM to the screen memory to display the messages
	move.l a6, a0				; Set the screen memory address in a0
	move.l #FRAMEBUFFER_ADDR, a1			; Set the cartridge ROM address in a1
	move.l #((FRAMEBUFFER_SIZE / 2) -1), d0			; Set the number of words to copy
.copy_screen_low:
	move.w (a1)+ , d1			; Copy a word from the cartridge ROM
	ifne DISPLAY_BYPASS_FRAMEBUFFER == 1
	rol.w #8, d1				; swap high and low bytes
	endif
	move.w d1, d2				; Copy the word to d2
	swap d2						; Swap the bytes
	move.w d1, d2				; Copy the word to d2
	move.l d2, (a0)+			; Copy the word to the screen memory
	move.l d2, (a0)+			; Copy the word to the screen memory
	dbf d0, .copy_screen_low    ; Loop until all the message is copied

; Check the different commands and the keyboard
	check_commands

	bra .print_loop_low		; Continue printing the message

.print_loop_high:
	vsync_wait

; We must move from the cartridge ROM to the screen memory to display the messages
	move.l a6, a1				; Set the screen memory address in a1
	move.l a6, a2
	lea BYTES_ROW_HIGH(a2), a2	; Move to the next line in the screen
	move.l #FRAMEBUFFER_ADDR, a0		; Set the cartridge ROM address in a0
	move.l #TRANSTABLE, a3		; Set the translation table in a3
	move.l #(ROWS_HIGH -1), d0	; Set the number of rows to copy - 1
.copy_screen_row_high:
	move.l #(COLS_HIGH -1), d1	; Set the number of columns to copy - 1 
.copy_screen_col_high:
	move.w (a0)+ , d2			; Copy a word from the cartridge ROM

	ifne DISPLAY_BYPASS_FRAMEBUFFER == 1
	rol.w #8, d2				; swap high and low bytes
	endif

	move.w d2, d3				; Copy the word to d3
	and.w #$FF00, d3			; Mask the high byte
	lsr.w #7, d3				; Shift the high byte 7 bits to the right
	move.w (a3, d3.w), d4		; Translate the high byte
	swap d4						; Swap the words

	and.w #$00FF, d2			; Mask the low byte
	add.w d2, d2				; Double the low byte
	move.w (a3, d2.w), d4		; Translate the low byte

	move.l d4, (a1)+			; Copy the word to the screen memory
	move.l d4, (a2)+			; Copy the word to the screen memory

	dbf d1, .copy_screen_col_high   ; Loop until all the message is copied

	lea BYTES_ROW_HIGH(a1), a1	; Move to the next line in the screen
	lea BYTES_ROW_HIGH(a2), a2	; Move to the next line in the screen

	dbf d0, .copy_screen_row_high   ; Loop until all the message is copied

; Check the different commands and the keyboard
	check_commands

	bra .print_loop_high		; Continue printing the message
	
.reset:
	speaker_restore				; The TT speaker back before the reset too
    move.l #PRE_RESET_WAIT, d6
.wait_me:
    subq.l #1, d6           ; Decrement the outer loop
    bne.s .wait_me          ; Wait for the timeout

	clr.l $420.w			; Invalidate memory system variables
	clr.l $43A.w
	clr.l $51A.w
	move.l $4.w, a0			; Now we can safely jump to the reset vector
	jmp (a0)
	nop

boot_gem:
	; If we get here, continue loading GEM
	; with the TT speaker as TOS had it
	speaker_restore
	; in the resolution TOS started in, if the screen was switched to ST low
	cmpa.w #-1, a5
	beq.s .rez_unchanged
	set_rez a5, a4
.rez_unchanged:
    rts

; Look a cookie up in the jar. d1 is its name. Return its value in d0, or 0
; when there is no jar or no such cookie. Clobbers a0.
find_cookie:
	move.l _p_cookies.w, d0
	beq.s .cookie_done			; No cookie jar
	move.l d0, a0
.cookie_next:
	move.l (a0)+, d0			; Cookie name, 0 at the end of the jar
	beq.s .cookie_done
	cmp.l d1, d0
	beq.s .cookie_found
	addq.l #4, a0				; Skip the cookie value
	bra.s .cookie_next
.cookie_found:
	move.l (a0), d0
.cookie_done:
	rts

; Shared functions included at the end of the file
; Don't forget to include the macros for the shared functions at the top of file
    include "inc/sidecart_functions.s"


end_rom_code:
end_pre_auto:
	even
	dc.l 0