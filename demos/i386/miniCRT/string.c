// integer to string.
char * itoa(int n, char * str, int radix) {
    char digit[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char * p = str;
    char * head = str;
    // radix not correct.
    if (!p || radix < 2 || radix > 36) {
        return p;
    }
    // radix not 10 and n less than 0 can't convert to string.
    if (radix != 10 && n < 0) {
        return p;
    }
    // special integer 0.
    if (n == 0) {
        *p++ = '0';
        *p = 0;                     // append '\0' at the end.
        return p;
    }
    if (radix == 10 && n < 0) {
        *p++ = '-';                 // add '-' at first if negative.
        n = -n;
    }
    while (n) {
        *p++ = digit[n % radix];    // get response char(low to high) from char array.
        n /= radix;                 // update n.
    }
    *p = 0;                         // append '\0' at the end.

    // reverse the string: low -> high  ==> high -> low.
    for (--p; head < p; ++head, --p) {
        char temp = *head;
        *head = *p;
        *p = temp;
    }
    return str;
}

/**
 * @brief 
 * 
 * @param src the string to compare.
 * @param dst the string to be compared.
 * @return negative if p1 < p2; vice versa.
 */
int strcmp(const char * src, const char * dst) {
    int ret = 0;
    unsigned char * p1 = (unsigned char *) src;
    unsigned char * p2 = (unsigned char *) dst;
    while (!(ret = *p1 - *p2) && *p2) {
        ++p1, ++p2;
    }
    if (ret < 0) {
        ret = -1;
    } else if (ret > 0) {
        ret = 1;
    }
    return ret;
}

char * strcpy(char * dest, const char * src) {
    char * ret = dest;
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return ret;
}

unsigned strlen(const char * str) {
    int cnt = 0;
    if (!str) {
        return 0;
    }
    for (; *str != '\0'; ++str) {
        ++cnt;
    }
    return cnt;
}
