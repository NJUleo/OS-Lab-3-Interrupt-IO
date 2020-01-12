
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               tty.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "keyboard.h"
#include "proto.h"

#define TTY_FIRST	(tty_table)
#define TTY_END		(tty_table + NR_CONSOLES)
#define MODE_INPUT 0
#define MODE_SEARCH 1
#define MODE_SHOW -1
#define MODE_SHOWED 2
#define MODE_UNSHOW 3


//#define AUTO_CLEAR_TTY

PRIVATE void init_tty(TTY* p_tty);
PRIVATE void tty_do_read(TTY* p_tty);
PRIVATE void tty_do_write(TTY* p_tty);
PRIVATE void put_key(TTY* p_tty, u32 key);
PRIVATE void clear_tty(TTY* p_tty);
PRIVATE void tty_do_handle(TTY* p_tty);
PRIVATE void clear_tty_search(TTY* p_tty);
PRIVATE int str_match(char* s1, char* s2);
PRIVATE void show_match_str(TTY* p_tty);
PRIVATE int str_size(char* str);
PRIVATE void unshow_match_str(TTY* p_tty);


//输入的和要搜索匹配的字符全部放置在这个文件当中。按道理说应该是每个终端单独存放对应的状态和缓冲，基本就是面向对象的思想，这里做个简化，既然不要求多终端就写再这个文件当中就可以了

// 当前状态，MODE_INPUT或MODE_SEARCH
PRIVATE int current_mode;

//输入缓冲，没要求翻页，只考虑一页。
PRIVATE char input_buf[80 * 25];
//搜索字符串缓冲
PRIVATE char search_buf[80 * 25];
//指向下一个输入字符所在的位置
PRIVATE int char_now;
//search mode指向下一个输入字符所在的位置
PRIVATE int char_now_search;
//每一行的长度，用于对换行进行统一回退
PRIVATE int line_length[25];
//当前正在写的行
PRIVATE int line_now;
//发生改变
PRIVATE int changed;
//console中改变了的。
PRIVATE int char_console;

//存放command
PRIVATE COMMAND_QUEUE commands;


/*======================================================================*
                           task_tty
 *======================================================================*/
PUBLIC void task_tty()
{
	TTY*	p_tty;

	init_keyboard();

	for (p_tty=TTY_FIRST;p_tty<TTY_END;p_tty++) {
		init_tty(p_tty);
	}
	select_console(0);

	// 初始为输入模式
	current_mode = MODE_INPUT;
	// 开始计时
	int time_counter = get_ticks();
	//初始化所有的command
	for(int i = 0; i < 100; i++){
		commands.commands[i].input = '\0';
		commands.commands[i].delete_char = '\0';
	}
	commands.size = 0;
	commands.


	while (1) {
		for (p_tty=TTY_FIRST;p_tty<TTY_END;p_tty++) {
			// 处在输入模式并且超过20s则清屏（输出\b来清屏……）
			// 不知道原作者怎么计算这个ms的，1s难道不是1000ms？
			// @See [[kernal/clock.c]]
#ifdef AUTO_CLEAR_TTY
			int current_time = get_ticks();
			if (current_mode == MODE_INPUT &&
				((current_time - time_counter) * 1000 / HZ) > 10 * 10000)
			{
				clear_tty(p_tty);
				// 重置计时器
				// 但是可以预见，这种方式的误差会越来越大，因为调用需要时间
				time_counter = current_time;
			}
#endif
			tty_do_read(p_tty);
			tty_do_handle(p_tty);
			tty_do_write(p_tty);

		}
	}
}

/*======================================================================*
			   init_tty
 *======================================================================*/
PRIVATE void init_tty(TTY* p_tty)
{
	p_tty->inbuf_count = 0;
	p_tty->p_inbuf_head = p_tty->p_inbuf_tail = p_tty->in_buf;

	init_screen(p_tty);
	clear_tty(p_tty);
}

/*======================================================================*
				in_process
				由keyboard调用，做一些处理之后放入tty缓存
 *======================================================================*/
