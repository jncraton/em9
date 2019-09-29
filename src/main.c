#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <sys/ioctl.h>
#include <termios.h>

#include "keyboard.h"

#define O_BINARY 0

#define LINE_LENGTH    88
#define MAX_LINE_BYTES 128
#define MAX_LINES      16384 

#define MAXSIZE        32768

#define TABSIZE        2
#define PAGESIZE       20
#define INDENT         "  "

#define CLRSCR         "\033[0J"
#define CLREOL         "\033[K"
#define GOTO_LINE_COL  "\033[%d;%dH"
#define RESET_COLOR    "\033[0m"

#define TEXT_COLOR     "\033[0m"
#define SELECT_COLOR   "\033[7m\033[1m"
#define STATUS_COLOR   "\033[1m\033[7m"

struct editor {
  int clipsize;

  int cols;          // Console columns
  int lines;         // Console lines

  char line_contents[MAX_LINES][MAX_LINE_BYTES];
  int buffer_lines;

  int linepos;               // Text position for current line
  int line;                  // Current document line
  int col;                   // Current document column
  int lastcol;               // Remembered column from last horizontal navigation
  int anchor;                // Anchor position for selection
  
  int permissions;           // File permissions

  char filename[FILENAME_MAX];

  char linebuf[MAX_LINE_BYTES];     // Scratch buffer
  
  char tmpbuf[MAXSIZE];     // Text Buffer  
  char clipboard[MAXSIZE];
};

void insert(struct editor *ed, int pos, char *buf, int bufsize) {
  int i;

  for (i = 0; i < bufsize; i++) {
    if (ed->linebuf[0] == '\r') {
    } else { 
      ed->line_contents[ed->line][ed->col] = buf[i];
      ed->col++;

      if (buf[i] == '\n' || ed->col > LINE_LENGTH) {
        ed->line++;
        ed->col = 0;
      }
    }
  }

  if (ed->line > ed->buffer_lines) {
    ed->buffer_lines = ed->line;
  }
}

int load_file(struct editor *ed, char *filename) {
  struct stat statbuf;
  int f;

  if (!realpath(filename, ed->filename)) return -1;
  f = open(ed->filename, O_RDONLY | O_BINARY);
  if (f < 0) return -1;

  fstat(f, &statbuf);
  ed->permissions = statbuf.st_mode & 0777;

  while (read(f, ed->linebuf, 1) > 0) {
    insert(ed, -1, ed->linebuf, 1);
  }

  ed->anchor = -1;

  ed->line = 0;
  ed->col = 0;

  close(f);
  return 0;
}

int save_file(struct editor *ed) {
  int f, line;

  f = open(ed->filename, O_CREAT | O_TRUNC | O_WRONLY, ed->permissions);
  if (f < 0) return -1;

  for (line = 0; line < ed->buffer_lines; line++) {
    write(f, ed->line_contents[line], strlen(ed->line_contents[line]));
  }

  close(f);
  return 0;
}

void erase(struct editor *ed, int pos, int len) {
}

void replace(struct editor *ed, int pos, int len, char *buf, int bufsize) {
  erase(ed, pos, len);
  insert(ed, pos, buf, bufsize);
}

int get(struct editor *ed, int pos) {
  return (int)('a');
}

char* text_ptr(struct editor *ed, int pos) {
  return 0x00;
}

//
// Navigation functions
//

int line_length(struct editor *ed, int linepos) {
	int pos;
  for (pos = linepos;;pos++) {
    int ch = get(ed, pos);
    if (ch < 0 || ch == '\n' || ch == '\r') return pos - linepos;
  }
}

int line_start(struct editor *ed, int pos) {
  for (; pos > 0 && get(ed, pos - 1) != '\n'; pos--);
  return pos;
}

