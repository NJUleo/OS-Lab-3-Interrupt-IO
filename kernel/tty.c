
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


//#define AUTO_CLEAR_TTY

PRIVATE void init_tty(TTY* p_tty);
PRIVATE void tty_do_read(TTY* p_tty);
PRIVATE void tty_do_write(TTY* p_tty);
PRIVATE void put_key(TTY* p_tty, u32 key);
PRIVATE void clear_tty(TTY* p_tty);
PRIVATE void tty_do_handle(TTY* p_tty);

//输入的和要搜索匹配的字符全部放置在这个文件当中。按道理说应该是每个终端单独存放对应的状态和缓冲，基本就是面向对象的思想，这里做个简化，既然不要求多终端就写再这个文件当中就可以了

// 当前状态，MODE_INPUT或MODE_SEARCH
PRIVATE int current_mode;

//输入缓冲，没要求翻页，只考虑一页。
PRIVATE char input_buf[80 * 25];
//搜索字符串缓冲
PRIVATE char search_buf[80 * 25];
//指向下一个输入字符所在的位置
PRIVATE int char_now;
//每一行的长度，用于对换行进行统一回退
PRIVATE int line_length[25];
//当前正在写的行
PRIVATE int line_now;
//发生改变
PRIVATE int changed;
//console中改变了的。
PRIVATE int char_console;



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


	while (1) {
		for (p_tty=TTY_FIRST;p_tty<TTY_END;p_tty++) {
			// 处在输入模式并且超过20s则清屏（输出\b来清屏……）
			// 不知道原作者怎么计算这个ms的，1s难道不是1000ms？
			// @See [[kernal/clock.c]]
#ifdef AUTO_CLEAR_TTY
			int current_time = get_ticks();
			if (current_mode == MODE_INPUT &&
				((current_time - time_counter) * 1000 / HZ) > 20 * 10000)
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
				  从tty缓存中读取字符，按照真正得状况存储进input_buf中
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
	

		//out_char(p_tty->p_console, ch);
	}
}


/*======================================================================*
			      tty_do_write
				  从tty缓存中将字符放入console
 *======================================================================*/
PRIVATE void tty_do_write(TTY* p_tty)
{
	// if(changed){
	// 	for (int i = 0; i < 80 * 25 * 2; ++i){
	// 		out_char(p_tty->p_console, '\b');
	// 	}
	// 	for(int i = 0; i < char_now; i++){
	// 		out_char(p_tty->p_console, input_buf[i]);
	// 	}
	// 	changed = 0;
	// }
	if(char_console > char_now){
		char_console--;
		out_char(p_tty->p_console, '\b');
	}else if(char_console < char_now){
		while(char_console < char_now){
			out_char(p_tty->p_console, input_buf[char_console]);
			char_console++;
		}
	}

}

/*======================================================================*
			      clear_tty
				  清空这个tty得所有字符
 *======================================================================*/
PRIVATE void clear_tty(TTY* p_tty){
	char_now = 0;
	line_now = 0;
	changed = 1;
	char_console = 0;
	for (int i = 0; i < 80 * 25 * 2; ++i)
	{
		out_char(p_tty->p_console, '\b');
	}
	for(int i = 0; i < 25; i++){
		line_length[i] = 0;
	}
	for(int i = 0; i < 80 * 25; i++){
		input_buf[i] = '\0';
		search_buf[i] = '\0';
	}
}


