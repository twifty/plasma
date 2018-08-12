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
        setattr(tmp, field, getattr(getattr(sys.modules[__name__], name), field))

del path, dir, module
