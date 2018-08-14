import inspect


def get_caller(level: int = 2):
    stack = inspect.stack()
    frame = stack[level][0]
    (filename, line_number, function_name, lines, index) = inspect.getframeinfo(frame)
    return 'File "%s", line %d, in %s\n        %s' % (filename, line_number, function_name, lines[0].strip())


def validate(value, expected, msg: str = "Expected 0x%X, got 0x%X"):
    try:
        assert value == expected, msg % (value, expected)
    except AssertionError as err:
        print("%s\n    %s" % (err, get_caller()))
    finally:
        return value


def validate_type(value, types, throw: bool = True):
    if isinstance(types, list):
        for _type in types:
            if isinstance(value, _type):
                return True
        t = []
        for _type in types:
            t.append(_type.__name__)
        error = "Expected one of [%s], got %s" % (', '.join(t), type(value).__name__)
    elif isinstance(value, types):
        return True
    else:
        error = "Expected a %s, got %s" % (types.__name__, type(value).__name__)
    error = "%s\n    %s" % (error, get_caller())
    if throw:
        raise AssertionError(error)
    print(error)
    return False


def validate_range(value, min, max, throw: bool = True) -> bool:
    validate_type(value, [int, float])
    if value >= min and value <= max:
        return True
    error = "Value (%d) out of range: %d - %d\n    %s" % (value, min, max, get_caller())
    if throw:
        raise AssertionError(error)
    print(error)
    return False


def validate_tuple(value, expected, throw: bool = True) -> bool:
    validate_type(value, tuple)
    validate_type(expected, tuple)
    error = None
    if len(value) == len(expected):
        for key in range(len(value)):
            if isinstance(value[key], expected[key]):
                continue
            error = "Expected offset %d to be %s, got %s" % (
                key, type(value[key]).__name__, expected[key].__name__)
            break
    else:
        error = "Expected length to be %d, got %d" % (len(expected), len(value))
    if error is None:
        return True
    error = "%s\n    %s" % (error, get_caller())
    if throw:
        raise AssertionError(error)
    print(error)
    return False
