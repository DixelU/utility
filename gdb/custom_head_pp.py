import gdb
import gdb.printing
import re

# Regex adapted to match dixelu::details::mfunc<T>(void*, void*, dixelu::details::adj_mf_ops)
__mfunc_regex__ = r"dixelu::details::mfunc<(.*)>\(\s*void\*\s*,\s*void\*\s*,.*dixelu::details::adj_mf_ops\s*\)"

def get_mfunc_parameter(func_ptr):
	try:
		func_symbol = gdb.execute('info symbol 0x{:x}'.format(func_ptr), to_string=True)

		if 'No symbol matches' in func_symbol:
			return None

		matches = re.finditer(__mfunc_regex__, func_symbol, re.MULTILINE)
		for match in matches:
			return match.group(1)

		return None
	except Exception as e:
		print(f"Exception in get_mfunc_parameter: {e}")
		return None


class CustomHeadPrinter:
	"Pretty printer for dixelu::details::custom_head"

	def __init__(self, val):
		self.val = val

	def to_string(self):
		typename = get_mfunc_parameter(int(self.val['mf']))
		if typename is None:
			return "custom_head<unknown>"
		return f"custom_head<{typename}>"

	def children(self):
		typename = get_mfunc_parameter(int(self.val['mf']))

		if typename is None or "nullptr" in typename:
			return [("empty", self.val['data'])]

		try:
			gdb_type = gdb.lookup_type(typename)
		except gdb.error:
			return [("data", f"<unavailable type {typename}>")]

		ptr = self.val['data'].cast(gdb_type.pointer())

		try:
			return [("data", ptr.dereference())]
		except gdb.error:
			return [("data", ptr)]

	def __call__(self, printable):
		self.val = printable
		return self.children()

class CustomPrettyPrinter(gdb.printing.PrettyPrinter):
	def __init__(self):
		super(CustomPrettyPrinter, self).__init__("custom_head")
		self.subprinters = []
		self.subprinters.append(
			gdb.printing.RegexpCollectionPrettyPrinter("custom_head")
		)
		self.subprinters[-1].add_printer(
			'custom_head', 'dixelu::details::custom_head', CustomHeadPrinter
		)

	def __call__(self, val):
		for printer in self.subprinters:
			p = printer(val)
			if p:
				return p
		return None


def register_printers(obj):
	if obj is None:
		obj = gdb
	gdb.printing.register_pretty_printer(obj, CustomPrettyPrinter(), True)


register_printers(None)