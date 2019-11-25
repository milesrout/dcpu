import functools
import itertools
import sys


def altevery(iterable, n=2):
    while True:
        for i in range(n):
            if i == 0:
                yield next(iterable)


def every(iterable, n=2):
    return itertools.compress(iterable, itertools.cycle([1] + [0] * (n - 1)))


def groups(iterable, n=2, fillvalue=None):
    iterables = itertools.tee(iterable, n)
    for i in range(n):
        for j in range(i):
            next(iterables[i], None)
    return itertools.zip_longest(*iterables, fillvalue=fillvalue)


def groupwise(iterable, n=2, fillvalue=None):
    return every(groups(iterable, n, fillvalue), n)


def compose(f):
    def outer(g):
        @functools.wraps(g)
        def inner(*args, **kwds):
            return f(g(*args, **kwds))
        return inner
    return outer


def convert_bytes(data, bytewidth, byteorder):
    grouped = list(groupwise(data, bytewidth, fillvalue=0))
    if byteorder == 'little':
        return [bytes(reversed(bs)) for bs in grouped]
    elif byteorder == 'big':
        return [bytes(bs) for bs in grouped]
    raise TypeError(f"Unexpected {byteorder}, expected 'big' or 'little'")


@compose(list)
def pretty_bytes(data, bytewidth):
    for value in convert_bytes(data, bytewidth, 'big'):
        yield hex(int.from_bytes(value, 'big'))


def pretty_print_bin(filename):
    with open(filename, 'rb') as f:
        data = f.read()
        for group in groupwise(pretty_bytes(data, 2), 8, '0x0'):
            print(''.join(g + ', ' for g in group))


if __name__ == '__main__':
    pretty_print_bin(sys.argv[1])
