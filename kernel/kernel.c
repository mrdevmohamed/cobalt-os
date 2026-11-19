#include <stdint.h>
#include <stddef.h>

enum { WIDTH = 800, HEIGHT = 600, COLS = 88, ROWS = 23, INPUT_MAX = 79 };
enum { BG = 0x101b2d, PANEL = 0x1c2b43, INK = 0xe5edf8,
       MUTED = 0x93a9c5, ACCENT = 0x62d5c4, TERM = 0x0b1321 };

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0,%1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}
static void debug(const char *s) { while (*s) outb(0xe9, (uint8_t)*s++); }
static size_t length(const char *s) { size_t n = 0; while (s[n]) ++n; return n; }
static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void copy(char *dst, const char *src) { while ((*dst++ = *src++)) {} }
static char *decimal(char *dst, uint32_t value) {
    char digits[10];
    unsigned n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) *dst++ = digits[--n];
    *dst = 0;
    return dst;
}

volatile uint32_t ticks;
extern void timer_isr(void), fault_isr(void);
extern uint8_t __bss_end[];
struct idt_gate { uint16_t low, selector; uint8_t zero, flags; uint16_t high; } __attribute__((packed));
struct idt_pointer { uint16_t size; uint32_t address; } __attribute__((packed));
static struct idt_gate idt[256];
static void gate(unsigned vector, void (*handler)(void)) {
    uint32_t address = (uint32_t)handler;
    idt[vector] = (struct idt_gate){address & 0xffff, 8, 0, 0x8e, address >> 16};
}
static void timer_init(void) {
    for (unsigned i = 0; i < 256; ++i) gate(i, fault_isr);
    gate(32, timer_isr);
    struct idt_pointer pointer = {sizeof(idt) - 1, (uint32_t)idt};
    __asm__ volatile ("lidt %0" : : "m"(pointer));
    outb(0x20, 0x11); outb(0x80, 0);
    outb(0xa0, 0x11); outb(0x80, 0);
    outb(0x21, 0x20); outb(0x80, 0);
    outb(0xa1, 0x28); outb(0x80, 0);
    outb(0x21, 4); outb(0x80, 0);
    outb(0xa1, 2); outb(0x80, 0);
    outb(0x21, 1); outb(0x80, 0);
    outb(0xa1, 1); outb(0x80, 0);
    outb(0x21, 0xfe); outb(0xa1, 0xff);
    outb(0x43, 0x36);
    outb(0x40, (1193182 / 100) & 0xff);
    outb(0x40, (1193182 / 100) >> 8);
    __asm__ volatile ("sti");
}

static volatile uint8_t *framebuffer;
static unsigned pitch;
static uint32_t foreground = INK;
static int window_x = 32, window_y = 72, visible = 1;
static int mouse_x = 680, mouse_y = 500, mouse_present, cursor_visible;
static uint32_t cursor_under[12 * 18];

static void pixel(int x, int y, uint32_t color) {
    if ((unsigned)x < WIDTH && (unsigned)y < HEIGHT)
        *(volatile uint32_t *)(framebuffer + y * pitch + x * 4) = color;
}
static uint32_t get_pixel(int x, int y) {
    if ((unsigned)x < WIDTH && (unsigned)y < HEIGHT)
        return *(volatile uint32_t *)(framebuffer + y * pitch + x * 4);
    return 0;
}
static void rect(int x, int y, int w, int h, uint32_t color) {
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col) pixel(x + col, y + row, color);
}
static void glyph(int x, int y, unsigned char c, uint32_t color) {
    const uint8_t *font = (const uint8_t *)0x7000 + c * 16;
    for (int row = 0; row < 16; ++row)
        for (int col = 0; col < 8; ++col)
            if (font[row] & (0x80 >> col)) pixel(x + col, y + row, color);
}
static void text(int x, int y, const char *s, uint32_t color) {
    while (*s) { glyph(x, y, (unsigned char)*s++, color); x += 8; }
}
static void cursor_hide(void) {
    if (!cursor_visible) return;
    for (int y = 0; y < 18; ++y)
        for (int x = 0; x < 12; ++x) pixel(mouse_x + x, mouse_y + y, cursor_under[y * 12 + x]);
    cursor_visible = 0;
}
static void cursor_show(void) {
    if (!mouse_present) return;
    for (int y = 0; y < 18; ++y) {
        for (int x = 0; x < 12; ++x) {
            cursor_under[y * 12 + x] = get_pixel(mouse_x + x, mouse_y + y);
            if ((y < 14 && x <= y / 2) || (y >= 10 && y < 18 && x >= 4 && x <= 6))
                pixel(mouse_x + x, mouse_y + y, (x == 0 || x == y / 2) ? TERM : INK);
        }
    }
    cursor_visible = 1;
}

