// The VU's float arithmetic on the bits of a float, in 32-bit unsigned integers only (no 64-bit type, no float
// operation): ONE source for three compilers. It is included
//   * by gsr/vu_limbs.h as C++ (namespace gsr::vp::limbs), which is what the CPU tests check against the double
//     arithmetic of gsr/vertex_program.h and against the processor's own round-toward-zero arithmetic;
//   * by gsr/vu_limbs.h again as text, from which src/vertex_stage_emit.h writes the HLSL and the WGSL library.
// So the text below must stay inside the subset the three languages share:
//   * every value is a 32-bit unsigned integer, declared `uvar name = value;` (HLSL `uint`, WGSL `var`);
//   * a comparison becomes a value only through ubit(comparison) (1 or 0); `if`, `for` and `&&`, `||` take
//     comparisons directly;
//   * no `?:`, no unary minus (write 0u - x), no assignment to a parameter, no `++`;
//   * every shift count is below 32 (C++ and constant evaluation in WGSL have no defined result otherwise);
//   * every literal carries the `u` suffix, and mixed operators are fully parenthesised (WGSL refuses `a & b + c`
//     and `a << b + c`);
//   * one `return` per function, as its last statement (FXC handles early returns badly).
// A function is VUX_FN<parameter count>(group, name, parameters..., { body }). Groups: Primitive (the body below is
// what C++ compiles; a shader language takes its own intrinsic instead, see emitLibrary: a select and a count of
// leading zeros cost FXC five to twenty instructions each when spelled out, and its compile time grows much faster
// than the instruction count), Base (shared by every arithmetic backend), Limb (add, multiply, divide, square root
// in integer limbs; the double backend of Direct3D 11 replaces exactly these four), Composite (built from the four).
//
// The rules (the reference is the engine in ps2xRuntime/src/lib/vu):
//   * operands: a denormal is a signed zero, an infinity or NaN the largest finite value of its sign (vuOperand);
//     every function below except vuOperand, vuItof and vuKey expects such operands and returns one;
//   * every result is the exact result rounded toward zero; above the finite range it is the largest finite value,
//     below the normal range a zero with the sign of the exact result;
//   * a sum of exact zero is +0 unless both operands are -0;
//   * division by zero (0 / 0 included) gives the largest finite value with the sign of the quotient;
//   * the square root takes the magnitude of its operand; the reciprocal square root is a division by that rounded
//     root (two roundings), so its zero radicand gives the largest finite value with the sign of the numerator;
//   * a multiply-add rounds twice: the product, then the sum;
//   * MAX and MINI return their second operand unless the first is strictly greater (smaller); -0 equals +0;
//   * float to fixed point truncates and saturates at the 32-bit signed range; integer to float truncates.

// c is 1 or 0
VUX_FN3(Primitive, vuSelect, c, a, b, {
    uvar m = 0u - c;
    return (a & m) | (b & ~m);
})

// leading zero bits of a value that is not zero
VUX_FN1(Primitive, vuClz, value, {
    uvar x = value;
    uvar t = ubit(x < 0x00010000u) << 4u;
    uvar n = t;
    x = x << t;
    t = ubit(x < 0x01000000u) << 3u;
    n = n + t;
    x = x << t;
    t = ubit(x < 0x10000000u) << 2u;
    n = n + t;
    x = x << t;
    t = ubit(x < 0x40000000u) << 1u;
    n = n + t;
    x = x << t;
    return n + ubit(x < 0x80000000u);
})

VUX_FN1(Base, vuOperand, bits, {
    uvar sign = bits & 0x80000000u;
    uvar mag = bits & 0x7FFFFFFFu;
    uvar clamped = vuSelect(ubit(mag < 0x7F7FFFFFu), mag, 0x7F7FFFFFu);
    return sign | vuSelect(ubit(mag <= 0x007FFFFFu), 0u, clamped);
})

VUX_FN1(Base, vuNeg, a, { return a ^ 0x80000000u; })

VUX_FN1(Base, vuAbs, a, { return a & 0x7FFFFFFFu; })

// An unsigned integer that orders like the float (both zeros give the same key).
VUX_FN1(Base, vuKey, a, {
    uvar mag = a & 0x7FFFFFFFu;
    return vuSelect(a >> 31u, 0x80000000u - mag, 0x80000000u + mag);
})

