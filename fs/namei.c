/*
 *  linux/fs/namei.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

#include <linux/sched.h>					// 调试程序头文件, 定义了任务结构 task_struct, 0 的数据等.
//#include <linux/kernel.h>
#include <asm/segment.h>					// 段操作头文件. 定义了有关段寄存器操作的嵌入式汇编函数.

#include <string.h>
#include <fcntl.h>							// 文件控制头文件. 文件及其描述符的操作控制常数符号的定义.
#include <errno.h>							// 错误号头文件. 包含系统中各种出错号.
#include <const.h>							// 常数符号头文件. 目前仅定义 inode 中 i_mode 字段的各标志位.
#include <sys/stat.h>						// 文件状态头文件. 含有文件或文件系统状态结构 stat() 和常量.

// 由文件名查找对应 inode 的内部函数.
static struct m_inode * _namei(const char * filename, struct m_inode * base, int follow_links);

// 下面宏中右侧表达式是访问数组的一种特殊使用方法. 它基于这样的一个事实, 
// 即用数组名和数组下标所表示的数组项(例如 a[b])的值等同于使用数组首指针(地址)加上该项偏移地址的形式的值 *(a+b), 
// 同时可知项 a[b] 也可以表示成 b[a] 的形式. 
// 因此对于字符数组项形式为 "LoveYou"[2] (或者 2["LoveYou"]) 就等同于 *("LoveYou" + 2); 
// 另外, 字符串 "LoveYou" 在内存中被存储的位置就是其地址, 
// 因此数组项 "LoveYou"[2] 的值就是该字符串索引值为 2 的字符 "v" 所对应的 ASCII 码值 0x76, 
// 或用八进制表示就是 0166. 在 C 语言中, 字符也可以用其 ASCII 码值来表示, 方法是在字符的 ASCII 码值前面加一个反斜杠. 
// 例如字符 "v" 可以表示成 "\x76" 或者 "\166". 
// 因此对于不可显示的字符(例如 ASCII 码值为 0x00--0x1f 的控制字符)就可用其 ASCII 码值来表示.
//
// 下面是访问模式宏. x 是头文件 include/fcntl.h 中行 7 行开始定义的文件访问(打开)标志. 
// 这个宏根据文件访问标志 x 的值来索引双引号中对应的数值. 
// 双引号中有 4 个八进制数值(实际表示 4 个控制字符): "\004\002\006\377", 
// 分别表示读, 写和执行的权限为: r, w, rw 和 wxrwxrwx, 
// 并且分别对应 x 的索引值 0--3. 例如, 如果 x 为 2, 则该宏返回八进制值 006, 表示可读可写(rw). 
// 另外, 其中 O_ACCMODE = 00003, 是索引值 x 的屏蔽码.
#define ACC_MODE(x) ("\004\002\006\377"[(x) & O_ACCMODE])

/*
 * comment out this line if you want names > NAME_LEN chars to be
 * truncated. Else they will be disallowed.
 */
/*
 * 如果想让文件名长度 > NAME_LEN 个的字符被截掉, 就将下面定义注释掉.
 */
/* #define NO_TRUNCATE */

#define MAY_EXEC 			1	// 可执行(可进入).
#define MAY_WRITE 			2	// 可写.
#define MAY_READ 			4	// 可读.

/*
 *	permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * I don't know if we should look at just the euid or both euid and
 * uid, but that should be easily changed.
 */
/*
 *	permission()
 *
 * 该函数用于检测一个文件的读/写/执行权限. 我不知道是否只需检查 euid, 还是需要检查 euid 和 uid 两者, 不过这很容易修改.
 */
