!
!	setup.s		(C) 1991 Linus Torvalds
!
! setup.s is responsible for getting the system data from the BIOS,
! and putting them into the appropriate places in system memory.
! both setup.s and system has been loaded by the bootblock.
!
! This code asks the bios for memory/disk/other parameters, and
! puts them in a "safe" place: 0x90000-0x901FF, ie where the
! boot-block used to be. It is then up to the protected mode
! system to read them from there before the area is overwritten
! for buffer-blocks.
!
! setup.s 负责从 BIOS 中获取系统数据, 并将这些数据放到系统内存适当地方. 
! 此时 setup.s 和 system 已经由 bootsect 引导块加载到内存中.
!
! 这段代码询问 bios 有关内存/磁盘/其他参数, 
! 并将这些参数放到一个 "安全的" 地方: 0x90000-0x901ff, 
! 也即原来 bootsect 代码块曾经的地方,
! 然后在被缓冲块覆盖掉之前由保护模式的 system 读取.

! NOTE! These had better be the same as in bootsect.s!
! 以下这些参数最好和 bootsect.s 中的相同!
#include <linux/config.h>

INITSEG  = DEF_INITSEG			! we move boot here - out of the way	! 原来 bootsect 所处的段.
SYSSEG   = DEF_SYSSEG			! system loaded at 0x10000 (65536).		! system 在 0x10000 处.
SETUPSEG = DEF_SETUPSEG			! this is the current segment			! 本程序所在的段地址.

.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

entry start
start:

! ok, the read went well so we get current cursor position and save it for posterity.
! ok, 整个磁盘过程都正常, 现在将光标位置保存以备今后使用.

! 下句将 ds 置成 INITSEG(0x9000). 这已经在 bootsect 程序中设置过, 但是现在是 setup 程序, Linus 觉得需要重新设置一下.

	mov	ax, #INITSEG							! this is done in bootsect already, but...
	mov	ds, ax

	! Get memory size (extended mem, kB)
	! 取扩展内存的大小值(KB).
	! 利用 BIOS 中断 0x15 功能号 ah = 0x88 取系统所扩展内存大小并保存在内存 0x90002 处.
	! 返回: ax = 从 0x100000(1M) 处开始的扩展内存大小(KB). 若出错则 CF 置位, ax = 出错码.
	mov	ah, #0x88
	int	0x15
	mov	[2], ax									! 将扩展内存数值存在 0x90002 处(1 个字).

	! check for EGA/VGA and some config parameters
	! 检查显示方式(EGA/VGA)并取参数.
	! 调用 BIOS 中断 0x10, 附加功能选择方式信息. 功能号: ah = 0x12, bl = 0x10
	! 返回: bh = 显示状态. 0x00 - 彩色模式, I/O 端口 = 0x3dx; 0x01 - 单色模式, I/O 端口 = 0x3bX.
	! bl = 安装的显示内存. 0x00 - 64KB; 0x01 - 128KB; 0x02 - 192KB; 0x03 = 256KB.
	! cx = 显示卡特性参数

	mov	ah, #0x12
	mov	bl, #0x10
	int	0x10
	mov	[8], ax									! 0x90008 = ??
	mov	[10], bx								! 0x9000A - 安装的显示内存; 0x9000B = 显示状态(彩/单色)
	mov	[12], cx								! 0x9000C = 显示卡特性参数.
	! 检测屏幕当前行列值. 若显示卡是 VGA 卡时则请求用户选择显示行列值, 并保存到 0x9000E 处.
	mov	ax, #0x5019								! 在 ax 中预置屏幕默认行列值(ah = 80 列; al = 25 行).
	cmp	bl, #0x10								! 若中断返回 bl 值为 0x10, 则表示不是 VGA 显示卡, 跳转
	je novga
	call chsvga								! 检测显示卡厂家和类型, 修改显示行列值
