#include <types.hh>

unint8 __udivmoddi4(unint8 num, unint8 den, unint8 *rem_p) {
    unint8 quot = 0, qbit = 1;

    if (den == 0)
    {
        //asm volatile("int $0"); /* Divide by zero */
        return 0; /* If trap returns... */
    }

    /* Left-justify denominator and count shift */
    while ((int8) den >= 0)
    {
        den <<= 1;
        qbit <<= 1;
    }

    while (qbit)
    {
        if (den <= num)
        {
            num -= den;
            quot += qbit;
        }
        den >>= 1;
        qbit >>= 1;
    }

    if (rem_p)
        *rem_p = num;

    return quot;
}

/*!
 * \brief replacemnt for the 64bit integer divide funtion
 *
 * this function replaces the 64bit integer divide function supplied
 * with the glic. this makes the need of linking against glibc obsolute
 * which in turn saves some space in .text section of the resulting binary
 */
extern "C" unint8 __udivdi3(unint8 num, unint8 den) {
    return __udivmoddi4(num, den, NULL);
}
