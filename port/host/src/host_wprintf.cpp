/*
 * swprintf / vswprintf for the host's 16-bit wchar_t (port/wide.h).
 *
 * glibc's wide printf reads 4-byte format characters and writes 4-byte
 * output, so with -fshort-wchar every number the game formats (scores,
 * records, "Level %d", nw4r::ut::TextWriterBase::Printf) came out as garbage.
 * The game's formats are ASCII: each conversion is formatted by the narrow
 * vsnprintf and widened. Semantics are C99's (and MSL's): %s takes a char
 * string, %ls a wchar_t string, %c an int, %lc a wint_t; the result counts
 * characters, -1 when it did not fit (the buffer is still terminated).
 */
#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" int port_vswprintf(wchar_t* buf, size_t n, const wchar_t* fmt, va_list ap);
extern "C" int port_swprintf(wchar_t* buf, size_t n, const wchar_t* fmt, ...);

namespace {

struct Out {
    wchar_t* buf;
    size_t n;
    size_t len;
    void put(wchar_t c) {
        if (len + 1 < n) {
            buf[len] = c;
        }
        len++;
    }
};

void PutPadded(Out& o, const wchar_t* ws, const char* s, size_t count, int width, bool left) {
    int pad = width > static_cast<int>(count) ? width - static_cast<int>(count) : 0;
    if (!left) {
        for (int i = 0; i < pad; i++) o.put(L' ');
    }
    for (size_t i = 0; i < count; i++) {
        o.put(ws != nullptr ? ws[i] : static_cast<wchar_t>(static_cast<unsigned char>(s[i])));
    }
    if (left) {
        for (int i = 0; i < pad; i++) o.put(L' ');
    }
}

} // namespace

extern "C" int port_vswprintf(wchar_t* buf, size_t n, const wchar_t* fmt, va_list ap) {
    Out o = {buf, n, 0};
    va_list args;
    va_copy(args, ap);
    for (const wchar_t* p = fmt; *p != 0; p++) {
        if (*p != L'%') {
            o.put(*p);
            continue;
        }
        // One conversion: %[flags][width][.precision][length]conv, as a narrow spec
        char spec[64];
        size_t sl = 0;
        spec[sl++] = '%';
        p++;
        bool left = false;
        while (*p != 0 && std::strchr("-+ #0", static_cast<char>(*p)) != nullptr) {
            left |= *p == L'-';
            spec[sl++] = static_cast<char>(*p++);
        }
        int width = 0;
        if (*p == L'*') {
            width = va_arg(args, int);
            if (width < 0) {
                left = true;
                width = -width;
            }
            sl += std::snprintf(spec + sl, sizeof spec - sl, "%d", width);
            p++;
        } else {
            while (*p >= L'0' && *p <= L'9') {
                width = width * 10 + (*p - L'0');
                spec[sl++] = static_cast<char>(*p++);
            }
        }
        int prec = -1;
        if (*p == L'.') {
            spec[sl++] = '.';
            p++;
            prec = 0;
            if (*p == L'*') {
                prec = va_arg(args, int);
                sl += std::snprintf(spec + sl, sizeof spec - sl, "%d", prec);
                p++;
            } else {
                while (*p >= L'0' && *p <= L'9') {
                    prec = prec * 10 + (*p - L'0');
                    spec[sl++] = static_cast<char>(*p++);
                }
            }
        }
        char len[3] = {0, 0, 0};
        if (*p == L'h' || *p == L'l') {
            len[0] = static_cast<char>(*p++);
            if (*p == len[0]) {
                len[1] = static_cast<char>(*p++);
            }
        } else if (*p == L'L' || *p == L'j' || *p == L'z' || *p == L't') {
            len[0] = static_cast<char>(*p++);
        }
        wchar_t conv = *p;
        if (conv == 0) {
            break;
        }
        char tmp[512];
        int tl = 0;
        switch (conv) {
        case L'%':
            o.put(L'%');
            continue;
        case L'd': case L'i': case L'u': case L'x': case L'X': case L'o': {
            std::strcpy(spec + sl, len);
            sl += std::strlen(len);
            spec[sl++] = static_cast<char>(conv);
            spec[sl] = 0;
            bool isSigned = conv == L'd' || conv == L'i';
            if (len[0] == 'l' && len[1] == 'l') {
                tl = isSigned ? std::snprintf(tmp, sizeof tmp, spec, va_arg(args, long long))
                              : std::snprintf(tmp, sizeof tmp, spec, va_arg(args, unsigned long long));
            } else if (len[0] == 'l' || len[0] == 'j' || len[0] == 'z' || len[0] == 't') {
                tl = isSigned ? std::snprintf(tmp, sizeof tmp, spec, va_arg(args, long))
                              : std::snprintf(tmp, sizeof tmp, spec, va_arg(args, unsigned long));
            } else {
                tl = isSigned ? std::snprintf(tmp, sizeof tmp, spec, va_arg(args, int))
                              : std::snprintf(tmp, sizeof tmp, spec, va_arg(args, unsigned int));
            }
            break;
        }
        case L'f': case L'F': case L'e': case L'E': case L'g': case L'G': case L'a': case L'A':
            if (len[0] == 'L') {
                spec[sl++] = 'L';
                spec[sl++] = static_cast<char>(conv);
                spec[sl] = 0;
                tl = std::snprintf(tmp, sizeof tmp, spec, va_arg(args, long double));
            } else {
                spec[sl++] = static_cast<char>(conv);
                spec[sl] = 0;
                tl = std::snprintf(tmp, sizeof tmp, spec, va_arg(args, double));
            }
            break;
        case L'p':
            spec[sl++] = 'p';
            spec[sl] = 0;
            tl = std::snprintf(tmp, sizeof tmp, spec, va_arg(args, void*));
            break;
        case L'c': {
            wchar_t c = static_cast<wchar_t>(va_arg(args, int));
            PutPadded(o, &c, nullptr, 1, width, left);
            continue;
        }
        case L's':
            if (len[0] == 'l') {
                const wchar_t* ws = va_arg(args, const wchar_t*);
                size_t count = 0;
                while (ws[count] != 0 && (prec < 0 || count < static_cast<size_t>(prec))) count++;
                PutPadded(o, ws, nullptr, count, width, left);
            } else {
                const char* s = va_arg(args, const char*);
                size_t count = 0;
                while (s[count] != 0 && (prec < 0 || count < static_cast<size_t>(prec))) count++;
                PutPadded(o, nullptr, s, count, width, left);
            }
            continue;
        case L'n':
            *va_arg(args, int*) = static_cast<int>(o.len);
            continue;
        default:
            // Unknown: copy it through
            for (size_t i = 0; i < sl; i++) o.put(static_cast<wchar_t>(spec[i]));
            o.put(conv);
            continue;
        }
        if (tl < 0) {
            tl = 0;
        }
        if (tl >= static_cast<int>(sizeof tmp)) {
            tl = sizeof tmp - 1;
        }
        for (int i = 0; i < tl; i++) {
            o.put(static_cast<wchar_t>(static_cast<unsigned char>(tmp[i])));
        }
    }
    va_end(args);
    if (n > 0) {
        buf[o.len < n ? o.len : n - 1] = 0;
    }
    return o.len < n ? static_cast<int>(o.len) : -1;
}

extern "C" int port_swprintf(wchar_t* buf, size_t n, const wchar_t* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = port_vswprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}
