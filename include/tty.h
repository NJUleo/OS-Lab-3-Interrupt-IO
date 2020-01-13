
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
				tty.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef _ORANGES_TTY_H_
#define _ORANGES_TTY_H_


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

//一条命令
typedef struct s_command{
	//这条命令的字符，可能是普通字符、\t、\b
	char input;
	//如果是删除命令，需要记录删除的字符，同样可能是普通字符、\t、\b
	char delete_char;
}COMMAND;

//指令队列
typedef struct s_command_queue{
	COMMAND commands[100];
	//COMMAND* front;//指向当前命令
	COMMAND* end;//指向下一个命令的位置
	int size;//目前拥有的命令个数
}COMMAND_QUEUE;

//入队
PRIVATE void command_enqueue(COMMAND_QUEUE* ptr_queue, COMMAND* ptr_command);
//获得当前的command
PRIVATE COMMAND* get_now_command(COMMAND_QUEUE* ptr_queue);
//new一个command，加入队列中
PRIVATE COMMAND* new_command(COMMAND_QUEUE* queue, char input, char delete_char);

#endif /* _ORANGES_TTY_H_ */
