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

print('\nMask left')
for i in range(16):
    print('{', end='')
    for j in range(16 - i):
        print(f'{-1:2d}', end='')
        if j < 15:
            print(', ', end='')
    for j in range(16 - i, 16):
        print(f'{0:#2d}', end='')
        if j < 15:
            print(', ', end='')
    print('}', end='')
    if i < 15:
        print(',', end='')
    print()

print('\nMask right')
for i in range(16):
    print('{', end='')
    for j in range(i):
        print(f'{0:2d}', end='')
        if j < 15:
            print(', ', end='')
    for j in range(i, 16):
        print(f'{-1:2d}', end='')
        if j < 15:
            print(', ', end='')
    print('}', end='')
    if i < 15:
        print(',', end='')
    print()

print()