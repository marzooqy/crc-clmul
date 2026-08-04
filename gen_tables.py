# Generate tables for the shuffle intrinsic

print('Shift left')
for i in range(16):
    print('{', end='')
    for j in range(16):
        k = j - i
        if k < 0:
            k = -1
        if j == 0:
            p = 2
        else:
            p = 3
        print(f'{k:{p}d}', end='')
        if j < 15:
            print(',', end='')
    print('}', end='')
    if i < 15:
        print(',', end='')
    print()

print('\nShift right')
for i in range(16):
    print('{', end='')
    for j in range(16):
        k = j + i
        if k > 15:
            k = -1
        if j == 0:
            p = 2
        else:
            p = 3
        print(f'{k:{p}d}', end='')
        if j < 15:
            print(',', end='')
    print('}', end='')
    if i < 15:
        print(',', end='')
    print()

print()