import ctypes
import os
import sys

if sys.platform == 'win32':
    ext = '.dll'
elif sys.platform == 'darwin':
    ext = '.dylib'
else:
    ext = '.so'

_crc = ctypes.CDLL(os.path.join(os.path.dirname(__file__), 'crc' + ext))

# Note: Update thie definition when the equivalent C code is changed
class params_t(ctypes.Structure):
    _fields_ = [('width', ctypes.c_uint8),
               ('poly', ctypes.c_uint64),
               ('refin', ctypes.c_bool),
               ('refout', ctypes.c_bool),
               ('init', ctypes.c_uint64),
               ('xorout', ctypes.c_uint64),
               ('k1', ctypes.c_uint64),
               ('k2', ctypes.c_uint64),
               ('table', ctypes.c_uint64 * 256),
               ('combine_table', ctypes.c_uint64 * 64)]

_crc.crc_params.argtypes = [ctypes.c_uint8, ctypes.c_uint64, ctypes.c_uint64, ctypes.c_bool, ctypes.c_bool, ctypes.c_uint64]
_crc.crc_params.restype = params_t

_crc.crc_table.argtypes = [ctypes.POINTER(params_t), ctypes.c_uint64, ctypes.c_char_p, ctypes.c_uint64]
_crc.crc_table.restype = ctypes.c_uint64

_crc.crc_calc.argtypes = [ctypes.POINTER(params_t), ctypes.c_uint64, ctypes.c_char_p, ctypes.c_uint64]
_crc.crc_calc.restype = ctypes.c_uint64

_crc.crc_combine.argtypes = [ctypes.POINTER(params_t), ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint64]
_crc.crc_combine.restype = ctypes.c_uint64

def crc_params(width, poly, init, refin, refout, xorout, check=0):
    return _crc.crc_params(width, poly, init, refin, refout, xorout)

def crc_table(params, crc, buf):
    return _crc.crc_table(ctypes.byref(params), crc, buf, len(buf))

def crc_calc(params, crc, buf):
    return _crc.crc_calc(ctypes.byref(params), crc, buf, len(buf))

def crc_combine(params, crc, crc2, len):
    return _crc.crc_combine(ctypes.byref(params), crc, crc2, len)