from bindings import *
from models import models

LEN = 300
test_data = bytes(b & 0xff for b in range(LEN))

failed = False

for name, model in models.items():
    params = crc_params(*model)
    value = crc_table(params, params.init, b'123456789')

    # Test 1: Test crc_table
    if value != model.check:
        print(name)
        print("table:", hex(value), hex(model.check), value == model.check)
        failed = True

    # Test 2: Test crc_clmul when len < 128
    value2 = crc_clmul(params, params.init, b'123456789')

    if value2 != model.check:
        print(name)
        print("clmul table:", hex(value2), hex(model.check), value2 == model.check)
        failed = True

    # Test 3: Test crc_clmul when len > 128
    value3 = crc_clmul(params, params.init, test_data)
    value4 = crc_table(params, params.init, test_data)

    if value3 != value4:
        print(name)
        print("clmul:", hex(value3), hex(value4), value3 == value4)
        failed = True

if failed:
    raise Exception("Test failed")
else:
    print("The test ran successfully!")
