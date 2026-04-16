from bindings import *
from models import models
import sys

# Test CPU features
use_simd = True

if len(sys.argv) > 1 and sys.argv[1] == '--no_simd':
    use_simd = False

cpu_check_features()

if use_simd and not cpu_enable_simd:
    raise Exception('Expected SIMD intrinsics to be enabled')

if not use_simd and cpu_enable_simd:
    raise Exception('Expected SIMD intrinsics to be disabled')

#----------------------------------------

# Test CRC
test_data = bytes(b & 0xff for b in range(300))
failed = False

def check(test_name, test_value, actual_value, print_result_if_true=True):
    result = test_value == actual_value

    if (result and print_result_if_true) or not result:
        print(f'{test_name + ':':<17} {test_value:#x} {actual_value:#x} {result}')

    if not result:
        global failed
        failed = True

for name, model in models.items():
    print(name)
    params = crc_params(*model)

    # Test crc_table
    value = crc_table(params, params.init, b'123456789')
    check('Table', value, model.check)

    # Test crc_calc when len < 16
    value = crc_calc(params, params.init, test_data[:10])
    value2 = crc_table(params, params.init, test_data[:10])
    check('len < 16', value, value2)

    # Test crc_calc when len < 64
    value = crc_calc(params, params.init, test_data[:50])
    value2 = crc_table(params, params.init, test_data[:50])
    check('len < 64', value, value2)

    # Test crc_calc when len < 128
    value = crc_calc(params, params.init, test_data[:100])
    value2 = crc_table(params, params.init, test_data[:100])
    check('len < 128', value, value2)

    # Test crc_calc when len > 128
    value = crc_calc(params, params.init, test_data)
    value2 = crc_table(params, params.init, test_data)
    check('len > 128', value, value2)

    # Test crc_calc in chunks
    value = crc_calc(params, params.init, test_data[:150])
    value = crc_calc(params, value, test_data[150:])
    value2 = crc_calc(params, params.init, test_data)
    check('Chunked', value, value2)

    # Test crc_calc with an unaligned buffer
    for i in range(1, 16):
        value = crc_calc_unaligned(params, params.init, test_data, i)
        value2 = crc_calc(params, params.init, test_data[i:])
        check('Unaligned', value, value2, False)

    # Test crc_combine_constant and crc_combine_fixed
    xp = crc_combine_constant(params, 4)
    value = crc_calc(params, params.init, b'12345')
    value2 = crc_calc(params, params.init, b'6789')
    value = crc_combine_fixed(params, value, value2, xp)
    check('Combine Constant', value, model.check)

    # Test crc_combine
    value = crc_calc(params, params.init, b'12345')
    value2 = crc_calc(params, params.init, b'6789')
    value = crc_combine(params, value, value2, 4)
    check('Combine', value, model.check)

    print()

if failed:
    raise Exception('Test failed')
else:
    print('The test ran successfully!')