novga:
	mov	[14], ax								! 保存屏幕当前行列值(0x9000E, 0x9000F).

	! 这段代码使用 BIOS 中断取屏幕当前光标位置(列, 行), 并保存在内存 0x90000 处(2 字节).
	! 控制台初始化程序会到此处读取该值.
	! BIOS 中断 0x10 功能号 ah = 0x03, 读光标位置.
	! 输入: bh = 页号
	! 返回: ch = 扫描开始线; cl = 扫描结束线; dh = 行号(0x00 顶端); dl = 列号(0x00 最左边).
	mov	ah, #0x03								! read cursor pos
	xor	bh, bh
	int	0x10									! save it in known place, con_init fetches
	mov	[0], dx									! it from 0x90000.

	! Get video-card data:
	! 下面这段用于取显示卡当前显示模式:
	! 调用 BIOS 中断 0x10, 功能号 ah = 0x0f
	! 返回: ah = 字符列数; al = 显示模式; bh = 当前显示页.
	! 0x90004(1 字)存放当前页; 0x90006 存放显示模式; 0x90007 存放字符列数.

	mov	ah, #0x0f
	int	0x10
	mov	[4], bx									! bh = display page
	mov	[6], ax									! al = video mode, ah = window width

	! Get hd0 data
	! 取第一个硬盘的信息(复制硬盘参数表).
	! 第 1 个硬盘参数表的首地址竟然是中断向量 0x41 的向量值! 而第 2 个硬盘参数表紧接在第 1 个表的后面, 
	! 中断向量 0x46 的向量值也指向第 2 个硬盘的参数表首址. 表的长度是 16 个字节.
	! 下面两段程序分别复制 ROM BIOS 中有关两个硬盘的参数表, 0x90080 处存放第 1 个硬盘的表, 
	! 0x90090 处存放第 2 个硬盘的表.

	! lds si,[4*0x41] 从内存指定位置处读取一个长指针值并放入 ds 和 si 寄存器中. ds 中放段地址, si 是段内偏移地址. 
	! 这里是把内存地址 4 * 0x41 (= 0x104) 处保存的 4 个字节读出. 这 4 字节即是硬盘参数表所处位置的段和偏移值.
	mov	ax, #0x0000
	mov	ds, ax
	lds	si, [4 * 0x41]							! 取中断向量 0x41 的值, 即 hd0 参数表的地址 -- ds:si
	mov	ax, #INITSEG
	mov	es, ax
	mov	di, #0x0080								! 传输目的地址: 0x9000:0x0080 -- es:di
	mov	cx, #0x10								! 共传输 16 字节.
	rep
	movsb

	! Get hd1 data

	mov	ax, #0x0000
	mov	ds, ax
	lds	si, [4 * 0x46]							! 取中断向量 0x46 的值, 即 hd1 参数表的地址 -- ds:si
	mov	ax, #INITSEG
	mov	es, ax
	mov	di, #0x0090								! 传输目的地址: 0x9000:0x0090 -- es:di
	mov	cx, #0x10
	rep
	movsb

	! Check that there IS a hd1 :-)
	! 检测系统是否有第 2 个硬盘. 如果没有则把第 2 个表清零.
	! 利用 BIOS 中断调用 0x13 的取盘类型功能, 功能号 ah = 0x15;
	! 输入: dl = 驱动器号(0x8X 是硬盘; 0x80 指第 1 个硬盘, 0x81 第 2 个硬盘)
	! 输出: ah = 类型码; 00 没有这个盘, CF 置位; 01 是软驱, 没有 change - line 支持;
	! 02 是软驱(或其它可移动设备), 有 change-line 支持; 03 是硬盘.

	mov	ax, #0x01500
	mov	dl, #0x81
	int	0x13
	jc	no_disk1
	cmp	ah, #3									! 是硬盘吗?(类型 = 3?).
	je	is_disk1
no_disk1:
	mov	ax, #INITSEG							! 第 2 个硬盘不存在, 则对第 2 个硬盘表清零.
	mov	es, ax
	mov	di, #0x0090
	mov	cx, #0x10
	mov	ax, #0x00
	rep
	stosb