int next_line(struct editor *ed, int pos, int dir) {
  pos = line_start(ed, pos);
	
  if (dir > 0) pos += line_length(ed, pos);
  
  pos += dir;

  if (pos < 0) return -1;
  //if ((unsigned int)pos > strlen(ed->content)) { return -1; }
  
  return line_start(ed, pos);
}

int column(struct editor *ed, int linepos, int col) {
  return ed->col;
}

void moveto(struct editor *ed, int pos, int center) {
}

//
// Text selection
//

int get_selection(struct editor *ed, int *start, int *end) {
  return 0;
}

void get_selection_or_line(struct editor *ed, int *start, int *end) {
}

int get_selected_text(struct editor *ed, char *buffer, int size) {
  return 0;
}

void update_selection(struct editor *ed, int select) {
}

int erase_selection(struct editor *ed) {
  return 1;
}

void erase_selection_or_line(struct editor *ed) {
}

void select_all(struct editor *ed) {
}

//
// Screen functions
//

void get_console_size(struct editor *ed) {
  struct winsize ws;

  ioctl(0, TIOCGWINSZ, &ws);
  ed->cols = ws.ws_col;
  ed->lines = ws.ws_row - 1;
}

//
// Display functions
//

void display_message(struct editor *ed, char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  printf(GOTO_LINE_COL, ed->lines + 1, 1);
  fputs(STATUS_COLOR, stdout);
  vprintf(fmt, args);
  fputs(CLREOL TEXT_COLOR, stdout);
  fflush(stdout);
  va_end(args);
}

int prompt(struct editor *ed, char *msg, int selection) {
  int maxlen, len, ch;
  char *buf = ed->linebuf;

  display_message(ed, msg);

  len = 0;
  maxlen = ed->cols - strlen(msg) - 1;
  if (selection) {
    len = get_selected_text(ed, buf, maxlen);
    fwrite(buf, 1, len, stdout);
  }

  for (;;) {
    fflush(stdout);
    ch = get_key();
    if (ch == KEY_ESC) {
      return 0;
    } else if (ch == KEY_ENTER) {
      buf[len] = 0;
      return len > 0;
    } else if (ch == KEY_BACKSPACE) {
      if (len > 0) {
        fputs("\b \b", stdout);
        len--;
      }
    } else if (ch >= ' ' && ch < 0x100 && len < maxlen) {
      putchar(ch);
      buf[len++] = ch;
    }
  }
}

int ask() {
  int ch = getchar();
  return ch == 'y' || ch == 'Y';
}

void draw_full_statusline(struct editor *ed) {
  int namewidth = ed->cols - 36;
  printf(GOTO_LINE_COL, ed->lines + 1, 1);
  sprintf(ed->linebuf, STATUS_COLOR "%*.*s  SLn %-3d SCol %-3d Ln %-6dCol %-4d" CLREOL TEXT_COLOR, -namewidth, namewidth, ed->filename, ed->line, ed->col, ed->line + 1, column(ed, ed->linepos, ed->col) + 1);
  fputs(ed->linebuf, stdout);
}

void display_line(struct editor *ed, int line) {
 /**
  * Displays a line on the screen
  * @return The number of characters printed, or zero if we printed the full line
  */
  fputs("\r", stdout);
  fputs(ed->line_contents[line], stdout);

  if ( !strchr(ed->line_contents[line], '\n')) {
    fputs("\n", stdout);
  }
}

void draw_screen(struct editor *ed) {
  int screen_line;

  printf("\e[1;1H\e[2J"); // Clear the screen
  printf(GOTO_LINE_COL, 1, 1);
  fputs(TEXT_COLOR, stdout);

  for (screen_line = ed->line; screen_line < ed->line + ed->lines; screen_line++) {
    display_line(ed, screen_line);
  }
}

void position_cursor(struct editor *ed) {
  printf(GOTO_LINE_COL, 1, ed->col + 1);
}

//
// Cursor movement
//

void adjust(struct editor *ed) {
}

int sign(int x) {
  return (x > 0) - (x < 0);
}

