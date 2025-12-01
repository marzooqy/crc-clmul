# This file is for experimenting with GF2 math and prototyping of CRC code
# Python makes this task easier with arbitrary length integers

# Everything is treated as a polynomial in the GF2 domain
# The coefficients of the polynomial are the bits (1s and 0s) of the integer
# Addition and subtraction are equivalent to an XOR
# Multiplication by x^n is equivalent to a left shift by n
# Division by x^n is equivalent to a right shift by n
# Multiplication and division of polynomials is carryless

from dataclasses import dataclass

def print_bin(a):
    print(bin(a))

def print_hex(a, w):
    print(('0x{:0' + str(w // 4) + 'x}').format(a))

@dataclass
class Model:
    width: int
    poly: int
    init: int
    refin: bool
    refout: bool
    xorout: int

crc32 = Model(32, 0x04c11db7, 0xffffffff, True, True, 0xffffffff)
crc32_mpeg = Model(32, 0x04c11db7, 0xffffffff, False, False, 0x00000000)
crc64_we = Model(64, 0x42f0e1eba9ea3693, 0xffffffffffffffff, False, False, 0xffffffffffffffff)
crc64_xz = Model(64, 0x42f0e1eba9ea3693, 0xffffffffffffffff, True, True, 0xffffffffffffffff)

# Find the remainder resulting from dividing polynomials a by b in GF2
# It tries to match the way this is done by hand
# You can find examples of this in the Ross Williams and Tad McCorkle guides
def mod(a, b):
    a_len = a.bit_length()
    b_len = b.bit_length()

    if a_len >= b_len:
        b <<= a_len - b_len

        for i in reversed(range(b_len - 1, a_len)):
            if (a >> i) & 1:
                a ^= b
            b >>= 1

    return a

# Expected: 1110
#print_bin(mod(0b11010110110000, 0b10011))

# Find the result of dividing polynomials a by b in GF2
# This is similar to mod but we apply a 1 every time the dividend is subtracted
def div(a, b):
    a_len = a.bit_length()
    b_len = b.bit_length()

    if a_len >= b_len:
        c = 0
        b <<= a_len - b_len

        for i in reversed(range(b_len - 1, a_len)):
            c <<= 1
            if (a >> i) & 1:
                a ^= b
                c |= 1
            b >>= 1

    return c

# Expected: 1100001010
#print_bin(div(0b11010110110000, 0b10011))

# Multiply polynomials a and b in GF2
def clmul(a, b):
    c = 0
    for _ in range(a.bit_length()):
        if a & 1:
            c ^= b
        a >>= 1
        b <<= 1

    return c

# Expected 1111111
#print_bin(clmul(0b1101, 0b1011))

#Reflect all the bits in an integer
def ref(a, w):
    b = 0
    for i in range(w):
        b <<= 1
        b |= a & 1
        a >>= 1

    return b

#Reflect all bytes in a bytes object
def ref_bytes(buf):
    return bytes((ref(b, 8) for b in buf))

# "Mathematical" CRC
# CRC is defined as M * X^W mod P
# M is the incoming data/message
# X^W is intentionally added so that all of the bits of M would be applied to the CRC
# otherwise polynomial division would stop before reaching the last W bits of M
# P is the polynomial including the often omitted X^(W + 1) term
# This also applies the different CRC parameters to the result
def crc_m(model, buf):
    # refin indicates that all of the bytes should be reflected before being consumed
    if model.refin:
        buf = ref_bytes(buf)

    data = int.from_bytes(buf, 'big')

    # The init is XOred with the first few incoming bits
    data ^= (model.init << (len(buf) * 8 - model.width))

    # Multiply by x^w
    data <<= model.width

    # Add x^(w+1)
    poly = (1 << model.width) | model.poly

    # Calculate the CRC
    crc = mod(data, poly)

    if model.refout:
        crc = ref(crc, model.width)
    return crc ^ model.xorout

# Prototype of the hardware accelerated algorithm
# Might have some unresolved errors
def crc_clmul(model, buf):
    if model.refin:
        p = (1 << model.width) | model.poly
        p <<= (64 - model.width)
        init = model.init

        k1 = ref(mod(1 << (512 + 63), p), 64)
        k2 = ref(mod(1 << (512 - 1), p), 64)

        x1 = x2 = x3 = x4 = 0
        h1 = h2 = h3 = h4 = 0
        l1 = l2 = l3 = l4 = 0

        x1 = int.from_bytes(buf[:16], 'little')
        x1 ^= init

        x2 = int.from_bytes(buf[16:32], 'little')
        x3 = int.from_bytes(buf[32:48], 'little')
        x4 = int.from_bytes(buf[48:64], 'little')

        length = len(buf) - 64
        pos = 64

        while length >= 64:
            h1 = clmul(x1 & 0xffffffffffffffff, k1)
            h2 = clmul(x2 & 0xffffffffffffffff, k1)
            h3 = clmul(x3 & 0xffffffffffffffff, k1)
            h4 = clmul(x4 & 0xffffffffffffffff, k1)

            l1 = clmul(x1 >> 64, k2)
            l2 = clmul(x2 >> 64, k2)
            l3 = clmul(x3 >> 64, k2)
            l4 = clmul(x4 >> 64, k2)

            x1 = int.from_bytes(buf[pos:pos + 16], 'little')
            x2 = int.from_bytes(buf[pos + 16:pos + 32], 'little')
            x3 = int.from_bytes(buf[pos + 32:pos + 48], 'little')
            x4 = int.from_bytes(buf[pos + 48:pos + 64], 'little')

            x1 ^= h1 ^ l1
            x2 ^= h2 ^ l2
            x3 ^= h3 ^ l3
            x4 ^= h4 ^ l4

            pos += 64
            length -= 64

        data = (x4 << 48 * 8) | (x3 << 32 * 8) | (x2 << 16 * 8) | x1
        data = ref(data, 64 * 8)
        data = (data << length * 8) | int.from_bytes(buf[pos:], 'big')
        crc = mod(data << 64, p) >> (64 - model.width)

        #return crc
        if model.refout:
            crc = ref(crc, model.width)
        return crc ^ model.xorout

    else:
        p = (1 << model.width) | model.poly
        p <<= (64 - model.width)
        init = model.init << (64 - model.width)

        k1 = mod(1 << (512 + 64), p)
        k2 = mod(1 << 512, p)

        x1 = x2 = x3 = x4 = 0
        h1 = h2 = h3 = h4 = 0
        l1 = l2 = l3 = l4 = 0

        x1 = int.from_bytes(buf[:16], 'big')
        x1 ^= (init << (128 - 64))

        x2 = int.from_bytes(buf[16:32], 'big')
        x3 = int.from_bytes(buf[32:48], 'big')
        x4 = int.from_bytes(buf[48:64], 'big')

        length = len(buf) - 64
        pos = 64

        while length >= 64:
            h1 = clmul(x1 >> 64, k1)
            h2 = clmul(x2 >> 64, k1)
            h3 = clmul(x3 >> 64, k1)
            h4 = clmul(x4 >> 64, k1)

            l1 = clmul(x1 & 0xffffffffffffffff, k2)
            l2 = clmul(x2 & 0xffffffffffffffff, k2)
            l3 = clmul(x3 & 0xffffffffffffffff, k2)
            l4 = clmul(x4 & 0xffffffffffffffff, k2)

            x1 = int.from_bytes(buf[pos:pos + 16], 'big')
            x2 = int.from_bytes(buf[pos + 16:pos + 32], 'big')
            x3 = int.from_bytes(buf[pos + 32:pos + 48], 'big')
            x4 = int.from_bytes(buf[pos + 48:pos + 64], 'big')

            x1 ^= h1 ^ l1
            x2 ^= h2 ^ l2
            x3 ^= h3 ^ l3
            x4 ^= h4 ^ l4

            pos += 64
            length -= 64

        data = (x1 << 48 * 8) | (x2 << 32 * 8) | (x3 << 16 * 8) | x4
        data = (data << length * 8) | int.from_bytes(buf[pos:], 'big')
        crc = mod(data << 64, p) >> (64 - model.width)

        #return crc
        if model.refout:
            crc = ref(crc, model.width)
        return crc ^ model.xorout

# Barret Reduction for 64-bits CRCs
# This is strictly following the Intel paper
def barret(model, value):
    p = (1 << model.width) | model.poly
    if model.refin:
        pp = ref(p, model.width + 1)
        u = ref(div(1 << 128, p), model.width + 1)
        t1 = clmul(value & 0xffffffffffffffff, u)
        t2 = clmul(t1 & 0xffffffffffffffff, pp)
        return (value ^ t2) >> 64
    else:
        u = div(1 << 128, p)
        t1 = clmul(value >> 64, u)
        t2 = clmul(t1 >> 64, p)
        return value ^ t2

# For testing the result of the Barret Reduction
#print_hex(mod(v, p), 64)

# Reflected case
#print_hex(ref(mod(v, p), 64), 64)