VUX_FN2(Base, vuLess, a, b, { return ubit(vuKey(a) < vuKey(b)); })

VUX_FN2(Base, vuLessEqual, a, b, { return ubit(vuKey(a) <= vuKey(b)); })

VUX_FN2(Base, vuEqual, a, b, { return ubit(vuKey(a) == vuKey(b)); })

VUX_FN2(Base, vuMax, a, b, { return vuSelect(vuLess(b, a), a, b); })

VUX_FN2(Base, vuMin, a, b, { return vuSelect(vuLess(a, b), a, b); })

// A signed 32-bit integer with `fraction` fraction bits (0..15) to float.
VUX_FN2(Base, vuItof, value, fraction, {
    uvar sign = value & 0x80000000u;
    uvar mag = vuSelect(sign >> 31u, 0u - value, value);
    uvar lz = vuClz(mag | ubit(mag == 0u));
    uvar norm = mag << lz;
    uvar bits = sign | ((158u - lz - fraction) << 23u) | ((norm >> 8u) & 0x007FFFFFu);
    return vuSelect(ubit(mag == 0u), 0u, bits);
})

// Float to a signed 32-bit integer with `fraction` fraction bits (0..15).
VUX_FN2(Base, vuFtoi, a, fraction, {
    uvar e = (a >> 23u) & 0xFFu;
    uvar m = (a & 0x007FFFFFu) | 0x00800000u;
    uvar t = e + fraction;
    uvar up = vuSelect(ubit(t >= 150u), t - 150u, 0u) & 7u;
    uvar down = vuSelect(ubit(t < 150u), 150u - t, 0u);
    down = vuSelect(ubit(down > 31u), 31u, down);
    uvar r = (m << up) >> down;
    r = vuSelect(a >> 31u, 0u - r, r);
    r = vuSelect(ubit(t >= 158u), vuSelect(a >> 31u, 0x80000000u, 0x7FFFFFFFu), r);
    return vuSelect(ubit((t < 127u) || (e == 0u)), 0u, r);
})

// The larger magnitude x with seven guard bits, the smaller one shifted to its exponent; what the shift loses
// only matters to a difference, where it takes one more unit (the floor of x - y is x - ceil(y)).
VUX_FN2(Limb, vuAdd, a, b, {
    uvar swap = ubit((a & 0x7FFFFFFFu) < (b & 0x7FFFFFFFu));
    uvar x = vuSelect(swap, b, a);
    uvar y = vuSelect(swap, a, b);
    uvar ex = (x >> 23u) & 0xFFu;
    uvar ey = (y >> 23u) & 0xFFu;
    uvar mx = ((x & 0x007FFFFFu) | (ubit(ex != 0u) << 23u)) << 7u;
    uvar my = ((y & 0x007FFFFFu) | (ubit(ey != 0u) << 23u)) << 7u;
    uvar d = ex - ey;
    d = vuSelect(ubit(d > 31u), 31u, d);
    uvar yt = my >> d;
    uvar lost = ubit((yt << d) != my);
    uvar same = ubit(((a ^ b) >> 31u) == 0u);
    uvar r = vuSelect(same, mx + yt, mx - yt - lost);
    uvar lz = vuClz(r | ubit(r == 0u));
    uvar e1 = ex + 1u;
    uvar sign = x & 0x80000000u;
    uvar bits = sign | ((e1 - lz) << 23u) | (((r << lz) >> 8u) & 0x007FFFFFu);
    bits = vuSelect(ubit(e1 <= lz), sign, bits);
    bits = vuSelect(ubit((e1 > lz) && ((e1 - lz) >= 255u)), sign | 0x7F7FFFFFu, bits);
    return vuSelect(ubit(r == 0u), a & b & 0x80000000u, bits);
})