void down(struct editor *ed, int select, int lines) {
  if (ed->line + lines >= 0 && ed->line + lines < ed->buffer_lines) {
    ed->line += lines;
    ed->col = 0;
  }

  adjust(ed);
}

void left(struct editor *ed, int select) {
  if (ed->col > 0) ed->col--;
}

void right(struct editor *ed, int select) {
  if (ed->col+1 < strlen(ed->line_contents[ed->line])) {
    ed->col++;
  }
}

int wordchar(int ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

void wordleft(struct editor *ed, int select) {
  int pos, phase;
  
  update_selection(ed, select);
  pos = ed->linepos + ed->col;
  phase = 0;
  while (pos > 0) {
    int ch = get(ed, pos - 1);
    if (phase == 0) {
      if (wordchar(ch)) phase = 1;
    } else {
      if (!wordchar(ch)) break;
    }

    pos--;
    if (pos < ed->linepos) {
      ed->linepos = next_line(ed, ed->linepos, -1);
      ed->line--;
    }
  }
  ed->col = pos - ed->linepos;
  ed->lastcol = ed->col;
  adjust(ed);
}

void wordright(struct editor *ed, int select) {
}

void home(struct editor *ed, int select) {
  update_selection(ed, select);
  ed->col = ed->lastcol = 0;
  adjust(ed);
}

void end(struct editor *ed, int select) {
  update_selection(ed, select);
  ed->col = ed->lastcol = line_length(ed, ed->linepos);
  adjust(ed);
}

//
// Text editing
//

void insert_char(struct editor *ed, char ch) {
  erase_selection(ed);
  insert(ed, ed->linepos + ed->col, &ch, 1);
  ed->col++;
  ed->lastcol = ed->col;
  adjust(ed);
}

void newline(struct editor *ed) {
  insert(ed, ed->linepos + ed->col, "\n", 1);

  down(ed, 0, 1);
  home(ed, 0);
}

void del(struct editor *ed) {
  int pos, ch;
  
  if (erase_selection(ed)) return;
  pos = ed->linepos + ed->col;
  ch = get(ed, pos);
  if (ch < 0) return;

  erase(ed, pos, 1);
  if (ch == '\r') {
    ch = get(ed, pos);
    if (ch == '\n') erase(ed, pos, 1);
  }
}

void backspace(struct editor *ed) {
  if (erase_selection(ed)) return;
  if (ed->linepos + ed->col == 0) return;
  left(ed, 0);
  del(ed);
}

void indent(struct editor *ed, char *indentation) {
}

void unindent(struct editor *ed, char *indentation) {
}

//
// Clipboard
//

void copy_selection_or_line(struct editor *ed) {
  FILE *f_pri, *f_sec, *f_clip;
  
  int selstart, selend, pos;

  get_selection_or_line(ed, &selstart, &selend);
  
  f_pri = popen("xsel", "w");
  f_sec = popen("xsel --secondary", "w");
  f_clip = popen("xsel --clipboard", "w");
  if (f_pri) {
    for (pos = selstart; pos < selend; pos++) {
      fprintf(f_pri, "%c", *text_ptr(ed, pos));
      if (f_sec) fprintf(f_sec, "%c", *text_ptr(ed, pos));
      if (f_clip) fprintf(f_clip, "%c", *text_ptr(ed, pos));
    }
    pclose(f_pri);
    if (f_sec) pclose(f_sec);
    if (f_clip) pclose(f_clip);
  } else {  
    //strncpy(ed->clipboard, ed->content+selstart, ed->clipsize);
  }
}

void cut_selection_or_line(struct editor *ed) {
  copy_selection_or_line(ed);
  erase_selection_or_line(ed);
}

void paste_selection(struct editor *ed) {
  FILE * f;
  char buffer[512];
  int n;
  int pos;

  erase_selection(ed);

  f = popen("xsel", "r");
  if (f) {
    pos = ed->linepos + ed->col;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
      insert(ed, pos, buffer, n);
      pos += n;
    }
    moveto(ed, pos, 0);
    pclose(f);
  } else {
    insert(ed, ed->linepos + ed->col, ed->clipboard, ed->clipsize);
    moveto(ed, ed->linepos + ed->col + ed->clipsize, 0);
  }
}