is_disk1:
	! now we want to move to protected mode ...
	! 现在我们要进入保护模式中了...
	cli											! no interrupts allowed!	! 从此开始不允许中断.
	! first we move the system to it's rightful place
	! 首先我们将 system 模块移到正确的位置.
	! bootsect 引导程序会把 system 模块读入到内存 0x10000(64KB) 开始的位置. 由于当时假设 system 模块最大长度不会超过 0x80000(512KB), 
	! 即其末端不会超过内存地址 0x90000, 所以 bootsect 会把自己移动到 0x90000 开始的地方, 并把 setup 加载到它的后面. 
	! 下面这段程序的用途是把整个 system 模块移动到 0x00000 位置, 即把从 0x10000 到 0x8ffff 的内存数据块(512KB)整块地向内存低端移动了 0x10000(64KB) 的位置.
	mov	ax, #0x0000
	cld											! 'direction' = 0, movs moves forward
do_move:
	mov	es, ax									! destination segment	! es:di 是目的地址(初始为 0x0:0x0)
	add	ax, #0x1000
	cmp	ax, #0x9000								! 已经把最后一段(从 0x8000 段开始的 64KB)代码移动完.
	jz	end_move								! 是,则跳转.
	mov	ds, ax									! source segment	! ds:si 是源地址(初始为 0x1000:0x0)
	sub	di, di
	sub	si, si
	mov cx, #0x8000								! 移动 0x8000 字(64KB).
	rep
	movsw
	jmp	do_move

	! then we load the segment descriptors
	! 此后, 我们加载段描述符.
	!
	! 下面指令 lidt 用于加载中断描述符表(IDT)寄存器. 它的操作数(idt_48)有 6 个字节. 前 2 个字节(字节 0-1)是描述符表的字节长度值; 
	! 后 4 字节(字节 2-5)是描述符表的 32 位线性基地址. 中断描述符表中的每一个 8 字节表项指出发生中断时需要调用的代码信息. 
	! 与中断向量有些相似, 但要包含更多的信息.
	!
	! lgdt 指令用于加载全局描述符表(GDT)寄存器, 其操作数格式与 lidt 指令的相同. 全局描述符表中的每个符项(8 字节)描述了保护模式下数据段和代码段(块)的信息.
	! 其中包括段的最大长限制(16 位), 段的线性地址基址(32 位)/段的特权级,段是否在内存,读写许可权以及其他一些保护模式运行的标志.