static char lines[ROWS][COLS];
static unsigned row, column;
static int caret_on = 1;
static void terminal_clear(void) {
    for (unsigned y = 0; y < ROWS; ++y)
        for (unsigned x = 0; x < COLS; ++x) lines[y][x] = ' ';
    row = column = 0;
}
static void newline(void) {
    column = 0;
    if (++row < ROWS) return;
    for (unsigned y = 1; y < ROWS; ++y)
        for (unsigned x = 0; x < COLS; ++x) lines[y - 1][x] = lines[y][x];
    for (unsigned x = 0; x < COLS; ++x) lines[ROWS - 1][x] = ' ';
    row = ROWS - 1;
}
static void put(char c) {
    outb(0xe9, (uint8_t)c);
    if (c == '\n') { newline(); return; }
    lines[row][column++] = c;
    if (column == COLS) newline();
}
static void print(const char *s) { while (*s) put(*s++); }
static void number(uint32_t n) { char s[11]; decimal(s, n); print(s); }
static void prompt(void) { print("cobalt> "); }
static void terminal_draw(void) {
    if (!visible) return;
    rect(window_x + 16, window_y + 44, COLS * 8, ROWS * 16, TERM);
    for (unsigned y = 0; y < ROWS; ++y)
        for (unsigned x = 0; x < COLS; ++x)
            if (lines[y][x] != ' ')
                glyph(window_x + 16 + (int)x * 8, window_y + 44 + (int)y * 16,
                      (unsigned char)lines[y][x], foreground);
    if (caret_on) rect(window_x + 16 + (int)column * 8, window_y + 57 + (int)row * 16, 8, 2, ACCENT);
}
static void clock_draw(void) {
    uint32_t seconds = ticks / 100;
    char clock[32] = "UP ";
    char *p = decimal(clock + 3, seconds / 3600);
    *p++ = ':'; *p++ = (char)('0' + seconds / 60 % 60 / 10);
    *p++ = (char)('0' + seconds / 60 % 10); *p++ = ':';
    *p++ = (char)('0' + seconds % 60 / 10); *p++ = (char)('0' + seconds % 10); *p = 0;
    rect(616, 12, 184, 16, BG);
    text(792 - (int)length(clock) * 8, 12, clock, MUTED);
}
static void desktop_draw(void) {
    rect(0, 0, WIDTH, HEIGHT, BG);
    rect(0, 40, WIDTH, 2, PANEL);
    text(24, 12, "COBALT / OS", ACCENT);
    text(96, 244, "A SMALL SYSTEM.", INK);
    text(96, 268, "C KERNEL. FASM BOOT.", MUTED);
    text(96, 292, "Click Terminal below to open the shell.", MUTED);
    rect(0, 560, WIDTH, 40, PANEL);
    rect(16, 568, 144, 24, visible ? ACCENT : BG);
    text(32, 572, "[ Terminal ]", visible ? TERM : INK);
    text(192, 572, "F1 help   Ctrl+L clear   Up/Down history", MUTED);
    if (visible) {
        rect(window_x + 5, window_y + 5, 736, 456, 0x060d18);
        rect(window_x, window_y, 736, 456, TERM);
        rect(window_x, window_y, 736, 32, PANEL);
        rect(window_x, window_y, 4, 32, ACCENT);
        text(window_x + 16, window_y + 8, "TERMINAL / cobalt", INK);
        text(window_x + 704, window_y + 8, "_", ACCENT);
        rect(window_x + 16, window_y + 422, 704, 1, PANEL);
        text(window_x + 16, window_y + 432, "PS/2 keyboard  |  ROM files  |  type help", MUTED);
        terminal_draw();
    }
    clock_draw();
}

