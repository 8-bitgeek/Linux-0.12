/*
 * linux/kernel/math/div.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * temporary real division routine.
 */

#include <linux/math_emu.h>

// 将指针 c 指向的 4 字节中内容左移 1 位. 
static void shift_left(int * c) {
	__asm__ __volatile__("movl (%0),%%eax; addl %%eax,(%0)\n\t"
		"movl 4(%0),%%eax; adcl %%eax,4(%0)\n\t"
		"movl 8(%0),%%eax; adcl %%eax,8(%0)\n\t"
		"movl 12(%0),%%eax; adcl %%eax,12(%0)"
		::"r" ((long) c):"ax");
}

// 将指针 c 指向的 4 字节中内容右移 1 位. 
static void shift_right(int * c) {
	__asm__("shrl $1,12(%0); rcrl $1,8(%0); rcrl $1,4(%0); rcrl $1,(%0)"
		::"r" ((long) c));
}

// 减法运算. 
// 16 字节减法运算, b-a ->a. 最后根据是否有借位(CF = 1)设置 OK. 若无借位(CF = 0)则 ok = 1. 否则 ok = 0. 
static int try_sub(int * a, int * b) {
	char ok;

	__asm__ __volatile__("movl (%1),%%eax; subl %%eax,(%2)\n\t"
		"movl 4(%1),%%eax; sbbl %%eax,4(%2)\n\t"
		"movl 8(%1),%%eax; sbbl %%eax,8(%2)\n\t"
		"movl 12(%1),%%eax; sbbl %%eax,12(%2)\n\t"
		"setae %%al":"=a" (ok):"c" ((long) a),"d" ((long) b));
	return ok;
}

// 16 字节除法. 
// 参数 a/b -> c. 利用减法模拟多字节除法. 
static void div64(int * a, int * b, int * c) {
	int tmp[4];     			// 余数变量. 
	int i;
	unsigned int mask = 0;  	// 操作位. 

	c += 4;
// 16 字节共 64 位. 
	for (i = 0; i < 64; i++) {
		if (!(mask >>= 1)) {
			c--;
			mask = 0x80000000;
		}
// 把余数值 tmp 初始化为 a 值. 
		tmp[0] = a[0]; tmp[1] = a[1];
		tmp[2] = a[2]; tmp[3] = a[3];
		if (try_sub(b, tmp)) {           // 是否有借位. 
			*c |= mask;             	// 如果无借位, 置当前操作位, 把余数存入 a, 用于下次操作. 
			a[0] = tmp[0]; a[1] = tmp[1];
			a[2] = tmp[2]; a[3] = tmp[3];
		}
		shift_right(b);         		// 右移一位, 使 b 的值与值数处于同一级别. 
	}
}

// 仿真浮点指令 FDIV. 
// 临时实数 src1 / src2 -> result 处. 
void fdiv(const temp_real * src1, const temp_real * src2, temp_real * result) {
	int i, sign;
	int a[4], b[4], tmp[4] = {0, 0, 0, 0};

// 首先确定两个数相除的符号. 符号值等于两者符号位异或值. 然后判断除数 src2 值是否为 0, 如果是, 则置被零除异常. 
	sign = (src1->exponent ^ src2->exponent) & 0x8000;
	if (!(src2->a || src2->b)) {
		set_ZE();               // 置被零除异常. 
		return;
	}
// 然后计算除后的指数值. 相除时指数值需要相减. 
// 但是由于指数使用偏置格式保存, 两个数的指数相减时偏置量也被减去了, 因此需要加上偏置量值（临时实数的偏置量是 16383）. 
    i = (src1->exponent & 0x7fff) - (src2->exponent & 0x7fff) + 16383;
// 如果结果指数变成了负值, 表示两数相除后产生下溢. 于是直接返回带符号的零值. 
	if (i<0) {
		set_UE();
		result->exponent = sign;        // 设置符号位. 
		result->a = result->b = 0;      // 设置返回值为 0. 
		return;
	}
// 把临时实数 src1, src2 有效数放入整型数组 a 和 b. 
	a[0] = a[1] = 0;
	a[2] = src1->a;
	a[3] = src1->b;
	b[0] = b[1] = 0;
	b[2] = src2->a;
	b[3] = src2->b;
// 如果 b[3] 大于等于 0, 则进行规格化处理, 即对 b 进行左移调整 b[3] 为负数. 
	while (b[3] >= 0) {
		i++;
		shift_left(b);
	}
// 进行 64 位除法操作. 
	div64(a,b,tmp);
// 如果除结果 tmp[0], tmp[1], tmp[2] 和 tmp[3] 都为 0 的话, 说明结果为 0, 则设置指数 i 为 0. 否则进行规格化处理. 
	if (tmp[0] || tmp[1] || tmp[2] || tmp[3]) {
		while (i && tmp[3] >= 0) {      // 进行规格化处理. 
			i--;
			shift_left(tmp);
		}
		if (tmp[3] >= 0) {               // 如果 tmp[3] 大于等于 0, 设置状态字非格式化异常标志位. 
			set_DE();
		}
	} else {
		i = 0;          				// 设置结果指数为 0. 
	}
// 如果结果指数大于 0x7fff, 表示产生上溢, 于是设置状态字溢出异常标志位, 并返回. 
	if (i > 0x7fff) {
		set_OE();
		return;
	}
// 如果 tmp[0] 或 tmp[1] 不为 0, 则设置状态字精度异常标志位
	if (tmp[0] || tmp[1]) {
		set_PE();
	}
	result->exponent = i | sign;            // 设置返回临时实数的符号位和指数值. 
	result->a = tmp[2];                     // 设置返回临时实数的有效值. 
	result->b = tmp[3];
}