PUBLIC void in_process(TTY* p_tty, u32 key)
{
        char output[2] = {'\0', '\0'};

        if (!(key & FLAG_EXT)) {
			if((key & FLAG_CTRL_L) && ((key & 0xFF) == 'z')){
				//ctrl + z
				if(current_mode == MODE_INPUT){
					//只在input状态下有撤销功能
					put_key(p_tty, 'c');
				}
				return;
			}
			
		put_key(p_tty, key);
        }
        else {
			int raw_code = key & MASK_RAW;
			switch(raw_code) {
				case ENTER:
					put_key(p_tty, '\n');
					break;
				case BACKSPACE:
					put_key(p_tty, '\b');
					break;
				case TAB:
					put_key(p_tty, '\t');
				case UP:
					if ((key & FLAG_SHIFT_L) || (key & FLAG_SHIFT_R)) {
						scroll_screen(p_tty->p_console, SCR_DN);
					}
					break;
				case ESC:
					switch(current_mode){
					case MODE_INPUT:
						current_mode = MODE_SEARCH;
						break;
					case MODE_SHOWED:
						current_mode = MODE_UNSHOW;
					}
					break;
				case DOWN:
					if ((key & FLAG_SHIFT_L) || (key & FLAG_SHIFT_R)) {
						scroll_screen(p_tty->p_console, SCR_UP);
					}
					break;
				case F1:
				case F2:
				case F3:
				case F4:
				case F5:
				case F6:
				case F7:
				case F8:
				case F9:
				case F10:
				case F11:
				case F12:
					/* Alt + F1~F12 */
					if ((key & FLAG_ALT_L) || (key & FLAG_ALT_R)) {
						select_console(raw_code - F1);
					}
					break;
				default:
						break;
			}
        }
}

/*======================================================================*
			      put_key
				  将字符放入tty缓冲
*======================================================================*/
PRIVATE void put_key(TTY* p_tty, u32 key)
{
	if (p_tty->inbuf_count < TTY_IN_BYTES) {
		*(p_tty->p_inbuf_head) = key;
		p_tty->p_inbuf_head++;
		if (p_tty->p_inbuf_head == p_tty->in_buf + TTY_IN_BYTES) {
			p_tty->p_inbuf_head = p_tty->in_buf;
		}
		p_tty->inbuf_count++;
	}
}


/*======================================================================*
			      tty_do_read
 *======================================================================*/
PRIVATE void tty_do_read(TTY* p_tty)
{
	if (is_current_console(p_tty->p_console)) {
		keyboard_read(p_tty);
	}
}

/*======================================================================*
			      tty_do_handle
				  从tty缓存中读取字符，封装为命令结构，方便之后逆转，按照真正得状况存储进input_buf中
 *======================================================================*/
PRIVATE void tty_do_handle(TTY* p_tty){
	if (p_tty->inbuf_count) {
		changed = 1;
		char ch = *(p_tty->p_inbuf_tail);
		p_tty->p_inbuf_tail++;
		if (p_tty->p_inbuf_tail == p_tty->in_buf + TTY_IN_BYTES) {
			p_tty->p_inbuf_tail = p_tty->in_buf;
		}
		p_tty->inbuf_count--;

		//下一此应该写的位置,用于baspace
		int char_now_next = 0;
		int char_now_next_search = 0;


		switch(current_mode){
		case MODE_INPUT:
			switch(ch){
			case '\n':
				line_now++;
				char_now = line_now * 80;
				break;
			case '\b':
				//如果要更换行，则删除到上一行的结束位置。如果是\t,删除四个
				if(line_now * 80 == char_now){
					//下一个要写的字符恰好是当前行的第一个字符，说明向上一行
					line_now--;
					line_now = (line_now < 0) ? 0 : line_now;
					char_now = line_now * 80 + line_length[line_now];
					if(line_length[line_now] == 80){
						//上一行是满的
						char_now--;
						input_buf[char_now] = '\0';
					}
					break;
				}else if(input_buf[char_now - 1] == '\t'){
					//tab
					char_now_next = char_now - 4;
				}else{
					//普通字符且不需要换行
					char_now_next = char_now - 1;
				}

				//防止越界
				char_now_next = (char_now_next < 0) ? 0 : char_now_next;
				while(char_now > char_now_next){
					char_now--;
					line_length[line_now] -= 1;
					input_buf[char_now] = '\0';
				}
				break;
			case '\t':
			//tab会在其中键入四个\t，这里暂时不考虑在其中换行的情况
				input_buf[char_now + 3] = input_buf[char_now + 2] = input_buf[char_now + 1] = input_buf[char_now] = ch;
				char_now += 4;
				line_length[line_now] += 4;
				// if((line_now + 1) * 80 < char_now){
				// 	//下次要写的字符的位置位于下一行
				// 	line_now++;
				// 	// line_length[line_now] += 1;
				// }
				break;
			case 27:
			//切换输入状态
				out_char(p_tty->p_console, 'B');
				current_mode = MODE_SEARCH;
				break;
			default:
			//普通字符
				input_buf[char_now] = ch;
				char_now++;
				line_length[line_now] += 1;
				if((line_now + 1) * 80 < char_now){
					//下次要写的字符的位置位于下一行
					line_now++;
				}
				break;
			}
			break;
		case MODE_SHOW:
			//除非esc否则不会发生改变
			break;
		case MODE_SEARCH:
		//和input基本一致，区别是将内容写在search_buf，没有行的概念
			switch(ch){
			case '\n':
				current_mode = MODE_SHOW;
				break;
			case '\b':
				//如果是\t,删除四个
				if(search_buf[char_now_search - 1] == '\t'){
					//tab
					char_now_next_search = char_now_search - 4;
				}else{
					//普通字符且不需要换行
					char_now_next_search = char_now_search - 1;
				}

				//防止越界
				char_now_next_search = (char_now_next_search < 0) ? 0 : char_now_next_search;
				while(char_now_search > char_now_next_search){
					char_now_search--;
					search_buf[char_now_search] = '\0';
				}
				break;
			case '\t':
			//tab会在其中键入四个\t，这里暂时不考虑在其中换行的情况
				search_buf[char_now_search + 3] = search_buf[char_now_search + 2] = search_buf[char_now_search + 1] = search_buf[char_now_search] = ch;
				char_now_search += 4;
				break;
			case ESC:
			//切换输入状态
					//current_mode = MODE_SEARCH;
				break;
			default:
			//普通字符
				search_buf[char_now_search] = ch;
				char_now_search++;
				break;
			}
			break;
		}
		
	

		//out_char(p_tty->p_console, ch);
	}
}


