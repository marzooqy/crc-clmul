from bindings import *
from models import models

test_data = bytes(b & 0xff for b in range(300))
failed = False

for name, model in models.items():
    print(name)
    params = crc_params(*model)

    # Test 1: Test crc_table
    value = crc_table(params, params.init, b'123456789')
    print("table:        ", hex(value), hex(model.check), value == model.check)

    if value != model.check:
        failed = True

    # Test 2: Test crc_calc when len < 128
    value = crc_calc(params, params.init, b'123456789')
    print("clmul & table:", hex(value), hex(model.check), value == model.check)

    if value != model.check:
        failed = True

    # Test 3: Test crc_calc when len > 128
    value = crc_calc(params, params.init, test_data)
    value2 = crc_table(params, params.init, test_data)
    print("clmul:        ", hex(value), hex(value2), value == value2)

    if value != value2:
        failed = True

    # Test 4: Test crc_calc in chunks
    value = crc_calc(params, params.init, test_data[:150])
    value = crc_calc(params, value, test_data[150:])
    value2 = crc_calc(params, params.init, test_data)
    print("clmul chunks: ", hex(value), hex(value2), value == value2)

    if value != value2:
        failed = True

    #Test 5: Test crc_combine
    value = crc_calc(params, params.init, b'12345')
    value2 = crc_calc(params, params.init, b'6789')
    value = crc_combine(params, value, value2, 4)
    print("combine:      ", hex(value), hex(model.check), value == model.check)

    if value != model.check:
        failed = True

    print()

if failed:
    raise Exception("Test failed")
else:
    print("The test ran successfully!")