void duplicate_selection_or_line(struct editor *ed) {
  int selstart, selend, sellen;
  
  get_selection_or_line(ed, &selstart, &selend);

  sellen = selend - selstart;

  //strncpy(ed->tmpbuf, ed->content + selstart, sellen);

  insert(ed, ed->linepos + ed->col, ed->tmpbuf, sellen);
}

//
// Editor Commands
//

void save_editor(struct editor *ed) {
  int rc;
  
  rc = save_file(ed);
  if (rc < 0) {
    display_message(ed, "Error saving document");
    sleep(5);
  }
}

void find_text(struct editor *ed, char* search) {
  int slen, selstart, selend;

  if (!search) {
    search = ed->tmpbuf;
    if (!get_selection(ed, &selstart, &selend)) {
      if (!prompt(ed, "Find: ", 1)) {
        return;
      }    
      strncpy(search, ed->linebuf, strlen(ed->linebuf));
    } else {
      //strncpy(search, ed->content + selstart, selend - selstart);
    }
  }
  
  slen = strlen(search);

  if (slen > 0) {
    /*
    char *match;
    
    match = strstr(ed->content + ed->linepos + ed->col, search);
    if (match != NULL) {
      int pos = match - ed->content;
      ed->anchor = pos;
      moveto(ed, pos + slen, 1);
    } else {
      putchar('\007');
    }
    */
  }
}

void goto_line(struct editor *ed, int lineno) {
  int l, pos, new_pos;

  ed->anchor = -1;
  if (!lineno && prompt(ed, "Goto line: ", 1)) {
    lineno = atoi(ed->linebuf);
  }

  pos = 0;
  for (l = 0; l < lineno - 1 || lineno < 0; l++) {
    new_pos = next_line(ed, pos, 1);
    if (new_pos < 0) break;
    pos = new_pos;
  }

  if (pos >= 0) {
    moveto(ed, pos, 1);
  } else {
    putchar('\007');
  }
}

void goto_anything(struct editor *ed, char *query) {
  if (!query) {
    prompt(ed, "Goto Anything: ", 1);
    query = ed->linebuf;
  }

  if (query[0] == ':') { goto_line(ed, atoi(query + 1)); }
  if (query[0] == '#') { find_text(ed, query + 1); }
  if (query[0] == '@') { find_text(ed, query + 1); }
}

void redraw_screen(struct editor *ed) {
  get_console_size(ed);
  draw_screen(ed);
}

//
// Editor
//