end_move:
	mov	ax, #SETUPSEG							! right, forgot this at first. didn't work :-)
	mov	ds, ax									! ds 指向本程序(setup)段.
	lidt	idt_48								! load idt with 0,0						! 加载 IDT 寄存器.
	lgdt	gdt_48								! load gdt with whatever appropriate	! 加载 GDT 寄存器.

	! that was painless, now we enable A20
	! 以上的操作很简单, 现在我们开启 A20 地址线.
	! 为了能够访问和使用 1MB 以上的物理内存, 我们需要首先开启 A20 地址线.
	call	empty_8042							! 测试 8042 状态寄存器, 等待输入缓冲器空. 只有当输入缓冲器为空时才可以对其执行命令.
	mov	al, #0xD1								! command write	 		! 0xD1 命令码 -- 表示要写数据到 8042 的 P2 端口. P2 端口位 1 用于 A20 线的选通.
	out	#0x64, al
	call	empty_8042							! 等待输入缓冲器空, 看命令是否被接受.
	mov	al, #0xDF								! A20 on				! 选通A20地址线的参数.
	out	#0x60, al								! 数据要写到 0x60 端口.
	call	empty_8042							! 若此时输入缓冲器为空, 则表示 A20 线已经选通.

	! well, that went ok, I hope. Now we have to reprogram the interrupts :-(
	! we put them right after the intel-reserved hardware interrupts, at
	! int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
	! messed this up with the original PC, and they haven't been able to
	! rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
	! which is used for the internal hardware interrupts as well. We just
	! have to reprogram the 8259's, and it isn't fun.

	! .word	0x00eb, 0x00eb 是直接使用机器码表示的两条相对跳转指令, 起延时作用. 
	! 0xeb 是直接近跳转指令的操作码, 带 1 个字节的相对位移值. 因此跳转范围是 -127 -- 127. 
	! CPU 通过把这个相对位移值加到 EIP 寄存器中就形成一个新的有效地址. 此时 EIP 指向下一条被执行的指令.
	! 执行时所花费的 CPU 时钟周期数是 7 至 10 个. 0x00eb 表示跳转值是 0 的一条指令, 因此还是直接执行下一条指令. 
	! 这两条指令共可提供 14--20 个 CPU 时钟周期的延迟时间. 在 as86 上没有表示相应指令的助记符, 
	! 因此 Linus 在 setup.s 等一些汇编程序中就直接使用机器码来表示这种指令.
	! 另外, 每个空操作指令 NOP 的时钟周期数是 3 个, 因此若要达到相同的延迟效果就需要 6~7 个 NOP 指令.

	! 8259 芯片主片端口是 0x20-0x21, 从片端口是 0xA0-0xA1. 输出值 0x11 表示初始化命令开始, 它是 ICW1 命令字, 表示边沿触发, 
	! 多片 8269 级连, 最后要发送 ICW4 命令字.

	mov	al, #0x11								! initialization sequence
	out	#0x20, al								! send it to 8259A-1		! 发送到 8259A 主芯片.
	.word	0x00eb, 0x00eb						! jmp $+2, jmp $+2			! '$' 表示当前指令的地址.
	out	#0xA0, al								! and to 8259A-2			! 再发送到 8259A 从芯片.
	.word	0x00eb, 0x00eb
	! Linux 系统硬件中断号被设置成从 0x20 开始.
	mov	al, #0x20								! start of hardware int's (0x20)
	out	#0x21, al								! 送主芯片 ICW2 命令字, 设置起始中断号, 要送奇端口.
	.word	0x00eb, 0x00eb
	mov	al, #0x28								! start of hardware int's 2 (0x28)
	out	#0xA1, al								! 送从芯片 ICW2 命令字, 从芯片的起始中断号.
	.word	0x00eb, 0x00eb

	mov	al, #0x04								! 8259-1 is master
	out	#0x21, al								! 送主芯片 ICW3 命令字, 主芯片的 IR2 连从芯片 INT.
	.word	0x00eb, 0x00eb
	mov	al, #0x02								! 8259-2 is slave
	out	#0xA1, al								! 送从芯片 ICW3 命令字, 主芯片的 IR2 连从芯片的 INT 连到主芯片的 IR2 引脚上.

	.word	0x00eb, 0x00eb
	mov	al, #0x01								! 8086 mode for both
	out	#0x21, al								! 送主芯片 ICW4 命令字. 8086 模式; 普通 EOI, 非缓冲方式, 需发送指令来. 初始化结束, 芯片就绪.
	.word	0x00eb, 0x00eb
	out	#0xA1, al								! 送从芯片 ICW4 命令字, 内容同上.
	.word	0x00eb, 0x00eb
	mov	al, #0xFF								! mask off all interrupts for now
	out	#0x21, al								! 屏蔽主芯片所有中断请求.
	.word	0x00eb, 0x00eb
	out	#0xA1, al								! 屏蔽从芯片所有中断请求.

	! well, that certainly wasn't fun :-(. Hopefully it works, and we don't
	! need no steenking BIOS anyway (except for the initial loading :-).
	! The BIOS-routine wants lots of unnecessary data, and it's less
	! "interesting" anyway. This is how REAL programmers do it.
	!
	! Well, now's the time to actually move into protected mode. To make
	! things as simple as possible, we do no register set-up or anything,
	! we let the gnu-compiled 32-bit programs do that. We just jump to
	! absolute address 0x00000, in 32-bit protected mode.
	!
	! 下面设置并进入 32 位保护模式运行. 首先加载机器状态字, 也称控制寄存器 CR0, 其位 0 置 1 将导致 CPU 切换到保护模式, 
	! 并且运行在特权级 0 中, 即当前特权级 CPL = 0. 此时段寄存器仍然指向与实地址模式中相同的线性地址处(在实地址模式下线性地址与物理内存地址相同). 
	! 在设置该位后, 随后一条指令必须是一条段间跳转指令以用于刷新 CPU 当前指令队列. 因为 CPU 是在执行一条指令之前就已从内存读取该指令并对其进行解码. 
	! 然而在进入保护模式以后那些属于实模式的承先取得的指令信息就变得不再有效. 而一条段间跳转指令就会刷新 CPU 的当前指令队列, 即丢弃这些无效信息. 
	! 另外, 在 Intel 公司的手册上建议 80386 或以上 CPU 应该使用指令 "mov cr0, ax" 切换到保护模式. lmsw 指令仅用于兼容以前的 286CPU.

	mov	ax, #0x0001								! protected mode (PE) bit			! 保护模式位(PE).
	lmsw	ax									! This is it!						! 就这样加载机器状态字!
	jmpi	0, 8								! jmp offset 0 of segment 8 (cs)	! 跳转至 cs 段偏移 0 处.
	! 我们已经将 system 模块移动到 0x00000 开始的地方, 所以上句中的偏移地址是 0. 而段值 8 已经是保护模式下的段选择符了, 
	! 用于选择描述符表和描述符表项以及所要求的特权级. 段选择符长度为 16 位(2 字节); 
	! 位 0-1 表示请求的特权级 0--3, 但 Linux 操作系统只用到两级; 0 级(内核级)和 3 级(用户级); 
	! 位 2 用于选择全局描述符表(0)还是局部描述符表(1); 位 3-15 是描述符表项的索引, 指出选择第几项描述符. 
	! 所以段选择符 8(0b0000, 0000, 0000, 1000) 表示请求特权级 0, 使用全局描述符表 GD 中第 2 个段描述符项, 
	! 该项指出代码的基地址是 0, 因此这里的跳转指令就会去执行 system 中的代码.

! This routine checks that the keyboard command queue is empty
! No timeout is used - if this hangs there is something wrong with
! the machine, and we probably couldn't proceed anyway.
! 下面这个子程序检查键盘命令队列是否为空. 这里不使用超时方法.
! 如果这里死机, 则说明 PC 有问题, 我们就没有办法再处理下去了.
!
! 只有当输入缓冲器为空时(键盘控制器状态寄存器位 1 = 0)才可以对其执行写命令.
empty_8042:
	.word	0x00eb, 0x00eb
	in	al, #0x64								! 8042 status port			! 读 AT 键盘控制器状态寄存器
	test	al, #2								! is input buffer full?		! 测试位 1, 输入缓冲器满?
	jnz	empty_8042								! yes - loop
	ret

! Routine trying to recognize type of SVGA-board present (if any)
! and if it recognize one gives the choices of resolution it offers.
! If one is found the resolution chosen is given by al,ah (rows,cols).

chsvga:
	cld
	push	ds
	push	cs
	pop	ds
	mov 	ax, #0xc000
	mov	es, ax
	lea	si, msg1
	call	prtstr
nokey:
	in	al, #0x60
	cmp	al, #0x82
	jb	nokey
	cmp	al, #0xe0
	ja	nokey
	cmp	al, #0x9c
	je	svga
	mov	ax, #0x5019
	pop	ds
	ret
svga:
	lea si, idati								! Check ATI 'clues'
	mov	di, #0x31
	mov cx, #0x09
	repe
	cmpsb
	jne	noati
	lea	si, dscati
	lea	di, moati
	lea	cx, selmod
	jmp	cx
noati:
	mov	ax, #0x200f								! Check Ahead 'clues'
	mov	dx, #0x3ce
	out	dx, ax
	inc	dx
	in	al, dx
	cmp	al, #0x20
	je	isahed
	cmp	al, #0x21
	jne	noahed
isahed:
	lea	si, dscahead
	lea	di, moahead
	lea	cx, selmod
	jmp	cx
noahed:
	mov	dx, #0x3c3								! Check Chips & Tech. 'clues'
	in	al, dx
	or	al, #0x10
	out	dx, al
	mov	dx, #0x104
	in	al, dx
	mov	bl, al
	mov	dx, #0x3c3
	in	al, dx
	and	al, #0xef
	out	dx, al
	cmp	bl, [idcandt]
	jne	nocant
	lea	si, dsccandt
	lea	di, mocandt
	lea	cx, selmod
	jmp	cx
nocant:
	mov	dx, #0x3d4								! Check Cirrus 'clues'
	mov	al, #0x0c
	out	dx, al
	inc	dx
	in	al, dx
	mov	bl, al
	xor	al, al
	out	dx, al
	dec	dx
	mov	al, #0x1f
	out	dx, al
	inc	dx
	in	al, dx
	mov	bh, al
	xor	ah, ah
	shl	al, #4
	mov	cx, ax
	mov	al, bh
	shr	al, #4
	add	cx, ax
	shl	cx, #8
	add	cx, #6
	mov	ax, cx
	mov	dx, #0x3c4
	out	dx, ax
	inc	dx
	in	al, dx
	and	al, al
	jnz	nocirr
	mov	al, bh
	out	dx, al
	in	al, dx
	cmp	al, #0x01
	jne	nocirr
	call	rst3d4
	lea	si, dsccirrus
	lea	di, mocirrus
	lea	cx, selmod
	jmp	cx
rst3d4:
	mov	dx, #0x3d4
	mov	al, bl
	xor	ah, ah
	shl	ax, #8
	add	ax, #0x0c
	out	dx, ax
	ret
nocirr:
	call	rst3d4								! Check Everex 'clues'
	mov	ax, #0x7000
	xor	bx, bx
	int	0x10
	cmp	al, #0x70
	jne	noevrx
	shr	dx, #4
	cmp	dx, #0x678
	je	istrid
	cmp	dx, #0x236
	je	istrid
	lea	si, dsceverex
	lea	di, moeverex
	lea	cx, selmod
	jmp	cx
istrid:
	lea	cx, ev2tri
	jmp	cx
noevrx:
	lea	si, idgenoa								! Check Genoa 'clues'
	xor ax, ax
	seg es
	mov	al, [0x37]
	mov	di, ax
	mov	cx, #0x04
	dec	si
	dec	di
l1:
	inc	si
	inc	di
	mov	al, (si)
	seg es
	and	al, (di)
	cmp	al, (si)
	loope 	l1
	cmp	cx, #0x00
	jne	nogen
	lea	si, dscgenoa
	lea	di, mogenoa
	lea	cx, selmod
	jmp	cx
nogen:
	lea	si, idparadise							! Check Paradise 'clues'
	mov	di, #0x7d
	mov	cx, #0x04
	repe
	cmpsb
	jne	nopara
	lea	si, dscparadise
	lea	di, moparadise
	lea	cx, selmod
	jmp	cx
nopara:
	mov	dx, #0x3c4								! Check Trident 'clues'
	mov	al, #0x0e
	out	dx, al
	inc	dx
	in	al, dx
	xchg ah, al
	mov	al, #0x00
	out	dx, al
	in	al, dx
	xchg al,ah
	mov	bl, al									! Strange thing ... in the book this wasn't
	and	bl, #0x02								! necessary but it worked on my card which
	jz	setb2									! is a trident. Without it the screen goes
	and	al, #0xfd								! blurred ...
	jmp	clrb2									!
setb2:
	or	al, #0x02								!
clrb2:
	out	dx, al
	and	ah, #0x0f
	cmp	ah, #0x02
	jne	notrid
ev2tri:
	lea	si, dsctrident
	lea	di, motrident
	lea	cx, selmod
	jmp	cx
notrid:
	mov	dx, #0x3cd								! Check Tseng 'clues'
	in	al, dx									! Could things be this simple ! :-)
	mov	bl, al
	mov	al, #0x55
	out	dx, al
	in	al, dx
	mov	ah, al
	mov	al, bl
	out	dx, al
	cmp	ah, #0x55
 	jne	notsen
	lea	si, dsctseng
	lea	di, motseng
	lea	cx, selmod
	jmp	cx
