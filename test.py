from models import models
import ctypes
import os

crc = ctypes.CDLL(os.path.join(os.path.dirname(__file__), 'crc.dll'))

class params_t(ctypes.Structure):
    _fields_ = [('width', ctypes.c_uint8),
               ('spoly', ctypes.c_uint64),
               ('rpoly', ctypes.c_uint64),
               ('refin', ctypes.c_bool),
               ('refout', ctypes.c_bool),
               ('init', ctypes.c_uint64),
               ('xorout', ctypes.c_uint64),
               ('x576', ctypes.c_uint64),
               ('x512', ctypes.c_uint64),
               ('table', ctypes.c_uint64 * 256)]

crc.crc_params.argtypes = [ctypes.c_uint8, ctypes.c_uint64, ctypes.c_uint64, ctypes.c_bool, ctypes.c_bool, ctypes.c_uint64]
crc.crc_params.restype = params_t

crc.crc_table.argtypes = [ctypes.POINTER(params_t), ctypes.c_uint64, ctypes.c_char_p, ctypes.c_uint64]
crc.crc_table.restype = ctypes.c_uint64

crc.crc_clmul.argtypes = [ctypes.POINTER(params_t), ctypes.c_uint64, ctypes.c_char_p, ctypes.c_uint64]
crc.crc_clmul.restype = ctypes.c_uint64

LEN = 300

test_data = bytes(b & 0xff for b in range(LEN))

failed = False

for name, model in models.items():
    params = crc.crc_params(*model[:6]) #excluding the check value
    value = crc.crc_table(params, params.init, b'123456789', 9)

    # Test 1: Test crc_table
    if value != model.check:
        print(name)
        print("table:", hex(value), hex(model.check), value == model.check)
        failed = True

    # Test 2: Test crc_clmul when len < 128
    value2 = crc.crc_clmul(params, params.init, b'123456789', 9)

    if value2 != model.check:
        print(name)
        print("clmul table:", hex(value2), hex(model.check), value2 == model.check)
        failed = True

    # Test 3: Test crc_clmul when len > 128
    value3 = crc.crc_clmul(params, params.init, test_data, LEN)
    value4 = crc.crc_table(params, params.init, test_data, LEN)

    if value3 != value4:
        print(name)
        print("clmul:", hex(value3), hex(value4), value3 == value4)
        failed = True

if failed:
    raise Exception("Test failed")
else:
    print("The test ran successfully!")
