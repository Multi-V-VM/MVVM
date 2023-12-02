import random
with open('hdastar.txt', 'w') as f:
    f.write('10000 10000\n')
    for i in range(10000 *10000+1):
        f.write('{} {}\n'.format(random.randint(0, 10000), random.randint(0, 10000)))