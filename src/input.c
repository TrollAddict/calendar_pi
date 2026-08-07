#define _DEFAULT_SOURCE
#include "input.h"
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

static struct termios g_orig_termios;
static int g_have_orig = 0;

int input_init(void) {
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) != 0) {
        return -1;
    }
    g_have_orig = 1;

    struct termios raw = g_orig_termios;
    raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return -1;
    }

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    return 0;
}

void input_restore(void) {
    if (g_have_orig) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
    }
}

input_event_t input_poll(void) {
    unsigned char buf[8];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) return INPUT_NONE;

    if (buf[0] == 'q' || buf[0] == 'Q' || buf[0] == 0x03) {
        return INPUT_QUIT;
    }
    if (buf[0] == '\r' || buf[0] == '\n' || buf[0] == ' ') {
        return INPUT_SELECT;
    }
    if (n >= 3 && buf[0] == 0x1b && buf[1] == '[') {
        switch (buf[2]) {
            case 'A': return INPUT_UP;
            case 'B': return INPUT_DOWN;
            case 'C': return INPUT_RIGHT;
            case 'D': return INPUT_LEFT;
            default: break;
        }
    }
    if (buf[0] == 0x1b && n == 1) {
        return INPUT_QUIT;
    }
    return INPUT_NONE;
}