// 检测文件访问许可权限(rwx).
// 参数: inode - 文件的 inode 指针; mask - 访问属性屏蔽码.
// 返回: 访问许可返回 1, 否则返回 0. (如果给定的 inode 的 i_mode 宿主访问权限(rwx)与 mask 指定权限相同，则表示有权限).
static int permission(struct m_inode * inode, int mask) {
	int mode = inode->i_mode;								// 文件访问属性.

	/* special case: not even root can read/write a deleted file */
	/* 特殊情况: 即使是超级用户(root)也不能读/写一个已被删除的文件. */
	// 如果 inode 有对应的设备, 但该 inode 的链接计数值等于 0, 表示该文件已被删除, 则返回.
	if (inode->i_dev && !inode->i_nlinks) {
		return 0;
	// 如果进程的有效用户 id(euid) 与 inode 的用户 id 相同, 则取文件宿主的访问权限.
	} else if (current->euid == inode->i_uid) {
		mode >>= 6; 			// 右移 6 位得到文件宿主信息等. (i_mode 低 6 位分别是其他人和组员访问权限)
	// 如果进程有效组 id(egid) 与 inode 的组 id 相同, 则取组用户的访问权限
	} else if (in_group_p(inode->i_gid)) {
		mode >>= 3;
	}
	// 最后判断如果所取的的访问权限与屏蔽码相同, 或者是超级用户, 则返回 1, 否则返回 0.
	if (((mode & mask & 0007) == mask) || suser()) {
		return 1;
	}
	return 0;
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, match returns 1 for success, 0 for failure.
 */
/*
 * ok, 我们不能使用 strncmp 字符串比较函数, 因为名称不在我们的数据空间(不在内核空间). 
 * 因而我们只能使用 match(). 问题不大, match() 同样也处理一些完整的测试.
 *
 * 注意! 与 strncmp 不同的是 match() 成功时返回 1, 失败时返回 0.
 */
// 指定长度字符串比较函数.
// 参数: len - 比较的字符串长度; name - 文件名指针; de - 目录项结构.
// 返回: 相同返回 1, 不同返回 0.
static int match(int len, const char * name, struct dir_entry * de) {
	register int same __asm__("ax");

	// 首先判断函数参数的有效性. 
	// 如果目录项指针空, 或者目录项 inode 等于 0, 或者要比较的字符串长度超过文件名长度, 则返回 0(不匹配).
	if (!de || !de->inode || len > NAME_LEN) {
		return 0;
	}
	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
    /* ""当作 "." 来看待 ---> 这样就能处理像 "/usr/lib//libc.a" 那样的路径名 */
    // 如果比较的长度 len 等于 0 并且目录项中文件名的第 1 个字符是 '.', 
	// 并且只有这么一个字符, 那么我们就认为是相同的, 因此返回 1(匹配)
	if (!len && (de->name[0] == '.') && (de->name[1] == '\0')) {
		return 1;
	}
	// 如果要比较的长度 len 小于 NAME_LEN, 但是目录项中文件名长度超过 len, 则也返回 0(不匹配)
	// 第 75 行上对目录项中文件名长度是否超过 len 的判断方法是检测 name[len] 是否为 NULL. 
	// 若长度超过 len, 则 name[len] 处就是一个不是 NULL 的普通字符. 
	// 而对于长度为 len 的字符串 name, 字符 name[len] 就应该是 NULL.
	if (len < NAME_LEN && de->name[len]) {
		return 0;
	}
	// 然后使用嵌入汇编语句进行快速比较操作. 它会在用户数据空间(fs 段)执行字符串的比较操作. 
	// %0 - eax(比较结果 same); %1 - eax(eax 初值 0); %2 - esi(名字指针);
	// %3 - edi(目录项名指针); %4 - ecs(比较的字节长度值 len).
	__asm__(\
		"cld\n\t"							// 清方向标志位.
		"fs; repe; cmpsb\n\t"				// 用户空间执行循环比较 [esi++] 和 [edi++] 操作.
		"setz %%al"							// 若比较结果一样(zf = 0)则置 al = 1(same = eax).
		: "=a" (same)
		: "0" (0), "S" ((long) name), "D" ((long) de->name), "c" (len));
	return same;							// 返回比较结果.
}

/*
 * find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 *
 * This also takes care of the few special cases due to '..'-traversal
 * over a pseudo-root and a mount point.
 */
/*
 * find_entry()
 *
 * 在指定目录中寻找一个与名字匹配的目录项. 返回一个含有找到目录项的高速缓冲块以及目录项本身(参数 -- res_dir). 
 * 该函数并不读取目录项的 inode  -- 如果需要的话则自己操作.
 *
 * 由于有 '..' 目录项, 因此在操作期间也会对几种特殊情况分别处理 -- 比如跨越一个伪根目录以及挂载点.
 */
// 从给定的 inode 中查找指定目录和文件名的目录项所在的数据块, 并返回该数据块对应的高速缓冲区指针.
// 参数: *dir - 指定目录 inode 的指针; name - 文件名; namelen - 文件名长度; 
// 该函数在指定目录的数据(文件)中搜索指定文件名的目录项.
// 并对指定文件名是 '..' 的情况根据当前进行的相关设置进行特殊处理.
// 返回: 成功则返回指定 name 的目录项所在数据块的高速缓冲区指针(
// 		比如 'dev/tty1' 则返回 'dev' 对应的目录项所在的数据块的高速缓冲区指针), 
// 并在 *res_dir 处返回的目录项结构指针. 失败则返回空指针 NULL.
static struct buffer_head * find_entry(struct m_inode ** dir, const char * name, 
										int namelen, struct dir_entry ** res_dir) {
	int entries;
	int block, i;
	struct buffer_head * bh;
	struct dir_entry * de; 										// 目录项指针.
	struct super_block * sb;

	// 同样, 本函数一开始也需要对函数参数的有效性进行判断和验证. 
	// 如果我们在本文件前面的代码中定义了符号常数 NO_TRUNCATE, 
	// 那么如果文件名长度超过最大长度 NAME_LEN, 则不予处理. 
	// 如果没有定义过 NO_TRUNCATE, 那么在文件名长度超过最大长度 NAME_LEN 时截短之.
#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN) {
		return NULL;
	}
#else
	if (namelen > NAME_LEN) {
		namelen = NAME_LEN;
	}
#endif
	// 首先计算本目录中目录项数 entries. 目录 inode  i_size 字段表示本目录的数据长度, 
	entries = (*dir)->i_size / (sizeof(struct dir_entry)); 	// 该目录(文件)可以保存多少个目录项(dir_entry).
	*res_dir = NULL; 											// 先置空目录项指针.
	// 接下来我们对目录项文件名是 '..' 的情况进行特殊处理. 如果当前进程指定的根 inode 就是函数参数指定的目录, 
	// 则说明对于本进程来说, 这个目录就是它的伪根目录, 即进程只能访问该目录中的项而不能退到其父目录中去. 
	// 也即对于该进程本目录就如同是文件系统的根目录. 因此我们需要将文件名修改为 '.'.
	// 否则, 如果该目录的 inode 号等于 ROOT_INO(1 号)的话, 说明确实是文件系统的根 inode. 则取文件系统的超级块. 
	// 如果被安装到的 inode 存在, 则先放回原 inode, 然后对被安装到的 inode 进行处理. 
	// 于是我们让 *dir 指向该被安装到的 inode ; 并且该 inode 的引用数加 1. 
	// 即针对这种情况, 我们悄悄进行了 "偷梁换柱" 工程:)
	/* check for '..', as we might have to do some "magic" for it */
	/* 检查目录项 '..', 因为我们可能需要对其进行特殊处理, 这里为什么要用 get_fs_type 来获取字符内容, 参考 do_execve() 函数的说明 */
	if (namelen == 2 && get_fs_byte(name) == '.' && get_fs_byte(name + 1) == '.') { 	// 如果要处理的目录项名是 '..'
		/* '..' in a pseudo-root results in a faked '.' (just change namelen) */
		// 在给定 inode 是进程根节点('/' 目录对应的 inode)的情况下， 如果再查找 '..' 目录项是不行的, 
		// 因为已在 '/' 目录下了, 没有上级目录了， 那么要获取的目录项名就只取 '.'
		if ((*dir) == current->root) {
			namelen = 1;
		} else if ((*dir)->i_num == ROOT_INO) { 	// 如果是真正的根 inode 的情况(inode 号为 1)下.
			/* '..' over a mount-point results in 'dir' being exchanged for the mounted
			   directory-inode. NOTE! We set mounted, so that we can iput the new dir */
			/* 在一个挂载点上的 '..' 需要将目录切换到挂载到的目录 inode 上. 
			   注意! 由于我们设置了 mounted 标志, 因而我们能够放回该新目录. */
			sb = get_super((*dir)->i_dev); 		// 获取设备的超级块信息.
			if (sb->s_imount) { 				// 如果该文件系统挂载到某个 inode 了.
				iput(*dir);
				(*dir) = sb->s_imount; 			// 将当前的 inode 更换为挂载点的 inode.
				(*dir)->i_count++;
			}
		}
	}
	// 现在我们开始正常操作, 查找指定名字的目录项在什么地方. 
	// 我们需要读取当前 inode 的数据区, 即取出当前 inode 在块设备中的数据块(逻辑块)信息. 
	// 这些逻辑块的块号保存在 inode 结构的 i_zone[] 数组中. 我们先取其中第 1 个块号. 
	if (!(block = (*dir)->i_zone[0])) {			// 如果第一个逻辑块号为 0, 则表示出错.
		return NULL;
	}
	// 从设备中读取指定的目录项数据块. 如果不成功, 则返回 NULL 退出.
	if (!(bh = bread((*dir)->i_dev, block))) {
		return NULL;
	}
	// 在当前的目录 inode 数据块中搜索匹配指定名字的目录项. 首先让 de 指向缓冲块中的数据块部分, 
	// 并在不超过当前 inode 的数据长度条件下, 循环执行搜索. 其中 i 是 inode 中的目录项索引号, 在循环开始时初始化为 0.
	i = 0;
	de = (struct dir_entry *)bh->b_data; 						// 初始化目录项指针指向当前 inode 的数据区. 
	while (i < entries) {
		// 如果当前目录项数据块已经搜索完, 还没有找到匹配的目录项, 则释放当前目录项数据块. 再读入目录的下一个逻辑块. 
		// 若这块为空, 则只要还没有搜索完目录中的所有目录项, 就跳过该块, 继续读目录的下一逻辑块. 
		// 若该块不空, 就让 de 指向该数据块, 然后在其中继续搜索. 
		// 其中 `i / DIR_ENTRIES_PER_BLOCK` 可得到当前搜索的目录项所在目录文件中的块号, 
		// 而 bmap() 函数(inode.c)则可计算出在设备上对应的逻辑块号.
		if ((char *)de >= (BLOCK_SIZE + bh->b_data)) { 			// 已搜索完该数据块.
			brelse(bh); 										// 则释放该数据块.
			bh = NULL; 											// 并读取下一个数据块, 如果为空则跳过该块.
			// 如果块号为 0, 或者从设备中读取这个数据块失败, 则跳过这个数据块并继续搜寻下一个数据块.
			if (!(block = bmap(*dir, i / DIR_ENTRIES_PER_BLOCK)) || !(bh = bread((*dir)->i_dev, block))) { 
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *)bh->b_data;
		}
		// 如果找到匹配的目录项的话, 则返回该目录项指针 de 和该目录项 inode 指针 *dir 以及该目录项数据块指针 bh, 
		// 并退出函数. 否则继续在目录项数据块中比较下一个目录项.
		if (match(namelen, name, de)) {
			*res_dir = de; 								// 找到 name 对应的目录项了, 更新这个指针.
			return bh; 									// 返回要找的目录项所在的数据块指针.
		}
		// 没有匹配到, 则继续匹配下一个目录项.
		de++; 											// 指向下一个目录项继续匹配.
		i++;
	}
	// 如果当前 inode 中没有找到相应的目录项, 则释放这个 inode 的数据块, 最后返回 NULL(失败).
	brelse(bh);
	return NULL;
}

/*
 *	add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
/*
 *      add_entry()
 * 使用与 find_entry() 同样的方法, 往指定目录中添加一指定文件名的目录项. 如果失败则返回 NULL. 
 *
 * 注意!! 'de'(指定目录项结构指针) 的 inode 部分被设置为 0 - 这表示在调用该函数和往目录项中添加信息之间不能去睡眠, 
 * 因为如果睡眠, 那么其他人(进程)可能会使用该目录项. 
 */
// 根据指定的目录和文件名添加目录项. 
// 参数: dir - 指定目录的 inode ; name - 文件名; namelen - 文件名长度; 
// 返回: 高速缓冲区指针; res_dir - 返回的目录项结构指针. 
static struct buffer_head * add_entry(struct m_inode * dir, const char * name, int namelen, struct dir_entry ** res_dir) {
	int block, i;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 同样, 本函数一开始也需要对函数参数的有效性进行判断和验证. 如果我们在前面定义了符号常数 NO_TRUNCATE,  
	// 那么如果文件名长度超过最大长度 NAME_LEN, 则不予处理. 
	// 如果没有定义过 NO_TRUNCATE, 那么在文件长度超过最大长度 NAME_LEN 时截断之. 
	*res_dir = NULL;                							// 用于返回目录项结构指针. 
#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN) {
		return NULL;
	}
#else
	if (namelen > NAME_LEN) {
		namelen = NAME_LEN;
	}
