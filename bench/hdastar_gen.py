import random
with open('hdastar.txt', 'w') as f:
    f.write('100000 100000\n')
    for i in range( 1001):
        f.write('{} {}\n'.format(random.randint(0, 100000), random.randint(0, 100000)))