notsen:
	mov	dx, #0x3cc								! Check Video7 'clues'
	in	al, dx
	mov	dx, #0x3b4
	and	al, #0x01
	jz	even7
	mov	dx, #0x3d4
even7:
	mov	al, #0x0c
	out	dx, al
	inc	dx
	in	al, dx
	mov	bl, al
	mov	al, #0x55
	out	dx, al
	in	al, dx
	dec	dx
	mov	al, #0x1f
	out	dx, al
	inc	dx
	in	al, dx
	mov	bh, al
	dec	dx
	mov	al, #0x0c
	out	dx, al
	inc	dx
	mov	al, bl
	out	dx, al
	mov	al, #0x55
	xor	al, #0xea
	cmp	al, bh
	jne	novid7
	lea	si, dscvideo7
	lea	di, movideo7
selmod:
	push	si
	lea	si, msg2
	call	prtstr
	xor	cx, cx
	mov	cl, (di)
	pop	si
	push	si
	push	cx
tbl:
	pop	bx
	push bx
	mov	al, bl
	sub	al, cl
	call dprnt
	call spcing
	lodsw
	xchg al, ah
	call dprnt
	xchg ah, al
	push ax
	mov	al, #0x78
	call prnt1
	pop	ax
	call	dprnt
	call	docr
	loop	tbl
	pop	cx
	call	docr
	lea	si, msg3
	call	prtstr
	pop	si
	add	cl, #0x80