/*======================================================================*
			      tty_do_write
				  从tty缓存中将字符放入console
 *======================================================================*/
PRIVATE void tty_do_write(TTY* p_tty)
{
	switch (current_mode){
	case MODE_INPUT:
	//这里需要保证每次只能前进或者后退
		if(char_console > char_now){
			char_console--;
			out_char(p_tty->p_console, '\b');
		}else if(char_console < char_now){
			while(char_console < char_now){
				out_char(p_tty->p_console, input_buf[char_console]);
				char_console++;
			}
		}
		break;
	case MODE_SEARCH:
		if(char_console > char_now + char_now_search){
			char_console--;
			out_char(p_tty->p_console, '\b');
		}else if(char_console < char_now + char_now_search){
			while(char_console < char_now + char_now_search){
				out_char_color(p_tty->p_console, search_buf[char_console - char_now], RED_CHAR_COLOR);
				char_console++;
			}
		}
		break;
	case MODE_SHOW:
		show_match_str(p_tty);
		current_mode = MODE_SHOWED;
		break;
	case MODE_SHOWED:
	//只能等待esc
		break;
	case MODE_UNSHOW:
		unshow_match_str(p_tty);
		current_mode = MODE_INPUT;
		break;
	default:
		break;
	}


}

/*======================================================================*
			      clear_tty
				  清空这个tty得所有字符
 *======================================================================*/
PRIVATE void clear_tty(TTY* p_tty){
	char_now = 0;
	line_now = 0;
	char_console = 0;
	clear_tty_search(p_tty);
	for (int i = 0; i < 80 * 25 * 2; ++i)
	{
		out_char(p_tty->p_console, '\b');
	}
	for(int i = 0; i < 25; i++){
		line_length[i] = 0;
	}
	for(int i = 0; i < 80 * 25; i++){
		input_buf[i] = '\0';
	}
}

PRIVATE void clear_tty_search(TTY* p_tty){
	char_now_search = 0;
	for(int i = 0; i < 80 * 25; i++){
		search_buf[i] = '\0';
	}
}

PRIVATE int str_match(char* s1, char* s2){
	while(*s2 != '\0'){
		if(*s1 != *s2){
			return 0;
		}
		s1++;
		s2++;
	}
	return 1;
}

PRIVATE void show_match_str(TTY* p_tty){
	char* ptr = input_buf;
	char* now = ptr;
	char* ptr_search_str;
	while(now != ptr + char_now){
		if(str_match(now, search_buf)){
			set_str_color(p_tty->p_console, now - ptr, str_size(search_buf), RED_CHAR_COLOR);
			now += 1;

		}else{
			//out_char(p_tty->p_console, *now);
			now++;
		}
	}
}

PRIVATE void unshow_match_str(TTY* p_tty){
		set_str_color(p_tty->p_console, 0, char_now, DEFAULT_CHAR_COLOR);
		clear_tty_search(p_tty);
}

PRIVATE int str_size(char* ptr){
	int result = 0;
	while(*ptr != '\0'){
		ptr++;
		result++;
	}
	return result;
}

//入队
PRIVATE void command_enqueue(COMMAND_QUEUE* queue, COMMAND* command){
	//其实不需要这个函数，所有的添加都在new_command()进行
}
//出队
PRIVATE COMMAND* command_dequeue(COMMAND_QUEUE* queue){
	if(queue->size == 0){
		return 0;
	}
	COMMAND* result = queue->front;
	queue->front++;
	if(queue->front - queue->commands == 100){
		queue->front = queue->commands;
	}
	return result;


}

//new一个command，加入队列中，实际上是把队尾的command修改
PRIVATE COMMAND* new_command(COMMAND_QUEUE* queue, char input, char delete_char){
	if(queue->size == 100){
		return;
	}
	COMMAND* command = queue->end;
	command->delete_char = delete_char;
	command->input = input;
	queue->end++;
	if(queue->end - queue->commands == 100){
		queue->end = queue->commands;
	}

}