// 24 x 24 bits in 12-bit halves: `top` is the product's upper 24 bits, `low` holds its lower 24 (and a carry).
VUX_FN2(Limb, vuMul, a, b, {
    uvar sign = (a ^ b) & 0x80000000u;
    uvar ea = (a >> 23u) & 0xFFu;
    uvar eb = (b >> 23u) & 0xFFu;
    uvar ma = (a & 0x007FFFFFu) | 0x00800000u;
    uvar mb = (b & 0x007FFFFFu) | 0x00800000u;
    uvar ah = ma >> 12u;
    uvar al = ma & 0xFFFu;
    uvar bh = mb >> 12u;
    uvar bl = mb & 0xFFFu;
    uvar mid = (ah * bl) + (al * bh);
    uvar low = ((mid & 0xFFFu) << 12u) + (al * bl);
    uvar top = (ah * bh) + (mid >> 12u) + (low >> 24u);
    uvar n = top >> 23u;
    uvar mant = vuSelect(n, top, (top << 1u) | ((low >> 23u) & 1u)) & 0x007FFFFFu;
    uvar es = ea + eb + n;
    uvar bits = sign | ((es - 127u) << 23u) | mant;
    bits = vuSelect(ubit(es >= 382u), sign | 0x7F7FFFFFu, bits);
    bits = vuSelect(ubit(es <= 127u), sign, bits);
    return vuSelect(ubit((ea == 0u) || (eb == 0u)), sign, bits);
})

// Long division of the 24-bit significands, eight quotient bits per step (the remainder stays below 2^24).
VUX_FN2(Limb, vuDiv, a, b, {
    uvar sign = (a ^ b) & 0x80000000u;
    uvar ea = (a >> 23u) & 0xFFu;
    uvar eb = (b >> 23u) & 0xFFu;
    uvar ma = (a & 0x007FFFFFu) | 0x00800000u;
    uvar mb = (b & 0x007FFFFFu) | 0x00800000u;
    uvar below = ubit(ma < mb);
    uvar n = ma << below;
    uvar q = n / mb;
    uvar r = (n % mb) << 8u;
    q = (q << 8u) | (r / mb);
    r = (r % mb) << 8u;
    q = (q << 8u) | (r / mb);
    r = (r % mb) << 7u;
    q = (q << 7u) | (r / mb);
    uvar es = ea + 382u - eb - below;
    uvar bits = sign | ((es - 255u) << 23u) | (q & 0x007FFFFFu);
    bits = vuSelect(ubit(es >= 510u), sign | 0x7F7FFFFFu, bits);
    bits = vuSelect(ubit(es <= 255u), sign, bits);
    bits = vuSelect(ubit(ea == 0u), sign, bits);
    return vuSelect(ubit(eb == 0u), sign | 0x7F7FFFFFu, bits);
})

// Digit by digit over the 48-bit radicand (the significand shifted so that the exponent left over is even), two
// bits per step from its upper, then its lower 24 bits; the remainder stays below 2^26.
VUX_FN1(Limb, vuSqrt, a, {
    uvar e = (a >> 23u) & 0xFFu;
    uvar m = (a & 0x007FFFFFu) | 0x00800000u;
    uvar wide = (e & 1u) ^ 1u;
    uvar hi = vuSelect(wide, m, m >> 1u);
    uvar lo = vuSelect(wide, 0u, (m & 1u) << 23u);
    uvar root = 0u;
    uvar rem = 0u;
    for (uvar i = 0u; i < 12u; i += 1u) {
        rem = (rem << 2u) | ((hi >> (22u - (2u * i))) & 3u);
        uvar t = (root << 2u) | 1u;
        uvar ge = ubit(rem >= t);
        rem = rem - (t & (0u - ge));
        root = (root << 1u) | ge;
    }
    for (uvar j = 0u; j < 12u; j += 1u) {
        rem = (rem << 2u) | ((lo >> (22u - (2u * j))) & 3u);
        uvar u = (root << 2u) | 1u;
        uvar gf = ubit(rem >= u);
        rem = rem - (u & (0u - gf));
        root = (root << 1u) | gf;
    }
    return vuSelect(ubit(e == 0u), 0u, (((e + 127u) >> 1u) << 23u) | (root & 0x007FFFFFu));
})

VUX_FN2(Composite, vuSub, a, b, { return vuAdd(a, b ^ 0x80000000u); })

VUX_FN3(Composite, vuMadd, acc, a, b, { return vuAdd(acc, vuMul(a, b)); })

VUX_FN3(Composite, vuMsub, acc, a, b, { return vuAdd(acc, vuMul(a, b) ^ 0x80000000u); })

VUX_FN2(Composite, vuRsqrt, num, radicand, { return vuDiv(num, vuSqrt(radicand)); })