nonum:
	in	al, #0x60							! Quick and dirty...
	cmp	al, #0x82
	jb	nonum
	cmp	al, #0x8b
	je	zero
	cmp	al, cl
	ja	nonum
	jmp	nozero
zero:
	sub	al, #0x0a
nozero:
	sub	al, #0x80
	dec	al
	xor	ah, ah
	add	di, ax
	inc	di
	push	ax
	mov	al, (di)
	int 	0x10
	pop	ax
	shl	ax, #1
	add	si, ax
	lodsw
	pop	ds
	ret
novid7:
	pop	ds										! Here could be code to support standard 80x50,80x30
	mov	ax, #0x5019
	ret

! Routine that 'tabs' to next col.

spcing:
	mov	al, #0x2e
	call	prnt1
	mov	al, #0x20
	call	prnt1
	mov	al, #0x20
	call	prnt1
	mov	al, #0x20
	call	prnt1
	mov	al, #0x20
	call	prnt1
	ret

! Routine to print asciiz-string at DS:SI

prtstr:
	lodsb
	and	al, al
	jz	fin
	call	prnt1
	jmp	prtstr
fin:
	ret

! Routine to print a decimal value on screen, the value to be
! printed is put in al (i.e 0-255).

dprnt:
	push	ax
	push	cx
	mov	ah, #0x00
	mov	cl, #0x0a
	idiv	cl
	cmp	al, #0x09
	jbe	lt100
	call	dprnt
	jmp	skip10