#endif
	// 现在我们开始操作, 向指定目录中添加一个指定文件名的目录项. 因此我们需要先读取目录的数据, 
	// 即取出目录 inode 对应块设备数据区中的数据块(逻辑块)信息. 这些逻辑块的块号保存在 inode 结构的 i_zone[9] 数组中. 
	// 我们先取其第 1 个块号. 如果目录 inode 指向的第一个直接磁盘块号为 0, 则说明该目录竟然不含数据, 这不正常. 
	// 于是返回 NULL 退出. 否则我们就从节点所在设备读取指定的目录项数据块. 如果不成功, 则也返回 NULL 退出. 
	// 另外, 如果参数提供的文件名长度等于 0, 则也返回 NULL 退出. 
	if (!namelen) return NULL;

	if (!(block = dir->i_zone[0])) {
		return NULL;
	}
	if (!(bh = bread(dir->i_dev, block))) {
		return NULL;
	}
	// 此时我们就在这个目录 inode 数据块中循环查找最后未使用的空目录项. 
	// 首先让目录项结构指针 de 指向缓冲块中的数据块部分, 即第一个目录项处. 
	// 其中 i 是目录中的目录项索引号, 在循环开始时初始化为 0. 
	i = 0;
	de = (struct dir_entry *)bh->b_data;
	while (1) {
		// 如果当前目录项数据块已经搜索完毕, 但还没有找到需要的空目录项, 则释放当前目录项数据块, 再读入目录的下一个逻辑块. 
		// 如果对应的逻辑块不存在就创建一块. 若读取或创建操作失败则返回空. 
		// 如果此次读取的磁盘逻辑块数据返回的缓冲块指针为空, 说明这块逻辑块可能是因为不存在而新创建的空块, 
		// 则把目录项索引值加上一块逻辑块所能容纳的目录项数 DIR_ENTRIES_PER_BLOCK, 用以跳过该块并继续搜索. 
		// 否则说明新读入的块上有目录项数据, 于是让目录项结构指针 de 指向该块的缓冲块数据部分, 然后在其中继续搜索. 
		// 其中 i / DIR_ENTRIES_PER_BLOCK 可计算得到当前搜索的目录项 i 所在目录文件中的块号, 
		// 而 create_block() 函数(inode.c)则可读取或创建出在设备上对应的逻辑块. 
		if ((char *)de >= BLOCK_SIZE + bh->b_data) {
			brelse(bh);
			bh = NULL;
			block = create_block(dir, i / DIR_ENTRIES_PER_BLOCK);
			if (!block)
				return NULL;
			if (!(bh = bread(dir->i_dev, block))) {          			// 若空则跳过该块继续. 
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *)bh->b_data;
		}
		// 如果当前所操作的目录项序号 i 乘上结构大小所得长度值已经超过目录 inode 信息所指出的目录数据长度值 i_size, 
		// 则说明整个目录文件数据中没有由于删除文件留下的空目录项, 
		// 因此我们只能把需要添加的新目录项附加到目录文件数据的末端处. 
		// 于是对该处目录项进行设置(置该目录项的 inode 指针为空), 并更新该目录文件的长度值(加上一个目录项的长度), 
		// 然后设置目录的 inode 已修改标志, 再更新该目录的改变时间为当前时间. 
		if (i * sizeof(struct dir_entry) >= dir->i_size) {
			de->inode = 0;
			dir->i_size = (i + 1) * sizeof(struct dir_entry);
			dir->i_dirt = 1;
			dir->i_ctime = CURRENT_TIME;
		}
		// 若当前搜索的目录项 de 的 inode 为空, 则表示找到一个还未使用的空闲目录项或是添加的新目录项. 
		// 于是更新目录的修改时间为当前时间, 并从用户数据区复制文件名到该目录项的文件名字段, 
		// 置含有本目录项的相应高速缓冲块已修改标志. 返回该目录项的指针以及该高速缓冲块的指针, 退出. 
		if (!de->inode) {
			dir->i_mtime = CURRENT_TIME;
			for (i = 0; i < NAME_LEN; i++) {
				de->name[i] = (i < namelen) ? get_fs_byte(name + i) : 0;
			}
			bh->b_dirt = 1;
			*res_dir = de;
			return bh;
		}
		de++;           						// 如果该目录项已经被使用, 则继续检测下一个目录项. 
		i++;
	}
	// 本函数执行不到这里. 这也许是 Linus 在写这段代码时, 先复制了上面 find_entry() 函数的代码, 而后修改成本函数的. 
	brelse(bh);
	return NULL;
}

// 查找符号链接文件对应的真实的 inode.
// 参数: dir - 目录 inode ; inode - 目录项 inode.
// 返回: 返回符号链接到的真实文件的 inode 指针. 出错返回 NULL.
static struct m_inode * follow_link(struct m_inode * dir, struct m_inode * inode) {
	unsigned short fs;							// 用于临时保存 fs 段寄存器值.
	struct buffer_head * bh;

	// 首先判断函数参数的有效性. 
	// 如果没有给出目录 inode (dir), 我们就使用当前进程结构中设置的根 inode, 并把链接数 +1. 
	if (!dir) {
		dir = current->root;
		dir->i_count++;
	}
	// 如果没有给出目录项 inode (inode), 则放回目录 inode 后返回 NULL. 
	if (!inode) {
		iput(dir);
		return NULL;
	}
	// 如果指定目录项不是一个符号链接, 就直接返回目录项对应的 inode  inode.
	if (!S_ISLNK(inode->i_mode)) {
		iput(dir);
		return inode;
	}
	// 然后取 fs 段寄存器值. fs 通常保存着指向任务数据段的选择符 0x17. 
	// 如果 fs 没有指向用户数据段, 或者给出的目录项 inode 第 1 个直接块块号等于 0, 
	// 或者是读取第 1 个直接块出错, 则放回 dir 和 inode 两个 inode 并返回 NULL 退出.
	// 否则说明现在 fs 正指向用户数据段, 并且我们已经成功地读取了符号链接目录项的文件内容, 
	// 并且文件内容已经在 bh 指向的缓冲块数据区中. 
	// 实际上, 这个缓冲块数据区中仅包含一个链接指向的文件路径名字符串.
	__asm__("mov %%fs, %0" : "=r" (fs));
	if (fs != 0x17 || !inode->i_zone[0] || !(bh = bread(inode->i_dev, inode->i_zone[0]))) {
		iput(dir);
		iput(inode);
		return NULL;
	}
	// 此时我们已经不需要符号链接目录项的 inode 了, 于是把它放回. 
	// 现在遇到一个问题, 那就是内核函数处理的用户数据都是存放在用户数据空间中的, 
	// 并使用了 fs 段寄存器来从用户空间传递数据到内核空间中. 而这里需要处理的数据却在内核空间中. 
	// 因此为了正确地处理位于内核中的用户数据, 我们需要让 fs 段寄存器临时指向内核空间, 即让 fs = 0x10. 
	// 并在调用函数处理完后再恢复原 fs 的值. 最后释放相应缓冲块, 并返回 _namei() 解析得到符号链接指向的文件 inode.
	iput(inode);
	__asm__("mov %0, %%fs" : : "r" ((unsigned short)0x10));
	inode = _namei(bh->b_data, dir, 0);
	__asm__("mov %0, %%fs" : : "r" (fs));
	brelse(bh);
	return inode;
}

/*
 * get_dir()
 *
 * Getdir traverses the pathname until it hits the topmost directory.
 * It returns NULL on failure.
 */
/*
 * 该函数根据给出的路径名进行搜索, 直到达到最深层(比如 /dev/tty1 -> dev/ 为最深层目录)的目录. 
 * 如果失败是返回 NULL.
 */
// 从指定目录开始获取给定路径名的最深层目录的 inode. 
// 参数: pathname - 路径名; inode - 指定起始目录的 inode.
// 返回: 给定目录名最深层目录的 inode 指针. 失败时返回 NULL.
static struct m_inode * get_dir(const char * pathname, struct m_inode * inode) {
	char c;
	const char * thisname;
	struct buffer_head * bh;
	int namelen, inr;
	struct dir_entry * de;
	struct m_inode * dir;

