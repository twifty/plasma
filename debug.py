import inspect


def validate(value, expected, msg="Expected 0x%X, got 0x%X"):
    try:
        assert value == expected, msg % (value, expected)
    except AssertionError as err:
        stack = inspect.stack()
        frame = stack[1][0]
        (filename, line_number, function_name, lines, index) = inspect.getframeinfo(frame)
        msg = '%s\n  File "%s", line %d, in %s\n    %s' % (
            err, filename, line_number, function_name, lines[0].strip())
        print(msg)
    finally:
        return value
