import inspect


def get_caller(level=2):
    stack = inspect.stack()
    frame = stack[level][0]
    (filename, line_number, function_name, lines, index) = inspect.getframeinfo(frame)
    return 'File "%s", line %d, in %s\n        %s' % (filename, line_number, function_name, lines[0].strip())


def validate(value, expected, msg="Expected 0x%X, got 0x%X"):
    try:
        assert value == expected, msg % (value, expected)
    except AssertionError as err:
        print("%s\n    %s" % (err, get_caller()))
        # stack = inspect.stack()
        # frame = stack[1][0]
        # (filename, line_number, function_name, lines, index) = inspect.getframeinfo(frame)
        # msg = '%s\n  File "%s", line %d, in %s\n    %s' % (
        #     err, filename, line_number, function_name, lines[0].strip())
        # print(msg)
    finally:
        return value


def type_check(value, types, throw=True):
    if isinstance(types, list):
        for _type in types:
            if isinstance(value, _type):
                return True
        t = []
        for _type in types:
            t.append(_type.__name__)
        msg = "Expected one of [%s], got %s" % (', '.join(t), type(value).__name__)
    elif isinstance(value, types):
        return True
    else:
        msg = "Expected a %s, got %s" % (types.__name__, type(value).__name__)
    msg = "%s\n    %s" % (msg, get_caller())
    if throw:
        raise AssertionError(msg)
    print(msg)
    return False


def validate_range(value, min, max, throw=True) -> bool:
    type_check(value, [int, float])
    if value >= min and value <= max:
        return True
    msg = "Value (%d) out of range: %d - %d\n    %s" % (value, min, max, get_caller())
    if throw:
        raise AssertionError(msg)
    print(msg)
    return False