	// 首先判断参数有效性. 如果给出的指定目录的 inode 指针为空, 则使用当前进程的工作目录 inode.
	if (!inode) {
		inode = current->pwd;									// 使用进程的当前工作目录 inode.
		inode->i_count++;
	}
	// 如果用户指定路径名的第 1 个字符是 '/', 则说明路径名是绝对路径名. 
	// 则应该从当前进程任务结构中设置的根(或伪根) inode 开始操作.
	// 于是我们需要先放回参数指定的或者设定的目录 inode, 并取得进程使用的根 inode. 
	// 然后把该 inode 的引用计数加 1, 并删除路径名的第 1 个字符 '/'. 
	// 这样就可以保证进程只能以其设定的根 inode 作为搜索的起点.
	if ((c = get_fs_byte(pathname)) == '/') { 	// 如果 pathname 是以 '/' 开始的, 则放回指定的用户指定的 inode 或者上面指定的当前工作目录 inode, 使用根 inode.
		iput(inode);											// 放回原 inode.
		inode = current->root;									// 设置为进程指定的根 inode.
		pathname++;
		inode->i_count++;
	}
	// 然后针对路径名中的各个目录名部分和文件名进行循环处理. 
	// 在循环处理过程中, 我们先要对当前正在处理的目录名部分的 inode 进行有效性判断, 
	// 并且把变量 thisname 指向当前正在处理的目录名(文件名)部分. 
	// 如果该 inode 表明当前处理的目录名部分不是目录类型, 或者没有可进入该目录的访问许可, 
	// 则放回该 inode 并返回 NULL 退出. 
	// 当然在刚进入循环时, 当前目录的 inode  inode 就是进程根 inode 或者是当前工作目录的 inode, 
	// 或者是参数指定的某个搜索起始目录的 inode. 
	while (1) {
		thisname = pathname; 											// thisname 指向当前正在处理的部分.
		if (!S_ISDIR(inode->i_mode) || !permission(inode, MAY_EXEC)) { 	// 如果当前 inode 不是目录或者访问不被许可则放回 inode 并返回 NULL.
			iput(inode);
			return NULL;
		}
		// 每次循环我们取路径名中一个目录名(或文件名)部分进行处理. 
		// 因此在每次循环中我们都要从路径名字符串中分离出一个目录名(或文件名). 
		// 方法是从当前路径名指针 pathname 开始处搜索检测字符, 直到字符是一个结尾符(NULL)或者是一个 '/' 字符. 
		// 此时变量 namelen 正好是当前处理目录名部分的长度, 而变量 thisname 正指向该目录名部分的开始处. 
		// 此时如果字符是结尾符 NULL, 则表明已经搜索到路径名末尾, 并已到达最后指定目录名或文件名, 
		// 则返回该 inode 指针退出.
		// 注意! 如果路径名中最后一个名称也是一个目录名, 但其后面没有加上 '/' 字符, 
		// 则函数不会返回该最后目录名的 inode ! 
		// 例如: 对于目录 /usr/src/linux, 该函数将只返回 src/ 目录名的 inode.
		for(namelen = 0; (c = get_fs_byte(pathname++)) && (c != '/'); namelen++) {
			/* nothing */; // 每次该循环结束都将得到 pathname 中的一部分(比如 'dev/'[目录] 或者 'tty1'[文件]).
		}
		/* 循环处理, 直至获取到最深层目录的 inode 后返回其指针 */
		if (!c) {						// 如果当前到达 pathname 末尾('\0'), 则直接返回本次循环前的 inode(最深层目录的 inode).
			return inode; 				// 比如 '/dev/tty1' -> 则返回的是 'dev/' 对应的 inode.
		}
		// 在得到当前目录名部分(或文件名)后, 
		// 我们调用查找目录项函数 find_entry() 在当前 inode 中寻找指定名称的目录项(dir_entry). 
		// 如果没有找到, 则放回该 inode, 并返回 NULL 退出. 如果找到, 
		// 则在找到的目录项中取出其 inode 号 inr 和设备号 idev, 
		// 释放包含该目录项的高速缓冲块并放回该 inode. 然后取节点号 inr 的 inode  inode, 
		// 并以该目录项为当前目录继续循环处理路径名中的下一目录名部分(或文件名). 
		// 如果当前处理的目录项是一个符号链接名, 则使用 follow_link() 就可以得到其指向的目录项名 inode.
		if (!(bh = find_entry(&inode, thisname, namelen, &de))) { 	// 在给定的 inode 里寻找对应的目录项, 比如在 '/' 的 inode 里寻找 'dev' 目录项.
			iput(inode);
			return NULL;
		}
		inr = de->inode;										// 当前目录项指定(对应)的 inode 号.
		brelse(bh);
		dir = inode; 											// 暂存原 inode.
		if (!(inode = iget(dir->i_dev, inr))) {					// 将 inode 更新为当前目录项(dir_entry)对应的 inode 信息.
			iput(dir); 											// 读取出错则释放原 inode.
			return NULL;
		}
		if (!(inode = follow_link(dir, inode)))					// 如果当前的 inode 是符号链接, 则将指针更新为其链接到的 inode.
			return NULL;
    }
}

/*
 *	dir_namei()
 *
 * dir_namei() returns the inode of the directory of the
 * specified name, and the name within that directory.
 */
/*
 *	dir_namei()
 *
 * dir_namei() 函数返回给定路径中最深层的目录 inode 指针, 以及最深层目录/文件的名称.
 */
// 参数: pathname - 目录路径名; namelen - 用于保存得到的最深层目录/文件名的长度; 
// 		name - 用于保存得到的最深层目录/文件名; base - 搜索路径中的起始目录的 inode.
// 返回: 指定目录名最深层的 inode 指针和最深层目录/文件名称及长度. 
// 		比如 '/dev/tty1' 返回的文件名是 'tty1', 返回的 inode 是 'dev/' 的 inode. 出错时返回 NULL.
// 注意!! 这里 "最深层目录" 是指路径名中最靠近末端的目录(比如 '/dev/tty1' 的最深层目录为 'dev/' 而不是 '/').
static struct m_inode * dir_namei(const char * pathname, int * namelen, 
								  const char ** name, struct m_inode * base) {
	char c;
	const char * basename;
	struct m_inode * dir; 		// 最深层的目录对应的 inode. 比如 '/dev/tty1' -> '/dev/' 对应的 inode.

	// 首先取得指定路径名最深层目录的 inode. 
	// 然后对路径名 pathname 进行搜索检测, 查出最后一个 '/' 字符后面的文件名字符串, 
	// 计算其长度, 并且返回最深层目录的 inode 指针. 
	// 注意! 如果路径名最后一个字符是斜杠字符 '/', 那么返回的文件名为空, 并且长度为 0. 
	// 但返回的 inode 指针仍然指向最后一个 '/' 字符前目录名的 inode. 
	// 比如 '/dev/tty1' 则返回的是 'dev/' 对应的 inode.
	if (!(dir = get_dir(pathname, base))) {		// base 是指定的起始目录 inode. (获取最深层目录的 inode 指针)
		return NULL;
	}
	basename = pathname;
	while (c = get_fs_byte(pathname++)) {
		if (c == '/') {
			// 更新并最终得到路径最深层的一个目录或文件名, 比如 '/dev/tty1' 则 basename 是 'tty1', 
			// 如果是 '/dev/' 则 basename 为空, 如果是 '/dev' 则 basename 是 'dev'.
			basename = pathname; 				
		}
	}
	*namelen = pathname - basename - 1; 		// 得到最深层目录或文件名的长度.
	*name = basename; 							// 最深层的目录或文件名字符指针.
	return dir;
}

// 取指定路径名的 inode 内部函数.
// 参数: pathname - 路径名; base - 搜索起点目录 inode ; 
// 		follow_links - 是否跟随符号链接的标志, 1 - 需要, 0 - 不需要.
struct m_inode * _namei(const char * pathname, struct m_inode * base, int follow_links) {
	const char * basename;
	int inr, namelen;
	struct m_inode * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先查找指定路径名中最深层目录的目录名并得到其 inode. 若不存在, 则返回 NULL 退出. 
	// 如果返回的最深层文件名字的长度是 0, 则表示该路径名以一个目录名为结尾(比如 '/dev/'). 
	// 因此说明我们已经找到对应目录的 inode, 可以直接返回该 inode 退出.
	if (!(base = dir_namei(pathname, &namelen, &basename, base))) {
		return NULL;
	}
	if (!namelen) {										/* special case: '/usr/' etc */
		return base;									/* 对应于 '/usr/' 等情况 */
	}
	// 然后在返回的顶层目录中寻找指定文件名目录项的 inode. 
	// 注意! 因为如果最后也是一个目录名, 但其后没有加 '/', 则不会返回该最后目录的 inode ! 
	// 例如: /usr/src/linux, 将只返回 src/ 目录名的 inode. 
	// 因为函数 dir_namei() 将不以 '/' 结束的最后一个名字当作一个文件名来看待, 
	// 因此这里需要单独对这种情况使用寻找目录项 inode 函数 find_entry() 进行处理. 
	// 此时 de 中含有寻找到的目录项指针, 而 base 是包含该目录项的目录的 inode 指针.
	bh = find_entry(&base, basename, namelen, &de);
	if (!bh) {
		iput(base);
		return NULL;
	}
	// 接着取该目录项的 inode 号, 并释放包含该目录项的高速缓冲块并放回目录 inode. 
	// 然后取对应节点号的 inode, 修改其被访问时间为当前时间, 并置已修改标志. 
	// 最后返回该 inode 指针 inode. 如果当前处理的目录项是一个符号链接名, 
	// 则使用 follow_link() 得到其指向的目录项名的 inode.
	inr = de->inode;
	brelse(bh);
	if (!(inode = iget(base->i_dev, inr))) {
		iput(base);
		return NULL;
	}
	if (follow_links) {
		inode = follow_link(base, inode);
	} else {
		iput(base);
	}
	inode->i_atime = CURRENT_TIME;
	inode->i_dirt = 1;
	return inode;
}

// 取指定路径名的 inode, 不跟随符号链接. 
// 参数: pathname - 路径名. 
// 返回: 对应的 inode. 
struct m_inode * lnamei(const char * pathname) {
	return _namei(pathname, NULL, 0);
}

/*
 *	namei()
 *
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 */
/*
 *	namei()
 *
 * 该函数被许多简单命令用于取得指定路径名称的 inode. open, link 等则使用它们自己的相应函数. 
 * 但对于像修改模式 "chmod" 等这样的命令, 该函数已足够用了.
 */
// 取指定路径名的 inode, 跟随符号链接.
// 参数: pathname - 路径名.
// 返回: 对应的 inode.
struct m_inode * namei(const char * pathname) {
	return _namei(pathname, NULL, 1); 			// 起始目录 inode 为 NULL, 1 - 需要追踪符号链接信息.
}

/*
 *	open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.
 */
/*
 * 	open_namei()
 *
 * open() 函数使用的 namei 函数 - 这其实几乎是完整的打开文件程序.
 */