void edit(struct editor *ed) {
  int done = 0;
  int key;

  while (!done) {
    draw_screen(ed);
    draw_full_statusline(ed);
    position_cursor(ed);
    fflush(stdout);
    key = get_key();

    if (key >= ' ' && key <= 0x7F) {
      insert_char(ed, (char) key);
    } else {
      switch (key) {
        case ctrl('t'): goto_line(ed, 1); break;
        case ctrl('b'): goto_line(ed, -1); break;

        case KEY_UP: down(ed, 0, -1); break;
        case KEY_DOWN: down(ed, 0, 1); break;
        case KEY_LEFT: left(ed, 0); break;
        case KEY_RIGHT: right(ed, 0); break;
        case KEY_HOME: home(ed, 0); break;
        case KEY_END: end(ed, 0); break;
        case KEY_PGUP: down(ed, 0, -PAGESIZE); break;
        case KEY_PGDN: down(ed, 0, PAGESIZE); break;
        case ctrl(KEY_UP): down(ed, 0, -PAGESIZE); break;
        case ctrl(KEY_DOWN): down(ed, 0, PAGESIZE); break;

        case ctrl(KEY_RIGHT): wordright(ed, 0); break;
        case ctrl(KEY_LEFT): wordleft(ed, 0); break;
        case ctrl(KEY_HOME): goto_line(ed, 1); break;
        case ctrl(KEY_END): goto_line(ed, -1); break;

        case shift(KEY_UP): down(ed, 1, -1); break;
        case shift(KEY_DOWN): down(ed, 1, 1); break;
        case shift(KEY_LEFT): left(ed, 1); break;
        case shift(KEY_RIGHT): right(ed, 1); break;
        case shift(KEY_PGUP): down(ed, 1, -PAGESIZE); break;
        case shift(KEY_PGDN): down(ed, 1, PAGESIZE); break;
        case shift(KEY_HOME): home(ed, 1); break;
        case shift(KEY_END): end(ed, 1); break;

        case shift(ctrl(KEY_RIGHT)): wordright(ed, 1); break;
        case shift(ctrl(KEY_LEFT)): wordleft(ed, 1); break;
        case shift(ctrl(KEY_HOME)): goto_line(ed, 1); break;
        case shift(ctrl(KEY_END)): goto_line(ed, -1); break;

        case ctrl('a'): select_all(ed); break;
        case ctrl('d'): duplicate_selection_or_line(ed); break;
        case ctrl('c'): copy_selection_or_line(ed); break;
        case KEY_F3: find_text(ed, 0); break;
        case ctrl('f'): find_text(ed, 0); break;
        case ctrl('l'): goto_line(ed, 0); break;
        case ctrl('g'): goto_anything(ed, 0); break;
        case ctrl('q'): done = 1; break;
        case ctrl('w'): done = 1; break;
        case ctrl('o'): done = 1; break;
        case ctrl('n'): done = 1; break;
        case KEY_TAB: indent(ed, INDENT); break;
        case shift(KEY_TAB): unindent(ed, INDENT); break;

        case KEY_ENTER: newline(ed); break;
        case KEY_BACKSPACE: backspace(ed); break;
        case KEY_DEL: del(ed); break;
        case ctrl('k'): erase_selection_or_line(ed); break;
        case ctrl('x'): cut_selection_or_line(ed); break;
        case ctrl('v'): paste_selection(ed); break;
        case ctrl('s'): save_editor(ed); break;
        default:
          insert_char(ed, ((char*)&key)[0]);
          insert_char(ed, ((char*)&key)[1]);
          break;
      }
    }
  }
}

//
// main
//

int main(int argc, char *argv[]) {
  sigset_t blocked_sigmask, orig_sigmask;
  struct termios tio;
  struct termios orig_tio;

  struct editor ed;

  if (argc < 2) return 0;

  if (load_file(&ed, argv[1]) < 0) {
    perror(argv[1]);
    return 0;
  }

  if (argc >= 3) goto_anything(&ed, argv[2]);

  setvbuf(stdout, NULL, 0, 8192);

  tcgetattr(0, &orig_tio);
  cfmakeraw(&tio);  
  tcsetattr(0, TCSANOW, &tio);
  linux_console = getenv("TERM") && !strcmp(getenv("TERM"), "linux");

  get_console_size(&ed);
  sigemptyset(&blocked_sigmask);
  sigaddset(&blocked_sigmask, SIGINT);
  sigaddset(&blocked_sigmask, SIGTSTP);
  sigaddset(&blocked_sigmask, SIGABRT);
  sigprocmask(SIG_BLOCK, &blocked_sigmask, &orig_sigmask);

  edit(&ed);

  printf(GOTO_LINE_COL, ed.lines + 2, 1);
  fputs(RESET_COLOR CLREOL CLRSCR, stdout);
  tcsetattr(0, TCSANOW, &orig_tio);   

  setbuf(stdout, NULL);
  sigprocmask(SIG_SETMASK, &orig_sigmask, NULL);
  return 0;
}
