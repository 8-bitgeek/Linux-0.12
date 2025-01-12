int strcmp(const char * src, const char * dst) {
    int ret = 0;
    unsigned char * p1 = (unsigned char *)src;
    unsigned char * p2 = (unsigned char *)dst;

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

/* 
 * The parameter `radix` specifies the base used to convert the integer `n` into a string. 
 * - radix = 2: Binary representation (e.g., "1010")
 * - radix = 10: Decimal representation (e.g., "12")
 * - radix = 16: Hexadecimal representation (e.g., "F")
 */
char * itoa(int n, char * str, int radix) {
    char digit[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char * p = str;
    char * head = str;

    /* radix < 2 or > 36 is not allowed */
    if (!p || radix < 2 || radix > 36) {
        return p;
    }
    /* a negative number is not allowed for bases other than decimal */
    if (radix != 10 && n < 0) {
        return p;
    }
    /* 0 */
    if (n == 0) {
        *p++ = '0';
        *p = 0;
        return p;
    }
    /* negative number bases decimal */
    if (radix == 10 && n < 0) {
        *p++ = '-';
        n = -n;
    }

    /* from low to high */
    while (n) {
        *p++ = digit[n % radix];
        n /= radix;
    }

    /* append '\0' */
    *p = 0;

    /* high to high and low to low */
    for (--p; head < p; ++head, --p) {
        char temp = *head;
        *head = *p;
        *p = temp;
    }

    return str;
}

char * strcpy(const char * src, char * dest) {
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
        cnt++;
    }
    return cnt;
}