// 文件打开 namei 函数.
// filename - 文件名. 
// flag - 打开文件标志, 它可取值: O_RDONLY(只读), O_WRONLY(只写) 或 O_RDWR(读写), 以及 O_CREAT(创建),
// 		  O_EXCL(被创建文件必须不存在), O_APPEND(在文件尾添加数据) 等其他一些标志的组合. (include/fcntl.h)
// mode - 如果本调用创建了一个新文件, 则 mode 就用于指定文件的许可属性. 
//	  	  这些属性有: S_IRWXU(文件宿主具有读, 写和执行权限), S_IRUSR(用户具有读文件权限), 
// 		  S_IRWXG(组成员有读, 写执行) 等等. 
//        对于新创建的文件, 这些属性只应用于将来对文件的访问, 创建了只读文件的打开调用也将返回一个读写的文件句柄. 
// 如果调用操作成功, 则返回文件句柄(文件描述符 fd), 否则返回出错码. 参见 (sys/stat.h, include/fcntl.h).
// res_inode - 文件路径名的 inode 指针的指针(比如 '/dev/tty1' 则返回 'tty1' 对应的 inode).
// 返回: 成功返回 0, 否则返回出错码; 
int open_namei(const char * pathname, int flag, int mode, struct m_inode ** res_inode) {
	const char * basename;
	int inr, dev, namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先对函数参数进行合规处理. 如果文件访问标志是只读(O_RDONLY, flag == 0), 但是清空文件标志 O_TRUNC 却置位了, 
	// 则在文件访问标志中添加只写标志 O_WRONLY. 这样做的原因是由于清空文件标志 O_TRUNC 必须在文件可写情况下有效.
	if ((flag & O_TRUNC) && !(flag & O_ACCMODE)) { 	// !(flag & O_ACCMODE) 成立则表示 flag == 0, 即 O_RDONLY.
		flag |= O_WRONLY;							// 访问标志添加只写标志.
	}
	// 使用当前进程的文件访问许可屏蔽码, 屏蔽掉给定 mode 中的相应位, 并添上普通文件标志 I_REGULAR(include/const.h).
	// 该 mode 将用于打开的文件不存在, 需要创建文件时, 作为*新文件的默认属性*.
	mode &= (0777 & ~current->umask);
	mode |= I_REGULAR;								// 添加常规文件标志. 参见 include/const.h 文件.
	// 然后根据指定的路径名寻找到对应的 inode (比如 '/dev/tty1' 则得到 '/dev/' 对应的 inode ), 
	// 以及最顶端目录名及其长度. 此时如果最顶端目录名长度为 0(例如 '/usr/' 这种路径名的情况), 
	// 那么如果操作不是读/写/创建/文件长度截 0, 则表示是在打开一个目录名文件操作. 
	// 于是直接返回该目录的 inode 并返回 0 退出. 
	// 如果是这四种操作之一, 则说明进程操作非法, 于是放回该 inode, 返回出错码. 
	// 下面得到的 dir 为最顶层目录的 inode (比如 '/dev/tty1' 时得到 'dev/' 的 inode).
	// 示例: pathname = '/dev/tty1' 时, 得到 basename = 'tty1'.
	if (!(dir = dir_namei(pathname, &namelen, &basename, NULL))) {
		return -ENOENT;
	}
	// 如果文件名字为空(指定的 pathname 是一个目录路径, 比如: '/usr/'), 则返回.
	if (!namelen) {												/* special case: '/usr/' etc */
		if (!(flag & (O_ACCMODE | O_CREAT | O_TRUNC))) { 		// 如果 flag 不是其中的任何一个, 则表示是在执行打开目录的操作.
			*res_inode = dir; 									// 则直接返回该目录的 inode 指针.
			return 0;
		}
		iput(dir); 												// 否则表示出错.
		return -EISDIR;
	}
	// 最后在当前目录 inode 中查找 pathname 中的*最终目录/文件名*(/dev/tty1 中的 tty1)对应的目录项结构 de, 
	// 并同时得到该目录项所在数据块的缓冲块头指针. 
	bh = find_entry(&dir, basename, namelen, &de);
	// 如果该数据块的缓冲块指针为 NULL, 则表示没有找到对应文件名的目录项, 
	// 因此只可能是创建文件操作. 此时如果不是创建文件, 则放回该目录的 inode, 返回出错号退出. 
	// 如果用户在该目录没有写权限, 则也放回该目录的 inode, 返回出错号退出.
	if (!bh) { 										// 该目录下没有指定文件名的文件的情况下:
		if (!(flag & O_CREAT)) {                	// 如果不是创建文件操作, 则放回 inode 并返回错误号.
			iput(dir);
			return -ENOENT;
		}
		if (!permission(dir, MAY_WRITE)) {       	// 如果是创建文件操作但是没有写权限, 放回 inode 并返回错误号.
			iput(dir);
			return -EACCES;
		}
		// 现在我们确定了是创建操作并且有写操作权限. 
		// 因此我们就在目录 inode 对应设备上申请一个新的 inode 给路径名上指定的文件使用. 
		// 若失败则放回目录的 inode, 并返回没有空间出错码. 
		// 否则使用该新 inode, 对其进行初始设置: 置节点的用户 id; 对应节点访问模式; 置已修改标志. 
		// 然后并在指定目录 dir 中添加一个新目录项. 
		inode = new_inode(dir->i_dev); 				// (fs/bitmap.c)
		if (!inode) {
			iput(dir);
			return -ENOSPC;
		}
		inode->i_uid = current->euid;
		inode->i_mode = mode;
		inode->i_dirt = 1;
		bh = add_entry(dir, basename, namelen, &de);
		// 如果返回的应该含有新目录项的调整缓冲区指针为 NULL, 则表示添加目录项操作失败. 
		// 于是将该新 inode 的引用连接计数减 1, 放回该 inode 与目录的 inode 并返回出错码退出. 
		// 否则说明添加目录项操作成功. 于是我们来设置该新目录项的一些初始值: 置 inode 号为新申请到的 inode 的号码; 
		// 并置高速缓冲区修改标志, 然后释放该高速缓冲区, 放回目录的 inode. 返回新目录项的 inode 指针, 并成功退出. 
		if (!bh) {
			inode->i_nlinks--;
			iput(inode);
			iput(dir);
			return -ENOSPC;
		}
		de->inode = inode->i_num; 					// 设置目录项的 inode 编号.
		bh->b_dirt = 1; 							// 更新 dirt 标志, 因为添加了新的目录项. 需要刷写到硬盘上.
		brelse(bh); 								// 释放这个缓存块, 引用次数 -1.
		iput(dir); 									// 释放目录 inode.
		*res_inode = inode; 						// 最终得到最深层目录/文件的 inode.
		return 0;
    }
	// 在目录中找到目录/文件名对应目录项, 则说明要打开的文件已经存在. 于是取出该目录项的 inode 号和其所在设备号, 
	// 并释放该高速缓冲区以及放回这个文件所在目录的 inode(比如文件 '/dev/tty1' 所在的目录 inode 为 'dev/'). 
	// 如果此时独占操作标志 O_EXCL 置位, 但现在文件已经存在, 则返回文件已存在出错码退出.
	inr = de->inode;
	dev = dir->i_dev;
	brelse(bh);
	if (flag & O_EXCL) { 							// 如果设置了独占标志, 则被访问文件必须不存在, 所以返回出错码.
		iput(dir);
		return -EEXIST;
	}
	// 然后我们读取该目录项的 inode 内容. 
	if (!(inode = follow_link(dir, iget(dev, inr)))) {
		return -EACCES;
	}
	// 若该 inode 对应的是一个目录, 并且访问模式不是只读(O_RDONLY)(也就是说目录只能是只读?), 
	// 或者没有访问的许可权限, 则放回该 inode, 返回访问权限出错码退出.
	if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) || !permission(inode, ACC_MODE(flag))) {
		iput(inode);
		return -EPERM;
	}
	// 接着我们更新该 inode 的访问时间字段值为当前时间. 如果设立了截 0 标志, 则将该 inode 的文件长度截为 0. 
	// 最后返回该目录项 inode 的指针. 并返回 0(成功).
	inode->i_atime = CURRENT_TIME;
	if (flag & O_TRUNC) {
		truncate(inode);
	}
	*res_inode = inode;
	return 0;
}

