const char krk_builtinsSrc[] = 
"# Please avoid using double quotes or escape sequences\n"
"# in this file to allow it to be easily converted to C.\n"
"class list():\n"
" 'Resizable array with direct constant-time indexing.'\n"
" def extend(i):\n"
"  'Add all entries from an iterable to the end of this list.'\n"
"  for v in i:\n"
"   self.append(v)\n"
"  return self.__len__()\n"
" def __str__(self): return self.__repr__()\n"
" def __repr__(self):\n"
"  if self.__inrepr: return '[...]'\n"
"  self.__inrepr=1\n"
"  let b='['\n"
"  let l=self.__len__()\n"
"  for i=0,i<l,i=i+1:\n"
"   if i>0:\n"
"    b+=', '\n"
"   b+=repr(self[i])\n"
"  self.__inrepr=0\n"
"  return b+']'\n"
"\n"
"class dict():\n"
" 'Hashmap of arbitrary keys to arbitrary values.'\n"
" def __str__(self): return self.__repr__()\n"
" def __repr__(self):\n"
"  if self.__inrepr: return '{...}'\n"
"  self.__inrepr = 1\n"
"  let out = '{'\n"
"  let first = True\n"
"  for v in self.keys():\n"
"   if not first:\n"
"    out += ', '\n"
"   first = False\n"
"   out += v.__repr__() + ': ' + self[v].__repr__()\n"
"  out += '}'\n"
"  self.__inrepr = 0\n"
"  return out\n"
" def keys(self):\n"
"  'Returns an iterable of the keys in this dictionary.'\n"
"  class KeyIterator():\n"
"   def __init__(self,t):\n"
"    self.t=t\n"
"   def __iter__(self):\n"
"    let i=0\n"
"    let c=self.t.capacity()\n"
"    def _():\n"
"     let o=None\n"
"     while o==None and i<c:\n"
"      o=self.t._key_at_index(i)\n"
"      i++\n"
"     if o==None:\n"
"      return _\n"
"     return o\n"
"    return _\n"
"  return KeyIterator(self)\n"
"\n"
"class Helper():\n"
" '''You seem to already know how to use this.'''\n"
" def __call__(self,obj=None):\n"
"  if obj:\n"
"   try:\n"
"    print(obj.__doc__)\n"
"   except:\n"
"    try:\n"
"     print(obj.__class__.__doc__)\n"
"    except:\n"
"     print('No docstring avaialble for', obj)\n"
"  else:\n"
"   from help import interactive\n"
"   interactive()\n"
" def __repr__(self):\n"
"  return 'Type help() for more help, or help(obj) to describe an object.'\n"
"\n"
"let help = Helper()\n"
"\n"
"let _licenseText = '''\n"
"Copyright (c) 2020-2021 K. Lange <klange@toaruos.org>\n"
"\n"
"Permission to use, copy, modify, and/or distribute this software for any\n"
"purpose with or without fee is hereby granted, provided that the above\n"
"copyright notice and this permission notice appear in all copies.\n"
"\n"
"THE SOFTWARE IS PROVIDED 'AS IS' AND THE AUTHOR DISCLAIMS ALL WARRANTIES\n"
"WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF\n"
"MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR\n"
"ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES\n"
"WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN\n"
"ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF\n"
"OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.\n"
"'''\n"
"\n"
"class LicenseReader():\n"
" def __call__(self):\n"
"  print(_licenseText)\n"
" def __repr__(self):\n"
"  return 'Copyright 2020-2021 K. Lange <klange@toaruos.org>. Type `license()` for more information.'\n"
"\n"
"let license = LicenseReader()\n"
"\n"
"__builtins__.list = list\n"
"__builtins__.dict = dict\n"
"__builtins__.help = help\n"
"__builtins__.license = license\n"
"\n"
"# this works because `kuroko` is always a built-in\n"
"import kuroko\n"
"kuroko.module_paths = ['./','./modules/','/home/klange/Projects/kuroko/modules/','/usr/share/kuroko/']\n"
"\n"
"return object()\n"
;