lt100:
	add	al, #0x30
	call	prnt1
skip10:
	mov	al, ah
	add	al, #0x30
	call	prnt1
	pop	cx
	pop	ax
	ret

! Part of above routine, this one just prints ascii al

prnt1:
	push	ax
	push	cx
	mov	bh, #0x00
	mov	cx, #0x01
	mov	ah, #0x0e
	int	0x10
	pop	cx
	pop	ax
	ret

! Prints <CR> + <LF>

docr:
	push	ax
	push	cx
	mov	bh, #0x00
	mov	ah, #0x0e
	mov	al, #0x0a
	mov	cx, #0x01
	int	0x10
	mov	al, #0x0d
	int	0x10
	pop	cx
	pop	ax
	ret

! 全局描述符表开始处. 描述符表由多个 8 字节长的描述符项组成. 这里给出了 3 个描述符项.
! 第 1 项无用, 但须存在. 第 2 项的系统代码段描述符, 第 3 项是系统数据段描述符.
gdt:
	.word	0, 0, 0, 0						! dummy	! 第 1 个描述符, 不用.

	! 在 GDT 表这里的偏移量是 0x08. 它是内核代码段选择符的值.
	.word	0x07FF							! 8Mb - limit=2047 (2048*4096=8Mb)	! (0~2047, 因此是 2048 * 4096B = 8MB)
	.word	0x0000							! base address=0
	.word	0x9A00							! code read/exec				! 代码段为只读, 可执行.
	.word	0x00C0							! granularity=4096, 386			! 颗粒度为 4096, 32 位模式.

	! 在 GDT 表中这里的偏移量是 0x10, 它是内核数据段选择符的值.
	.word	0x07FF							! 8Mb - limit=2047 (2048*4096=8Mb)	! (2048*4096B=8MB)
	.word	0x0000							! base address=0
	.word	0x9200							! data read/write			 	! 数据段为可读可写.
	.word	0x00C0							! granularity=4096, 386			! 颗粒度为 4096, 32 位模式.