// 创建一个设备特殊文件或普通文件节点(node). 
// 该函数创建名称为 filename, 由 mode 和 dev 指定的文件系统节点(普通文件, 设备特殊文件或命名管道). 
// 参数: filename - 路径名; mode - 指定使用许可以及所创建节点的类型; dev - 设备号. 
int sys_mknod(const char * filename, int mode, int dev) {
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先检查操作许可和参数的有效性并取路径名中顶层目录的 inode. 
	// 如果不是超级用户, 则返回访问许可出错码. 
	if (!suser()) {
		return -EPERM;
	}
	// 如果找不到对应路径名中顶层目录的 inode, 则返回出错码. 
	if (!(dir = dir_namei(filename, &namelen, &basename, NULL))) {
		return -ENOENT;
	}
	// 如果最顶端的文件名长度为 0, 则说明给出的路径名最后没有指定文件名, 放回该目录 inode, 返回出错码退出. 
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	// 如果在该目录中没有写的权限, 则放回该目录的 inode, 返回访问许可出错码退出. 
	if (!permission(dir, MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	// 然后我们搜索一下路径名指定的文件是否已经存在. 若已经存在则不能创建同名文件节点. 
	// 如果对应路径名上最后的文件名的目录项已经存在, 则释放包含该目录项的缓冲区块并放回目录的 inode, 
	// 返回文件已经存在的出错退出. 
	bh = find_entry(&dir, basename, namelen, &de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	// 否则我们就申请一个新的 inode, 并设置该 inode 的属性模式. 
	inode = new_inode(dir->i_dev);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_mode = mode;
	// 如果要创建的是块设备文件或者是字符设备文件, 则令 inode 的直接逻辑块指针 0 等于设备号. 
	// 即对于设备文件来说, 其 inode 的 i_zone[0] 中存放的是该设备文件所定义设备的设备号. 
	if (S_ISBLK(mode) || S_ISCHR(mode)) {
		inode->i_zone[0] = dev;
	}
	// 设置该 inode 的修改时间, 访问时间为当前时间, 并设置 inode 已修改标志. 
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	inode->i_dirt = 1;
	// 接着为这个新的 inode 在目录中新添加一个目录项. 如果失败(包含该目录项的高速缓冲块指针为 NULL), 则放回目录的i节点; 
	// 把所申请的 inode 引用连接计数复位, 并放回该 inode, 返回出错码退出. 
	bh = add_entry(dir, basename, namelen, &de);
	if (!bh) {
		iput(dir);
		inode->i_nlinks = 0;
		iput(inode);
		return -ENOSPC;
	}
	// 现在添加目录项操作也成功了, 于是我们来设置这个目录项内容. 令该目录项的 inode 字段等于新 inode 号, 
	// 并置高速缓冲区已修改标志, 放回目录和新的 inode, 释放高速缓冲区, 最后返回 0(成功). 
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

// 创建一个目录. 
// 参数: pathname - 路径名; mode - 目录使用的权限属性. 
// 返回: 成功则返回 0, 否则返回出错码. 
int sys_mkdir(const char * pathname, int mode) {
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh, *dir_block;
	struct dir_entry * de;

	// 首先检查参数的有效性并取路径名中顶层目录的 inode. 如果找不到对应路径名中顶层目录的 inode, 则返回出错码. 
	if (!(dir = dir_namei(pathname,&namelen,&basename, NULL))) {
		return -ENOENT;
	}
	// 如果最顶端文件名长度为 0, 则说明给出的路径名最后没有指定文件名, 放回该目录 inode, 返回出错码退出. 
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	// 如果在该目录中没有写的权限, 则放回该目录 inode, 返回访问许可出错码退出. 
	// 如果不是超级用户, 则返回访问许可出错码. 
	if (!permission(dir, MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	// 然后我们搜索一下路径名指定的目录名是否已经存在. 若已经存在则不能创建同名目录节点. 
	// 如果对应路径名上最后的目录名的目录项已经存在, 则释放包含该目录项的缓冲区块并放回目录的 inode, 
	// 返回文件已经存在的出错码退出. 否则我们就申请一个新的 inode, 
	// 并设置该 inode 的属性模式: 置该新 inode 对应的文件长度为 32 字节(2 个目录项的大小), 置节点已修改标志, 
	// 以及节点的修改时间和访问时间. 2 个目录项分别用于 '.' 和 '..' 目录. 
	bh = find_entry(&dir, basename, namelen, &de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = new_inode(dir->i_dev);
	if (!inode) {           						// 若不成功则放回目录的 inode, 返回无空间出错码. 
		iput(dir);
		return -ENOSPC;
	}
	inode->i_size = 32;
	inode->i_dirt = 1;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	// 接着为该新 inode 申请一用于保存目录项数据的磁盘块, 并令 inode 的第一个直接块指针等于该块号. 
	// 如果申请失败则放回对应目录的 inode; 复位新申请的 inode 连接计数; 放回该新的 inode, 返回没有空间出错码退出. 
	// 否则置该新的 inode 已修改标志. 
	if (!(inode->i_zone[0] = new_block(inode->i_dev))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ENOSPC;
	}
	inode->i_dirt = 1;
	// 从设备上读取新申请的磁盘块(目的是把对应块放到高速缓冲区中). 若出错, 则放回对应目录的 inode; 
	// 释放申请的磁盘块; 复位新申请的 inode 连接计数; 放回该新的 inode, 返回没有空间出错码退出. 
	if (!(dir_block = bread(inode->i_dev, inode->i_zone[0]))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ERROR;
	}
	// 然后我们在缓冲块中建立起所创建目录文件中的 2 个默认的新目录项('.' 和 '..')结构数据. 
	// 首先令 de 指向存放目录项的数据块, 然后置该目录项的 inode 号字段等于新申请的 inode 号, 
	// 名字字段等于 “.”. 然后 de 指向下一个目录项结构, 并在该结构中存放上级目录的 inode 号和名字 “..”. 
	// 然后设置该高速缓冲块已修改标志, 并释放该缓冲区块. 再初始化设置新 inode 的模式字段, 并置该 inode 已修改标志. 
	de = (struct dir_entry *)dir_block->b_data;
	de->inode = inode->i_num;         				// 设置 '.' 目录项. 
	strcpy(de->name, ".");
	de++;
	de->inode = dir->i_num;         				// 设置 '..' 目录项. 
	strcpy(de->name, "..");
	inode->i_nlinks = 2;
	dir_block->b_dirt = 1;
	brelse(dir_block);
	inode->i_mode = I_DIRECTORY | (mode & 0777 & ~current->umask);
	inode->i_dirt = 1;
	// 现在我们在指定目录中新添加一个目录项, 用于存放新建目录的 inode 和目录名. 
	// 如果失败(包含该目录项的高速缓冲区指针为 NULL), 则放回目录的 inode; 
	// 所申请的 inode 引用连接计数复位, 并放回该 inode. 返回出错码退出. 
	bh = add_entry(dir, basename, namelen, &de);
	if (!bh) {
		iput(dir);
		inode->i_nlinks = 0;
		iput(inode);
		return -ENOSPC;
	}
	// 最后令该新目录项的 inode 字段等于新 inode 号, 并置高速缓冲块已修改标志, 
	// 放回目录和新的 inode, 释放高速缓冲区, 最后返回 0(成功). 
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	dir->i_nlinks++;
	dir->i_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
/*
 * 用于检查指定的目录是否为空的子程序(用于 rmdir 系统调用). 
 */
// 检查指定目录是否为空. 
// 参数: inode - 指定目录的 inode 指针. 
// 返回: 1 - 目录中是空的; 0 - 不空. 
static int empty_dir(struct m_inode * inode) {
	int nr, block;
	int len;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先计算指定目录中现有目录项个数并检查开始两个特定目录项中信息是否正确. 
	// 一个目录中应该起码有 2 个目录项: 即 “.” 和 “..”. 
	// 如果目录项个数少于 2 个或者该目录 inode 的第 1 个直接块没有指向任何磁盘块号, 或者该直接块读不出, 
	// 则显示警告信息 “设备dev上目录错”, 返回 0(失败). 
	len = inode->i_size / sizeof(struct dir_entry);        		// 目录中目录项个数. 
	if (len < 2 || !inode->i_zone[0] || !(bh = bread(inode->i_dev, inode->i_zone[0]))) {
	    printk("warning - bad directory on dev %04x\n", inode->i_dev);
		return 0;
	}
	// 此时 bh 所指缓冲块中含有目录项数据. 我们让目录项指针 de 指向缓冲块中第 1 个目录项. 对于第 1 个目录项(“.”), 
	// 它的 inode 号字段 inode 应该等于当前目录的 inode 号. 对于第 2 个目录项(“..”), 
	// 节点号字段 inode 应该等于上一层目录的 inode 号, 不会为 0. 
	// 因此, 如果第 1 个目录项的 inode 号字段值不等于该目录的 inode 号, 或者第 2 个目录项的 inode 号字段为零, 
	// 或者两个目录项的名字字段不分别等于 “.” 和 “..”, 则显示出错警告信息 “设备 dev 上目录错”, 并返回 0. 
	de = (struct dir_entry *)bh->b_data;
	if (de[0].inode != inode->i_num || !de[1].inode || strcmp(".", de[0].name) || strcmp("..", de[1].name)) {
	    printk("warning - bad directory on dev %04x\n", inode->i_dev);
		return 0;
	}
	// 然后我们令 nr 等于目录项序号(从 0 开始计); de 指向第三个目录项. 
	// 并循环检测该目录中其余所有的(len - 2)个目录项, 看有没有目录项的 inode 号字段不为 0(被使用). 
	nr = 2;
	de += 2;
	while (nr < len) {
		// 如果该块磁盘块中的目录项已经全部检测完毕, 则释放该磁盘块的缓冲块, 并读取目录数据文件中下一块含有目录项的磁盘块. 
		// 读取的方法是根据当前检测的目录项序号 nr 计算出对应目录项在目录数据文件中的数据块号(nr / DIR_ENTRIES_PER_BLOCK), 
		// 然后使用 bmap() 函数取得对应的盘块号 block, 再使用读设备块函数 bread() 把相应盘块读入缓冲块中, 
		// 并返回该缓冲块的指针. 若所读取的相应盘块没有使用(或已经不用, 如文件已经删除等), 
		// 则继续读下一块, 若读不出, 则出错返回 0. 否则让 de 指向读出块的第 1 个目录项. 
		if ((void *) de >= (void *)(bh->b_data + BLOCK_SIZE)) {
			brelse(bh);
			block = bmap(inode, nr / DIR_ENTRIES_PER_BLOCK);
			if (!block) {
				nr += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			if (!(bh = bread(inode->i_dev, block))) {
				return 0;
			}
			de = (struct dir_entry *)bh->b_data;
		}
		// 对于 de 指向的当前目录项, 如果该目录项的 inode 号字段不等于 0, 
		// 则表示该目录项目前正被使用, 则释放该高速缓冲区, 返回 0 退出. 
		// 否则, 若还没有查询完该目录中的所有目录项, 则把目录项序号 nr 增 1, de 指向下一个目录项, 继续检测. 
		if (de->inode) {
			brelse(bh);
			return 0;
		}
		de++;
		nr++;
	}
	// 执行到这里说明该目录中没有找到已用的目录项(当然除了头两个以外), 则释放缓冲块返回 1. 
	brelse(bh);
	return 1;
}

// 删除目录. 
// 参数: name - 目录名(路径名). 
// 返回: 返回 0 表示成功, 否则返回出错号. 
int sys_rmdir(const char * name) {
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先检查参数的有效性并取路径名中顶层目录的 inode. 如果找不到对应路径名中顶层目录的 inode, 则返回出错码. 
	// 如果最顶端文件名长度为 0, 则说明给出的路径名最后没有指定文件名, 放回该目录 inode, 返回出错码退出. 
	// 如果在该目录中没有写的权限, 则放回该目录 inode, 返回访问许可出错码退出. 如果不是超级用户, 则返回访问许可出错码. 
	if (!(dir = dir_namei(name, &namelen, &basename, NULL))) {
		return -ENOENT;
	}
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	// 然后根据指定目录的 inode 和目录名利用函数 find_entry() 寻找对应目录项, 并返回包含该目录项的缓冲块指针 bh, 
	// 包含该目录项的目录的 inode 指针 dir 和该目录项指针 de. 
	// 再根据该目录项 de 中的 inode 号利用 iget() 函数得到对应的 inode  inode. 
	// 如果对应路径名上最后目录的名的目录项不存在, 则释放包含该目录项的高速缓冲区, 放回目录的 inode, 
	// 返回文件不存在出错码, 并退出. 
	// 如果取目录项的 inode 出错, 则放回目录的 inode, 并释放含有目录项的高速缓冲区, 返回出错号. 
	bh = find_entry(&dir, basename, namelen, &de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	// 此时我们已有包含要被删除目录项的目录 inode  dir, 要被删除目录项的 inode  inode 和要被删除目录项指针 de. 
	// 下面我们通过对这 3 个对象中信息的检查来验证删除操作的可行性. 
	// 若该目录设置了受限删除标志并且进程的有效用户 id(euid) 不是 root, 
	// 并且进程的有效用户 id(euid) 不等于该 inode 的用户 id, 则表示当前进程没有权限删除该目录, 
	// 于是放回包含要删除目录名的目录 inode 和该要删除目录的 inode, 然后释放高速缓冲区, 返回出错码. 
	if ((dir->i_mode & S_ISVTX) && current->euid && inode->i_uid != current->euid) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	// 如果要被删除的目录项 inode 的设备号不等于包含该目录项的目录的设备号, 
	// 或者该被删除目录的引用连接计数大于 1(表示有符号连接等), 则不能删除该目录. 
	// 于是释放包含要删除目录名的目录 inode 和该要删除目录的 inode, 释放高速缓冲块, 返回出错码. 
	if (inode->i_dev != dir->i_dev || inode->i_count > 1) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	// 如果要被删除目录的目录项 inode 就等于包含该需删除目录的目录 inode, 则表示试图删除 "." 目录, 这是不允许的. 
	// 于是放回包含要删除目录名的目录 inode 和要删除目录的 inode, 释放高速缓冲块, 返回出错码. 
	if (inode == dir) {						/* we may not delete ".", but "../dir" is ok */
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	// 若要被删除目录 inode 的属性表明这不是一个目录, 则本删除操作的前提完全不存在. 
	// 于是放回包含删除目录名的目录 inode 和该要删除目录的 inode, 释放高速缓冲块, 返回出错码. 
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTDIR;
	}
	// 若该需要被删除的目录不空, 则也不能删除. 
	// 于是放回包含要删除目录名的目录 inode 和该要删除目录的 inode, 释放高速缓冲块, 返回出错码. 
	if (!empty_dir(inode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTEMPTY;
	}
	// 对于一个空目录, 其目录项链接数应该为 2(链接到上层目录和本目录). 
	// 若该需被删除目录的 inode 的连接数不等于 2, 则显示警告信息, 但删除操作仍然执行. 
	// 于是置该需删除目录的目录项的 inode 号字段为 0, 表示该目录项不再使用, 
	// 并置含有该目录项的调整缓冲块已修改标志, 并释放该缓冲块. 
	// 然后再置被删除目录 inode 的链接数为 0(表示空闲), 并置 inode 已修改标志. 
	if (inode->i_nlinks != 2) {
		printk("empty directory has nlink!=2 (%d)", inode->i_nlinks);
	}
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	inode->i_nlinks = 0;
	inode->i_dirt = 1;
	// 再将包含被删除目录名的目录的 inode 链接计数减 1, 修改其改变时间和修改时间为当前时间, 并置该节点已修改标志. 
	// 最后放回包含要删除目录名的目录 inode 和该要删除目录的 inode, 返回 0(删除操作成功). 
	dir->i_nlinks--;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_dirt = 1;
	iput(dir);
	iput(inode);
	return 0;
}

// 删除(释放)文件名对应的目录项. 从文件系统删除一个名字. 
// 如果是文件的最后一个链接, 并且没有进程正打开该文件, 则该文件也将被删除, 并释放所占用的设备空间. 
// 参数: name - 文件名(路径名). 
// 返回: 成功则返回 0, 否则返回出错号. 
int sys_unlink(const char * name) {
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先检查参数的有效性并取路径名中顶层目录的 inode. 如果找不到对应路径名中顶层目录的 inode, 则返回出错码. 
	// 如果最顶端文件名长度为 0, 则说明给出的路径名最后没有指定文件名, 放回该目录 inode, 返回出错码退出. 
	// 如果在该目录中没有写的权限, 则放回该目录 inode, 返回访问许可出错码退出. 如果不是超级用户, 则返回访问许可出错码. 
	if (!(dir = dir_namei(name, &namelen, &basename, NULL))) {
		return -ENOENT;
	}
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir, MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	// 然后根据指定目录的 inode 和目录名利用函数 find_entry() 寻找对应目录项, 并返回包含该目录项的缓冲块指针 bh, 
	// 包含该目录项的目录的 inode 指针 dir 和该目录项指针 de. 
	// 再根据该目录项 de 中的 inode 号利用 iget() 函数得到对应的 inode  inode. 
	// 如果对应路径名上最后目录的名的目录项不存在, 则释放包含该目录项的高速缓冲区, 放回目录的 inode, 
	// 返回文件不存在出错码, 并退出. 
	// 如果取目录项的 inode 出错, 则放回目录的 inode, 并释放含有目录项的高速缓冲区, 返回出错号. 
	bh = find_entry(&dir, basename, namelen, &de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -ENOENT;
	}
	// 此时我们已有包含要被删除目录项的目录 inode  dir, 要被删除目录项的 inode  inode 和要被删除目录项指针 de. 
	// 下面我们通过对这 3 个对象中信息的检查来验证删除操作的可行性. 
	// 若该目录设置了受限删除标志并且进程的有效用户 id(euid) 不是 root, 
	// 并且进程的有效用户 id(euid) 不等于该 inode 的用户 id, 
	// 并且进程的 euid 也不等于目录 inode 的用户 id, 则表示当前进程没有权限删除该目录, 
	// 于是放回包含要删除目录名的目录 inode 和该要删除目录的 inode, 然后释放高速缓冲区, 返回出错码. 
	if ((dir->i_mode & S_ISVTX) && !suser() && current->euid != inode->i_uid && current->euid != dir->i_uid) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	// 如果该指定文件名是一个目录, 则也不能删除. 
	// 放回该目录 inode 和该文件名目录项的 inode, 释放包含该目录项的缓冲块, 返回出错号. 
	if (S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	// 如果该 inode 的链接计数值已经为 0, 则显示警告信息, 并修正其为 1. 
	if (!inode->i_nlinks) {
		printk("Deleting nonexistent file (%04x:%d), %d\n", inode->i_dev, inode->i_num, inode->i_nlinks);
		inode->i_nlinks = 1;
	}
	// 现在我们可以删除文件名对应的目录项了. 于是将该文件名目录项中的 inode 号字段置为 0, 
	// 表示释放该目录项, 并设置包含该目录项的缓冲块已修改标志, 释放该高速缓冲块. 
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	// 然后把文件名对应 inode 的链接数减 1, 置已修改标志, 更新改变时间为当前时间. 
	// 最后放回该 inode 和目录的 inode, 返回 0(成功). 
	// 如果是文件的最后一个链接, 即 inode 链接数减 1 后等于 0, 并且此时没有进程正打开该文件, 
	// 那么在调用 iput() 放回 inode 时, 该文件也将被删除并释放所占用的设备空间. 参见 fs/inode.c. 
	inode->i_nlinks--;
	inode->i_dirt = 1;
	inode->i_ctime = CURRENT_TIME;
	iput(inode);
	iput(dir);
	return 0;
}

// 建立符号链接. 
// 为一个已存在文件创建一个符号链接(也称为软连接 - hard link). 
// 参数: oldname - 原路径名; newname - 新的路径名. 
// 返回: 若成功则返回 0, 否则返回出错号. 
int sys_symlink(const char * oldname, const char * newname) {
	struct dir_entry * de;
	struct m_inode * dir, * inode;
	struct buffer_head * bh, * name_block;
	const char * basename;
	int namelen, i;
	char c;

	// 首先查找新路径名的最顶层目录的 inode, 并返回最后的文件名及其长度. 如果目录的 inode 没有找到, 则返回出错号. 
	// 如果新路径名中不包括文件名, 则放回新路径名目录的 inode, 返回出错号. 
	// 另外, 如果用户没有在新目录中写的权限, 则也不能建立连接, 于是放回新路径名目录的 inode, 返回出错号. 
	dir = dir_namei(newname, &namelen, &basename, NULL);
	if (!dir) {
		return -EACCES;
	}
	if (!namelen) {
		iput(dir);
		return -EPERM;
	}
	if (!permission(dir, MAY_WRITE)) {
		iput(dir);
		return -EACCES;
	}
	// 现在我们在目录指定设备上申请一个新的 inode, 并设置该 inode 模式为符号链接类型以及进程规定的模式屏蔽码. 
	// 并且设置该 inode 已修改标志. 
	if (!(inode = new_inode(dir->i_dev))) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_mode = S_IFLNK | (0777 & ~current->umask);
	inode->i_dirt = 1;
	// 为了保存符号链接路径名字符串信息, 我们需要为该 inode 申请一个磁盘块, 
	// 并让 inode 的第 1 个直接块号 i_zone[0] 等于得到的逻辑块号. 
	// 然后置 inode 已修改标志. 如果申请失败则放回对应目录的 inode; 
	// 复位新申请的 inode 链接计数; 放回该新的 inode, 返回没有空间出错码退出. 
	if (!(inode->i_zone[0] = new_block(inode->i_dev))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ENOSPC;
	}
	inode->i_dirt = 1;
	// 然后从设备上读取新申请的磁盘块(目的是把对应块放到高速缓冲区中). 
	// 若出错, 则放回对应目录的 inode ; 复位新申请的 inode 链接计数; 
	// 放回该新的 inode, 返回没有空间出错码退出. 
	if (!(name_block = bread(inode->i_dev, inode->i_zone[0]))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ERROR;
	}
	// 现在我们可以把符号链接名字字符串放入这个盘块中了. 
	// 盘块长度为 1024 字节, 因此默认符号链接名长度最大也只能是 1024 字节. 
	// 我们把用户空间中的符号链接名字符串复制到盘块所在的缓冲块中, 并置缓冲块已修改标志. 
	// 为防止用户提供的字符串没有以 NULL 结尾, 我们在缓冲块数据区最后一个字节处放上一个 NULL. 
	// 然后释放该缓冲块, 并设置 inode 对应文件中数据长度等于符号链接名字符串长度, 并置 inode 已修改标志. 
	i = 0;
	while (i < 1023 && (c = get_fs_byte(oldname++))) {
		name_block->b_data[i++] = c;
	}
	name_block->b_data[i] = 0;
	name_block->b_dirt = 1;
	brelse(name_block);
	inode->i_size = i;
	inode->i_dirt = 1;
	// 然后我们搜索一下路径名指定的符号链接名是否已经存在. 若已经存在则不能创建同名目录项 inode. 
	// 如果对应符号链接文件名已经存在, 则释放包含该目录项的缓冲区块, 复位新申请的 inode 连接计数, 
	// 并施加目录的 inode, 返回文件已经存在的出错码退出. 
	bh = find_entry(&dir, basename, namelen, &de);
	if (bh) {
		inode->i_nlinks--;
		iput(inode);
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	// 现在我们在指定目录中新添加一个目录项, 用于存放新建符号链接文件名的 inode 号和目录名. 
	// 如果失败(包含该目录项的高速缓冲区指针为 NULL), 则放回目录的 inode; 所申请的 inode 引用链接计数复位, 
	// 并放回该 inode. 返回出错码退出. 
	bh = add_entry(dir, basename, namelen, &de);
	if (!bh) {
		inode->i_nlinks--;
		iput(inode);
		iput(dir);
		return -ENOSPC;
	}
	// 最后令该新目录项的 inode 字段等于新 inode 号, 并置高速缓冲块已修改标志, 
	// 释放高速缓冲块, 放回目录和新的 inode, 最后返回 0(成功). 
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	brelse(bh);
	iput(dir);
	iput(inode);
	return 0;
}

// 为文件建立一个文件名目录项. 
// 为一个已存在的文件创建一个新链接(也称为硬连接 - hard link). 
// 参数: oldname - 原路径名; newname - 新的路径名. 
// 返回: 若成功则返回 0, 否则返回出错号. 
int sys_link(const char * oldname, const char * newname) {
	struct dir_entry * de;
	struct m_inode * oldinode, * dir;
	struct buffer_head * bh;
	const char * basename;
	int namelen;

	// 首先对原文件名进行有效性验证, 它应该存在并且不是一个目录名. 所以我们先取原文件路径名对应的 inode(oldinode). 
	// 如果为0, 则表示出错, 返回出错号. 如果原路径名对应的是一个目录名, 则放回该 inode, 也返回出错号. 
	oldinode = namei(oldname);
	if (!oldinode) return -ENOENT;

	if (S_ISDIR(oldinode->i_mode)) {
		iput(oldinode);
		return -EPERM;
	}
	// 然后查找新路径名的最顶层目录的 inode  dir, 并返回最后的文件名及其长度. 
	// 如果目录的 inode 没有找到, 则放回原路径名的 inode, 返回出错号. 
	// 如果新路径名中不包括文件名, 则放回原路径名 inode 和新路径名目录的 inode, 返回出错号. 
	dir = dir_namei(newname, &namelen, &basename, NULL);
	if (!dir) {
		iput(oldinode);
		return -EACCES;
	}
	if (!namelen) {
		iput(oldinode);
		iput(dir);
		return -EPERM;
	}
	// 我们不能跨设备建立硬链接. 因此如果新路径名顶层目录的设备号与原路径名的设备号不一样, 
	// 则放回新路径名目录的 inode 和原路径名的 inode, 返回出错号. 
	// 另外, 如果用户没有在新目录中写的权限, 则也不能建立连接, 
	// 于是放回新路径名目录的 inode 和原路径名的 inode, 返回出错号. 
	if (dir->i_dev != oldinode->i_dev) {
		iput(dir);
		iput(oldinode);
		return -EXDEV;
	}
	if (!permission(dir, MAY_WRITE)) {
		iput(dir);
		iput(oldinode);
		return -EACCES;
	}
	// 现在查询该新路径名是否已经存在, 如果存在则也不能建立链接. 于是释放包含该已存在目录项的高速缓冲块, 
	// 放回新路径名目录的 inode 和原路径名的 inode, 返回出错号. 
	bh = find_entry(&dir, basename, namelen, &de);
	if (bh) {
		brelse(bh);
		iput(dir);
		iput(oldinode);
		return -EEXIST;
	}
	// 现在所有条件都满足了, 于是我们在新目录中添加一个目录项. 
	// 若失败则放回该目录的 inode 和原路径名的 inode, 返回出错号. 
	// 否则初始设置该目录项的 inode 号等于原路径名的 inode 号, 
	// 并置包含该新添目录的缓冲块已修改标志, 释放该缓冲块, 放回目录的 inode. 
	bh = add_entry(dir, basename, namelen, &de);
	if (!bh) {
		iput(dir);
		iput(oldinode);
		return -ENOSPC;
	}
	de->inode = oldinode->i_num;
	bh->b_dirt = 1;
	brelse(bh);
	iput(dir);
	// 再将原节点的链接计数加 1, 修改其改变时间为当前时间, 并设置 inode 已修改标志. 
	// 最后放回原路径名的 inode, 并返回 0(成功). 
	oldinode->i_nlinks++;
	oldinode->i_ctime = CURRENT_TIME;
	oldinode->i_dirt = 1;
	iput(oldinode);
	return 0;
}