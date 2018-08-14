import os
import sys
import argparse

path = os.path.dirname(__file__)
dir = os.path.basename(path)

setattr(sys.modules[__name__], dir.capitalize(), argparse.Namespace())
tmp = getattr(sys.modules[__name__], dir.capitalize())

for module in os.listdir(path):
    if module[:1] != '_' and module[-3:] == '.py':
        name = module[:-3]
        field = name.capitalize()

        __import__(dir + '.' + name, locals(), globals())
        _module = getattr(sys.modules[__name__], name)
        if hasattr(_module, field):
            setattr(tmp, field, getattr(_module, field))

del path, dir, module, tmp