static int controller_wait(int output) {
    for (unsigned i = 0; i < 100000; ++i) {
        uint8_t status = inb(0x64);
        if (output ? (status & 1) : !(status & 2)) return 1;
    }
    return 0;
}
static int controller_command(uint8_t command) {
    if (!controller_wait(0)) return 0;
    outb(0x64, command);
    return 1;
}
static int controller_data(uint8_t data) {
    if (!controller_wait(0)) return 0;
    outb(0x60, data);
    return 1;
}
static int mouse_command(uint8_t command) {
    if (!controller_command(0xd4) || !controller_data(command) || !controller_wait(1)) return 0;
    return inb(0x60) == 0xfa;
}
static void input_init(void) {
    while (inb(0x64) & 1) (void)inb(0x60);
    if (!controller_command(0xa8) || !controller_command(0x20) || !controller_wait(1)) return;
    uint8_t config = (inb(0x60) | 0x40) & (uint8_t)~0x33;
    if (!controller_command(0x60) || !controller_data(config)) return;
    mouse_present = mouse_command(0xf6) && mouse_command(0xf4);
}

static const struct rom_file { const char *name, *contents; } files[] = {
    {"README.TXT", "Cobalt is an independent 32-bit x86 hobby operating system.\n"
     "Boot: BIOS + FASM. Kernel, desktop and shell: freestanding C.\n"
     "Drag the terminal title bar; click _ to minimize it.\n"
     "The taskbar restores it. Files are compiled-in, read-only ROM data.\n"
     "No processes, writable disk filesystem, networking or protection yet.\n"},
    {"COMMANDS.TXT", "help clear about version echo uptime mem date ls cat color\n"
     "history reboot shutdown halt\n"
     "color accepts: white green amber. cat requires an exact ROM filename.\n"
     "Up/Down: command history. Ctrl+L: clear. Ctrl+C: cancel. F1: help.\n"}
};
static char input[INPUT_MAX + 1], history[8][INPUT_MAX + 1], draft[INPUT_MAX + 1];
static unsigned input_length, history_count, history_next, history_offset;
static void help(void) {
    print("Commands\n"
          "  help              Show this list\n"
          "  clear             Clear terminal\n"
          "  about / version   System information\n"
          "  echo TEXT         Print text\n"
          "  uptime / date     Timer / hardware UTC clock\n"
          "  mem               Kernel memory usage\n"
          "  ls / cat FILE     Read built-in ROM files\n"
          "  color NAME        white, green, amber\n"
          "  history           Show recent commands\n"
          "  reboot            Restart machine\n"
          "  shutdown / halt   QEMU power off / stop CPU\n");
}
static uint8_t cmos(uint8_t index) { outb(0x70, index); return inb(0x71); }
static unsigned rtc_value(uint8_t n, int binary) { return binary ? n : (n & 15) + (n >> 4) * 10; }
static void print_two(unsigned n) { put((char)('0' + n / 10)); put((char)('0' + n % 10)); }
static void date(void) {
    uint8_t sec, min, hour, day, month, year, mode;
    do {
        while (cmos(0x0a) & 0x80) {}
        sec = cmos(0); min = cmos(2); hour = cmos(4);
        day = cmos(7); month = cmos(8); year = cmos(9); mode = cmos(0x0b);
    } while ((cmos(0x0a) & 0x80) || sec != cmos(0));
    int binary = mode & 4, pm = hour & 0x80;
    unsigned h = rtc_value(hour & 0x7f, binary);
    if (!(mode & 2)) h = h % 12 + (pm ? 12 : 0);
    print("20"); print_two(rtc_value(year, binary)); put('-');
    print_two(rtc_value(month, binary)); put('-'); print_two(rtc_value(day, binary)); put(' ');
    print_two(h); put(':'); print_two(rtc_value(min, binary)); put(':');
    print_two(rtc_value(sec, binary)); print(" (RTC)\n");
}
static void stop(void) {
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}
static void execute(char *command) {
    while (*command == ' ') ++command;
    if (!*command) return;
    char *args = command;
    while (*args && *args != ' ') ++args;
    if (*args) *args++ = 0;
    while (*args == ' ') ++args;
    if (equal(command, "help")) help();
    else if (equal(command, "clear")) terminal_clear();
    else if (equal(command, "about") || equal(command, "version"))
        print("Cobalt OS 0.1.0 / i386\nFASM BIOS loader + freestanding C kernel.\n"
              "800x600 graphics, PS/2 input, single address space.\n");
    else if (equal(command, "echo")) { print(args); put('\n'); }
    else if (equal(command, "uptime")) { number(ticks / 100); print(" seconds since boot\n"); }
    else if (equal(command, "date")) date();
    else if (equal(command, "mem")) {
        print("Kernel + static data: "); number((uint32_t)__bss_end - 0x10000); print(" bytes\n");
        print("Kernel base: 0x10000. Stack top: 0x90000.\nNo heap; no RAM capacity probe.\n");
    } else if (equal(command, "ls")) {
        for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
            print(files[i].name); print("  "); number((uint32_t)length(files[i].contents)); print(" bytes (ROM)\n");
        }
    } else if (equal(command, "cat")) {
        unsigned i;
        for (i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
            if (equal(args, files[i].name)) { print(files[i].contents); break; }
        if (i == sizeof(files) / sizeof(files[0])) print("File not found; use ls (names are case-sensitive).\n");
    } else if (equal(command, "color")) {
        if (equal(args, "white")) foreground = INK;
        else if (equal(args, "green")) foreground = ACCENT;
        else if (equal(args, "amber")) foreground = 0xf3cd83;
        else print("Usage: color white|green|amber\n");
    } else if (equal(command, "history")) {
        for (unsigned i = 0; i < history_count; ++i) {
            print(history[(history_next + 8 - history_count + i) % 8]); put('\n');
        }
    } else if (equal(command, "reboot")) {
        print("Rebooting...\n"); terminal_draw();
        if (controller_wait(0)) outb(0x64, 0xfe);
        stop();
    } else if (equal(command, "shutdown")) {
        print("Powering off (QEMU ACPI)...\n"); terminal_draw();
        __asm__ volatile ("outw %0,%1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
        print("Power-off unsupported on this machine. CPU halted.\n"); terminal_draw(); stop();
    } else if (equal(command, "halt")) {
        print("CPU halted. Reset the machine to continue.\n"); terminal_draw(); stop();
    } else { print("Unknown command: "); print(command); print(" (try help)\n"); }
}
static void input_replace(const char *s) {
    while (input_length) { --input_length; --column; lines[row][column] = ' '; }
    copy(input, s); input_length = (unsigned)length(input);
    for (unsigned i = 0; i < input_length; ++i) put(input[i]);
}
static void submit(void) {
    input[input_length] = 0;
    if (input_length) {
        copy(history[history_next], input);
        history_next = (history_next + 1) % 8;
        if (history_count < 8) ++history_count;
    }
    put('\n'); execute(input);
    input_length = history_offset = 0; input[0] = 0;
    prompt();
}
static const char keymap[128] = {
    [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',
    [12]='-',[13]='=',[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',[21]='y',[22]='u',
    [23]='i',[24]='o',[25]='p',[26]='[',[27]=']',[30]='a',[31]='s',[32]='d',[33]='f',
    [34]='g',[35]='h',[36]='j',[37]='k',[38]='l',[39]=';',[40]='\'',[41]='`',
    [43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',[48]='b',[49]='n',[50]='m',
    [51]=',',[52]='.',[53]='/',[57]=' '
};
static const char shifted[128] = {
    [2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',[10]='(',[11]=')',
    [12]='_',[13]='+',[26]='{',[27]='}',[39]=':',[40]='"',[41]='~',[43]='|',
    [51]='<',[52]='>',[53]='?'
};
static void keyboard(uint8_t scan) {
    static unsigned shift, control, caps, extended;
    if (scan == 0xe0) { extended = 1; return; }
    unsigned code = scan & 0x7f, released = scan & 0x80, ext = extended;
    extended = 0;
    if (!ext && (code == 42 || code == 54)) {
        unsigned bit = code == 42 ? 1 : 2;
        if (released) shift &= ~bit; else shift |= bit;
        return;
    }
    if (code == 29) { control = !released; return; }
    if (released) return;
    if (!ext && code == 58) { caps ^= 1; return; }
    if (!visible) return;
    if (ext && (code == 72 || code == 80)) {
        if (code == 72 && history_offset < history_count) {
            if (!history_offset) copy(draft, input);
            ++history_offset;
        } else if (code == 80 && history_offset) --history_offset;
        else return;
        input_replace(history_offset ? history[(history_next + 8 - history_offset) % 8] : draft);
    } else if (ext) return;
    else if (code == 28) submit();
    else if (code == 14 && input_length) {
        input[--input_length] = 0; lines[row][--column] = ' ';
    } else if (code == 59) {
        put('\n'); help(); input_length = history_offset = 0; input[0] = 0; prompt();
    } else {
        char c = keymap[code];
        if (control && c == 'l') {
            terminal_clear(); prompt();
            for (unsigned i = 0; i < input_length; ++i) put(input[i]);
        } else if (control && c == 'c') {
            print("^C\n"); input_length = history_offset = 0; input[0] = 0; prompt();
        } else if (c && !control && input_length < INPUT_MAX) {
            if (c >= 'a' && c <= 'z') { if (!!shift != !!caps) c -= 'a' - 'A'; }
            else if (shift && shifted[code]) c = shifted[code];
            input[input_length++] = c; input[input_length] = 0; put(c);
        }
    }
    caret_on = 1;
    terminal_draw();
}
static void mouse(uint8_t byte) {
    static uint8_t packet[3];
    static unsigned count, left, dragging;
    static int drag_x, drag_y;
    if (!count && !(byte & 8)) return;
    packet[count++] = byte;
    if (count != 3) return;
    count = 0;
    if (packet[0] & 0xc0) return;
    mouse_x += (int)packet[1] - ((packet[0] & 0x10) ? 256 : 0);
    mouse_y -= (int)packet[2] - ((packet[0] & 0x20) ? 256 : 0);
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= WIDTH) mouse_x = WIDTH - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= HEIGHT) mouse_y = HEIGHT - 1;
    unsigned pressed = packet[0] & 1;
    int redraw = 0;
    if (pressed && !left) {
        if (mouse_y >= 568 && mouse_y < 592 && mouse_x >= 16 && mouse_x < 160) {
            visible = !visible; redraw = 1;
            debug(visible ? "GUI: terminal restored\n" : "GUI: terminal minimized\n");
        } else if (visible && mouse_y >= window_y && mouse_y < window_y + 32 &&
                   mouse_x >= window_x && mouse_x < window_x + 736) {
            if (mouse_x >= window_x + 692) {
                visible = 0; redraw = 1; debug("GUI: terminal minimized\n");
            } else { dragging = 1; drag_x = mouse_x - window_x; drag_y = mouse_y - window_y; }
        }
    }
    if (!pressed) dragging = 0;
    if (dragging) {
        int x = mouse_x - drag_x, y = mouse_y - drag_y;
        if (x < 0) x = 0;
        if (x > 59) x = 59;
        if (y < 48) y = 48;
        if (y > 99) y = 99;
        if (x != window_x || y != window_y) { window_x = x; window_y = y; redraw = 1; }
    }
    left = pressed;
    if (redraw) desktop_draw();
}

void kmain(void) {
    framebuffer = (volatile uint8_t *)(*(volatile uint32_t *)0x6000);
    pitch = *(volatile uint16_t *)0x6004;
    timer_init();
    input_init();
    terminal_clear();
    print("Cobalt OS 0.1.0\n"
          "------------------------------------------------------------\n"
          "Welcome to your C + FASM operating system.\n"
          "Type help for commands; use the mouse to move this window.\n\n");
    prompt(); desktop_draw(); cursor_show();
    debug("\nCOBALT READY\n");
    uint32_t last_blink = ticks, last_second = ticks / 100;
    for (;;) {
        unsigned processed = 0;
        while ((inb(0x64) & 1) && processed++ < 32) {
            uint8_t status = inb(0x64), data = inb(0x60);
            if (status & 0xc0) continue;
            cursor_hide();
            if (status & 0x20) { if (mouse_present) mouse(data); }
            else keyboard(data);
            cursor_show();
        }
        uint32_t now = ticks;
        if (now - last_blink >= 50) {
            last_blink = now; caret_on = !caret_on;
            cursor_hide();
            if (visible) {
                rect(window_x + 16 + (int)column * 8, window_y + 57 + (int)row * 16,
                     8, 2, caret_on ? ACCENT : TERM);
            }
            cursor_show();
        }
        if (now / 100 != last_second) {
            last_second = now / 100;
            cursor_hide(); clock_draw(); cursor_show();
        }
        __asm__ volatile ("hlt");
    }
}
