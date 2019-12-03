# OS-Lab-3-Interrupt-IO 实验报告

3 4 5 7

### 保护模式

cs ds等指向GDT表

GDT表项定义段的起始地址、界限、属性等。

段寄存器和GDT中的描述符共同提供段式存储

Descriptor 数据结构就是描述符

选择子 包含描述符的相对GDT基址的偏移量和其他的一些信息

gdtr 包含GDT基址和界限

门描述符，描述选择子和一个偏移指定的线性地址，通过这个地址进行转移

### 8042

```ass
in al, 0x60
```

通过0x60端口获得输出缓冲区内容

```C
in_byte(0x60);
```

| 寄存器名称 | 端口 | R/W  | 用法         |
| ---------- | ---- | ---- | ------------ |
| 输出缓冲区 | 0x60 | R    | 读输出缓冲区 |
| 输入缓冲区 | 0x60 | W    | 写输入缓冲区 |
| 状态寄存器 | 0x64 | R    | 读状态寄存器 |
| 控制寄存器 | 0x64 | W    | 发送命令     |

Break Code是Make Code 和0x80 或 的结果

使用表驱动的方法把BC MC对应到字符 /include/keymap.h

keymap[0x1E * MAP_COLS]为字符

0x2A是shift，keymap[0x1E * MAP_COLS + 1]表示

0xE0， +2

每一个字符依次终端

使用缓冲区，将扫描码放进去，之后再对它进行处理

#### 缓冲区



```C
/* Keyboard structure, 1 per console. */
typedef struct s_kb {
	char*	p_head;			/* 指向缓冲区中下一个空闲位置 */
	char*	p_tail;			/* 指向键盘任务应处理的字节（最早来的字节） */
	int	count;			/* 缓冲区中共有多少字节 */
	char	buf[KB_IN_BYTES];	/* 缓冲区 32字节 */
}KB_INPUT;
```

循环队列的数据结构

```C

PRIVATE KB_INPUT	kb_in;

/*======================================================================*
                            keyboard_handler
 *======================================================================*/
PUBLIC void keyboard_handler(int irq)
{
	u8 scan_code = in_byte(KB_DATA);

	if (kb_in.count < KB_IN_BYTES) {
		*(kb_in.p_head) = scan_code;
		kb_in.p_head++;
		if (kb_in.p_head == kb_in.buf + KB_IN_BYTES) {
			kb_in.p_head = kb_in.buf;
		}
		kb_in.count++;
	}
}
```

超过缓冲区还未处理，则直接丢弃。PRIVATE就是static

### 显示

不同终端使用不同的显存位置

80 * 25 文本模式，每两个字节一个字符。低8位ascii码，高8位属性

每个终端占据显存空间4000字节

通过端口传送数据操作寄存器控制显示。

Start Address High Register 和 Start Address Low Register设置从显存的某个位置开始显示

### tty

为每个终端创立缓冲区，对读入的字符由tty自行处理。同样也是循环队列的结构

光标、显示位置、控制台显存等，由console结构记录

```C
#define TTY_IN_BYTES	256	/* tty input queue size */

struct s_console;

/* TTY */
typedef struct s_tty
{
	u32	in_buf[TTY_IN_BYTES];	/* TTY 输入缓冲区 */
	u32*	p_inbuf_head;		/* 指向缓冲区中下一个空闲位置 */
	u32*	p_inbuf_tail;		/* 指向键盘任务应处理的键值 */
	int	inbuf_count;		/* 缓冲区中已经填充了多少 */

	struct s_console *	p_console;
}TTY;
```

```C
typedef struct s_console
{
	unsigned int	current_start_addr;	/* 当前显示到了什么位置	  */
	unsigned int	original_addr;		/* 当前控制台对应显存位置 */
	unsigned int	v_mem_limit;		/* 当前控制台占的显存大小 */
	unsigned int	cursor;			/* 当前光标位置 */
}CONSOLE;
```

### 还需要实现的功能

1. Tab和空格的输入和删除（Tab统一删除，回车要回到上一次的光标位置而不是上以一行的最后）

2. 20秒清空屏幕，回到左上角进行输入

3. ESC查找模式
   1. 不清空屏幕
   2. 关键字用彩色显示
   3. 按下回车，查找（区分大小写）匹配文本，并修改颜色，屏蔽esc之外的案件
   4. 再按下esc，关键字删除，文本颜色恢复，光标恢复，回到输入模式
   
   * **搜索的Tab和四个空格不同**：可能不能直接去到显存里面进行匹配。是否意味着要维护一个能够体现出tab和空格区别的数据结构来单独存放？这是否太sb？
   
4. 输入模式下ctrl Z撤销（查找模式下没有Ctrl Z）：

   1. 普通字符
   2. 回车和tab
   3. 删除字符

 

用一段缓存来存放当前输入的字符。