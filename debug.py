import inspect


def get_caller(level: int = 2):
    stack = inspect.stack()
    curr = len(stack) - 1
    history = []
    while curr >= level:
        frame = stack[curr][0]
        (filename, line_number, function_name, lines, index) = inspect.getframeinfo(frame)
        history.append('    File "%s", line %d, in %s' % (filename, line_number, function_name))
        curr -= 1

    return '\n'.join(history) + '\n        %s' % lines[0].strip()


def validate(value, expected, msg: str = "Expected $1, got $0"):
    if value != expected:
        error = msg + "\n" + get_caller()
        error = error.replace('$0', str(value))
        error = error.replace('$1', str(expected))

        raise AssertionError(error)

    return value


def validate_type(value, types, throw: bool = True, msg: str = "Expected $1, got $0"):
    if isinstance(types, list):
        for _type in types:
            if isinstance(value, _type):
                return True
        t = []
        for _type in types:
            t.append(_type.__name__)
        expected = '[' + ', '.join(t) + ']'
    elif isinstance(value, types):
        return True
    else:
        expected = types.__name__

    error = msg + "\n" + get_caller()
    error = error.replace('$0', type(value).__name__)
    error = error.replace('$1', expected)

    if throw:
        raise AssertionError(error)

    print(error)
    return False


def validate_range(value, min, max, throw: bool = True, msg: str = "Value ($0) out of range: $1 - $2") -> bool:
    validate_type(value, [int, float])
    if value >= min and value <= max:
        return True

    error = msg + "\n" + get_caller()
    error = error.replace('$0', str(value))
    error = error.replace('$1', str(min))
    error = error.replace('$2', str(max))

    if throw:
        raise AssertionError(error)

    print(error)
    return False


def validate_lower(value, target, throw: bool = True, msg: str = "Value ($0) out of range: X - $1") -> bool:
    validate_type(value, [int, float])
    if value < target:
        return True

    error = msg + "\n" + get_caller()
    error = error.replace('$0', str(value))
    error = error.replace('$1', str(target))

    if throw:
        raise AssertionError(error)

    print(error)
    return False


def validate_higher(value, target, throw: bool = True, msg: str = "Value ($0) out of range: X - $1") -> bool:
    validate_type(value, [int, float])
    if value > target:
        return True

    error = msg + "\n" + get_caller()
    error = error.replace('$0', str(value))
    error = error.replace('$1', str(target))

    if throw:
        raise AssertionError(error)

    print(error)
    return False


def validate_tuple(value, expected, throw: bool = True, msg: str = "Expected offset $3 to be $0, got $1") -> bool:
    validate_type(value, tuple)
    validate_type(expected, tuple)
    validate_length(value, len(expected), level=3)
    error = None
    for key in range(len(value)):
        if isinstance(value[key], expected[key]):
            continue

        error = msg + "\n" + get_caller()
        error = error.replace('$0', type(value[key]).__name__)
        error = error.replace('$1', expected[key].__name__)
        error = error.replace('$2', str(key))

        break

    if error is None:
        return True
    if throw:
        raise AssertionError(error)

    print(error)
    return False


def validate_length(value, expected: int, throw: bool = True,
                    msg: str = "Expected length to be $1, got $0", level: int = 2) -> bool:
    if len(value) == expected:
        return True

    error = msg + "\n" + get_caller()
    error = error.replace('$0', str(len(value)))
    error = error.replace('$1', str(expected))

    if throw:
        raise AssertionError(error)

    print(error)
    return False
