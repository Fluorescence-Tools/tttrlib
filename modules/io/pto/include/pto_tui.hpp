// SPDX-License-Identifier: MIT
// tttrlib PTO Terminal UI Library (pto_tui.hpp)
#ifndef PTO_TUI_HPP
#define PTO_TUI_HPP

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <sstream>
#include <iomanip>
#include <algorithm>

#if defined(_WIN32)
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/select.h>
#endif

namespace pto_tui {

struct Terminal {
    static void get_size(int& width, int& height) {
        width = 80;
        height = 24;
#if !defined(_WIN32)
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            width = ws.ws_col;
            height = ws.ws_row;
        }
#else
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#endif
    }

    static void enter_raw_mode() {
        std::cout << "\033[?1049h\033[?25l" << std::flush;
#if !defined(_WIN32)
        struct termios raw;
        tcgetattr(STDIN_FILENO, &raw);
        raw.c_lflag &= ~(ECHO | ICANON);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
    }

    static void exit_raw_mode() {
        std::cout << "\033[?25h\033[?1049l" << std::flush;
#if !defined(_WIN32)
        struct termios raw;
        tcgetattr(STDIN_FILENO, &raw);
        raw.c_lflag |= (ECHO | ICANON);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
    }
};

enum class Key {
    None, Up, Down, Left, Right,
    CtrlUp, CtrlDown, CtrlLeft, CtrlRight,
    Enter, Escape, Quit,
    Tab, Space,
    Num1, Num2, Num3, Num4, Num5,
    Plus, Minus, LogScale, Channel, Extract, ExtractAll, ToggleTree
};

inline Key read_key(int timeout_ms = 25) {
#if !defined(_WIN32)
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int rv = select(STDIN_FILENO + 1, &set, NULL, NULL, &tv);
    if (rv <= 0) return Key::None;

    char c = 0;
    if (read(STDIN_FILENO, &c, 1) <= 0) return Key::None;
    if (c == 27) {
        /* Check if esc sequence */
        struct timeval tv_esc = {0, 2000};
        fd_set set_esc;
        FD_ZERO(&set_esc);
        FD_SET(STDIN_FILENO, &set_esc);
        if (select(STDIN_FILENO + 1, &set_esc, NULL, NULL, &tv_esc) <= 0) return Key::Escape;

        char seq[16] = {0};
        int len = 0;
        while (len < 15) {
            char ch = 0;
            if (read(STDIN_FILENO, &ch, 1) <= 0) break;
            seq[len++] = ch;
            if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '~') break;
            FD_ZERO(&set_esc);
            FD_SET(STDIN_FILENO, &set_esc);
            tv_esc.tv_sec = 0;
            tv_esc.tv_usec = 1000;
            if (select(STDIN_FILENO + 1, &set_esc, NULL, NULL, &tv_esc) <= 0) break;
        }

        if (seq[0] == '[') {
            if (len == 2) {
                if (seq[1] == 'A') return Key::Up;
                if (seq[1] == 'B') return Key::Down;
                if (seq[1] == 'C') return Key::Right;
                if (seq[1] == 'D') return Key::Left;
            }
            std::string s(seq, len);
            if (s == "[1;5A" || s == "[5A") return Key::CtrlUp;
            if (s == "[1;5B" || s == "[5B") return Key::CtrlDown;
            if (s == "[1;5C" || s == "[5C") return Key::CtrlRight;
            if (s == "[1;5D" || s == "[5D") return Key::CtrlLeft;

            /* Alt/Option or Shift modifier fallback */
            if (s == "[1;2A" || s == "[1;3A") return Key::CtrlUp;
            if (s == "[1;2B" || s == "[1;3B") return Key::CtrlDown;
            if (s == "[1;2C" || s == "[1;3C") return Key::CtrlRight;
            if (s == "[1;2D" || s == "[1;3D") return Key::CtrlLeft;

            if (seq[len - 1] == 'A') return Key::Up;
            if (seq[len - 1] == 'B') return Key::Down;
            if (seq[len - 1] == 'C') return Key::Right;
            if (seq[len - 1] == 'D') return Key::Left;
        } else if (seq[0] == 'O') {
            if (seq[1] == 'A') return Key::Up;
            if (seq[1] == 'B') return Key::Down;
            if (seq[1] == 'C') return Key::Right;
            if (seq[1] == 'D') return Key::Left;
        }
        return Key::Escape;
    }
    if (c == '\t') return Key::Tab;
    if (c == ' ') return Key::Space;
    if (c == '\n' || c == '\r') return Key::Enter;
    if (c == 'q' || c == 'Q') return Key::Quit;
    if (c == '1') return Key::Num1;
    if (c == '2') return Key::Num2;
    if (c == '3') return Key::Num3;
    if (c == '4') return Key::Num4;
    if (c == '5') return Key::Num5;
    if (c == '+' || c == '=') return Key::Plus;
    if (c == '-' || c == '_') return Key::Minus;
    if (c == 'l' || c == 'L') return Key::LogScale;
    if (c == 'c' || c == 'C') return Key::Channel;
    if (c == 'e') return Key::Extract;
    if (c == 'E') return Key::ExtractAll;
    if (c == 't') return Key::ToggleTree;
    return Key::None;
#else
    if (!_kbhit()) return Key::None;
    int c = _getch();
    if (c == 0 || c == 224) {
        int c2 = _getch();
        if (c2 == 72) return Key::Up;
        if (c2 == 80) return Key::Down;
        if (c2 == 75) return Key::Left;
        if (c2 == 77) return Key::Right;
        if (c2 == 141) return Key::CtrlUp;
        if (c2 == 145) return Key::CtrlDown;
        if (c2 == 115) return Key::CtrlLeft;
        if (c2 == 116) return Key::CtrlRight;
        return Key::None;
    }
    if (c == 'q' || c == 'Q') return Key::Quit;
    if (c == '\r' || c == '\n') return Key::Enter;
    if (c == '1') return Key::Num1;
    if (c == '2') return Key::Num2;
    if (c == '3') return Key::Num3;
    if (c == '4') return Key::Num4;
    if (c == '5') return Key::Num5;
    if (c == ' ') return Key::Space;
    if (c == 'l' || c == 'L') return Key::LogScale;
    if (c == 'c' || c == 'C') return Key::Channel;
    return Key::None;
#endif
}

class App {
public:
    std::string title;
    bool running = true;

    void run(std::function<void()> draw_cb, std::function<bool(Key)> input_cb, std::function<bool()> needs_redraw_cb = nullptr, int tick_ms = 30) {
        Terminal::enter_raw_mode();
        std::cout << "\033[2J" << std::flush;
        draw_cb();
        while (running) {
            Key k = read_key(tick_ms);
            bool redraw = (k != Key::None);
            if (needs_redraw_cb && needs_redraw_cb()) redraw = true;

            if (k != Key::None) {
                if (k == Key::Quit) {
                    running = false;
                } else {
                    if (input_cb(k)) running = false;
                }
            }

            if (redraw && running) {
                draw_cb();
            }
        }
        Terminal::exit_raw_mode();
    }
};

} // namespace pto_tui

#endif /* PTO_TUI_HPP */