! 下面是加载中断描述符表寄存器 idtr 的指令 lidt 要求的 6 字节操作数. 
! 前 2 字节的 IDT 的限长, 后 4 字节是 idt 表在线性地址空间中的 32 位基地址. 
! CPU 要求在进入保护模式之前需设置 IDT 表, 因此这里先设置一个长度为 0 的空表.
idt_48:
	.word	0								! idt limit=0
	.word	0, 0							! idt base=0L

! 这是加载全局描述符表寄存器 gdtr 的指令 lgdt 要求的 6 字节操作数. 前 2 字节是 gdt 的限长, 后 4 字节是 gdt 表的线性基地址. 
! 这里全局表长度设置为 2KB(0x7ff 即可), 因为每 8 字节组成一个段描述符项, 所以表中共可有 256 面. 
! 4 字节的线性基地址为 0x0009 << 16 + 0x0200 + gdt, 即 0x90200 + gdt. (符号 gdt 是全局表在本段中的偏移地址)
gdt_48:
	.word	0x800							! gdt limit=2048, 256 GDT entries
	.word	512 + gdt, 0x9					! gdt base = 0X9xxxx

msg1:		.ascii	"Press <RETURN> to see SVGA-modes available or any other key to continue."
		db	0x0d, 0x0a, 0x0a, 0x00
msg2:		.ascii	"Mode:  COLSxROWS:"
		db	0x0d, 0x0a, 0x0a, 0x00
msg3:		.ascii	"Choose mode by pressing the corresponding number."
		db	0x0d, 0x0a, 0x00

idati:		.ascii	"761295520"
idcandt:	.byte	0xa5
idgenoa:	.byte	0x77, 0x00, 0x66, 0x99
idparadise:	.ascii	"VGA="

! Manufacturer:	  Numofmodes:	Mode:

moati:		.byte	0x02,	0x23, 0x33
moahead:	.byte	0x05,	0x22, 0x23, 0x24, 0x2f, 0x34
mocandt:	.byte	0x02,	0x60, 0x61
mocirrus:	.byte	0x04,	0x1f, 0x20, 0x22, 0x31
moeverex:	.byte	0x0a,	0x03, 0x04, 0x07, 0x08, 0x0a, 0x0b, 0x16, 0x18, 0x21, 0x40
mogenoa:	.byte	0x0a,	0x58, 0x5a, 0x60, 0x61, 0x62, 0x63, 0x64, 0x72, 0x74, 0x78
moparadise:	.byte	0x02,	0x55, 0x54
motrident:	.byte	0x07,	0x50, 0x51, 0x52, 0x57, 0x58, 0x59, 0x5a
motseng:	.byte	0x05,	0x26, 0x2a, 0x23, 0x24, 0x22
movideo7:	.byte	0x06,	0x40, 0x43, 0x44, 0x41, 0x42, 0x45

!			msb = Cols lsb = Rows:

dscati:		.word	0x8419, 0x842c
dscahead:	.word	0x842c, 0x8419, 0x841c, 0xa032, 0x5042
dsccandt:	.word	0x8419, 0x8432
dsccirrus:	.word	0x8419, 0x842c, 0x841e, 0x6425
dsceverex:	.word	0x5022, 0x503c, 0x642b, 0x644b, 0x8419, 0x842c, 0x501e, 0x641b, 0xa040, 0x841e
dscgenoa:	.word	0x5020, 0x642a, 0x8419, 0x841d, 0x8420, 0x842c, 0x843c, 0x503c, 0x5042, 0x644b
dscparadise:	.word	0x8419, 0x842b
dsctrident:	.word 	0x501e, 0x502b, 0x503c, 0x8419, 0x841e, 0x842b, 0x843c
dsctseng:	.word	0x503c, 0x6428, 0x8419, 0x841c, 0x842c
dscvideo7:	.word	0x502b, 0x503c, 0x643c, 0x8419, 0x842c, 0x841c

.text
endtext:
.data
enddata:
.bss
endbss:
