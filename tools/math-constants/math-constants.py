#! /usr/bin/env python3

import os
from mpmath import *

script_realpath = os.path.realpath(__file__)
root_dir = os.path.dirname(os.path.dirname(os.path.dirname(script_realpath)))
script_path = os.path.relpath(script_realpath, root_dir)
file_path = "src/common/math/Constants.h"
file_realpath = os.path.join(root_dir, file_path)
file_handle = open(file_realpath, "w")

header = "// Generated with {}\n".format(script_path)
header += "// Do not modify!"

header_id = "COMMON_MATH_CONSTANTS_H_"

write_float = True
write_double = True
write_long_double = False

# GLM constants not defined there:
# zero, one, three_over_two_pi, four_over_pi, root_half_pi, root_two_pi,
# root_ln_four, e, euler, root_five, ln_ln_two.

constants = []

class _phi:
	name = "phi"
	gnu = ""
	std = "phi"
	glm = "golden_ratio"
	operation = "φ"
	def compute(): return mp.phi

constants.append(_phi)

class _egamma:
	name = "egamma"
	gnu = ""
	std = "egamma"
	glm = ""
	operation = "Γe"
	def compute(): return gamma(e)

constants.append(_egamma)

class _ln2:
	name = "ln2"
	gnu = "M_LN2"
	std = "ln2"
	glm = "ln_two"
	operation = "ln(2)"
	def compute(): return ln(2)

constants.append(_ln2)

class _ln10:
	name = "ln10"
	gnu = "M_LN10"
	std = "ln10"
	glm = "ln_ten"
	operation = "ln(10)"
	def compute(): return ln(10)

constants.append(_ln10)

class _log2e:
	name = "log2e"
	gnu = "M_LOG2E"
	std = "log2e"
	glm = ""
	operation = "log2(e)"
	def compute(): return log(mp.e, 2)

constants.append(_log2e)

class _log10e:
	name = "log10e"
	gnu = "M_LOG10E"
	std = "log10e"
	glm = ""
	operation = "log10(e)"
	def compute(): return log(mp.e, 10)

constants.append(_log10e)

class _pi:
	name = "pi"
	gnu = "M_PI"
	std = "pi"
	glm = ""
	operation = "π"
	def compute(): return mp.pi

constants.append(_pi)

class _mul2_pi:
	name = "mul2_pi"
	gnu = ""
	std = ""
	glm = "two_pi"
	operation = "2π"
	def compute(): return fmul(2, mp.pi)

constants.append(_mul2_pi)

class _inv_pi:
	name = "inv_pi"
	gnu = "M_1_PI"
	std = "inv_pi"
	glm = "one_over_pi"
	operation = "1÷π"
	def compute(): return fdiv(1, mp.pi)

constants.append(_inv_pi)

class _inv_mul2_pi:
	name = "inv_mul2_pi"
	gnu = ""
	std = ""
	glm = "one_over_two_pi"
	operation = "1÷2π"
	def compute(): return fdiv(1, fmul(2, mp.pi))

constants.append(_inv_mul2_pi)

class _div2_pi:
	name = "div2_pi"
	gnu = "M_2_PI"
	std = ""
	glm = "two_over_pi"
	operation = "2÷π"
	def compute(): return fdiv(2, mp.pi)

constants.append(_div2_pi)

class _div180_pi:
	name = "div180_pi"
	gnu = ""
	std = ""
	glm = ""
	operation = "180÷π"
	def compute(): return fdiv(180, mp.pi)

constants.append(_div180_pi)

class _divpi_2:
	name = "divpi_2"
	gnu = "M_PI_2"
	std = ""
	glm = "half_pi"
	operation = "π÷2"
	def compute(): return fdiv(mp.pi, 2)

constants.append(_divpi_2)

class _divpi_4:
	name = "divpi_4"
	gnu = "M_PI_4"
	std = ""
	glm = "quarter_pi"
	operation = "π÷4"
	def compute(): return fdiv(mp.pi, 4)

constants.append(_divpi_4)

class _divpi_90:
	name = "divpi_90"
	gnu = ""
	std = ""
	glm = ""
	operation = "π÷90"
	def compute(): return fdiv(mp.pi, 90)

constants.append(_divpi_90)

class _divpi_180:
	name = "divpi_180"
	gnu = ""
	std = ""
	glm = ""
	operation = "π÷180"
	def compute(): return fdiv(mp.pi, 180)

constants.append(_divpi_180)

class _divpi_360:
	name = "divpi_360"
	gnu = ""
	std = ""
	glm = ""
	operation = "π÷360"
	def compute(): return fdiv(mp.pi, 360)

constants.append(_divpi_360)

class _sqrtpi:
	name = "sqrtpi"
	gnu = ""
	std = "sqrtpi"
	glm = "root_pi"
	operation = "√π"
	def compute(): return sqrt(mp.pi)

constants.append(_sqrtpi)

class _sqrt2:
	name = "sqrt2"
	gnu = "M_SQRT2"
	std = "sqrt2"
	glm = "root_two"
	operation = "√2"
	def compute(): return sqrt(2)

constants.append(_sqrt2)

class _sqrt3:
	name = "sqrt3"
	gnu = ""
	std = "sqrt3"
	glm = "root_three"
	operation = "√3"
	def compute(): return sqrt(3)

constants.append(_sqrt3)

class _inv_sqrtpi:
	name = "inv_sqrtpi"
	gnu = ""
	std = "inv_sqrtpi"
	glm = ""
	operation = "1÷√π"
	def compute(): return sqrt(mp.pi)

constants.append(_inv_sqrtpi)

class _inv_sqrt2:
	name = "inv_sqrt2"
	gnu = "M_SQRT1_2"
	std = "inv_sqrt2"
	glm = "one_over_root_two"
	operation = "1÷√2"
	def compute(): return fdiv(1, sqrt(2))

constants.append(_inv_sqrt2)

class _inv_sqrt3:
	name = "inv_sqrt3"
	gnu = ""
	std = "inv_sqrt3"
	glm = ""
	operation = "1÷√3"
	def compute(): return fdiv(1, sqrt(3))

constants.append(_inv_sqrt3)

class _div2_sqrtpi:
	name = "div2_sqrtpi"
	gnu = "M_2_SQRTPI"
	std = ""
	glm = "two_over_root_pi"
	operation = "2÷√π"
	def compute(): return fdiv(2, sqrt(mp.pi))

constants.append(_div2_sqrtpi)

class _inv_3:
	name = "inv_3"
	gnu = ""
	std = ""
	glm = "third"
	operation = "1÷3"
	def compute(): return fdiv(1, 3)

constants.append(_inv_3)

class _div2_3:
	name = "div2_3"
	gnu = ""
	std = ""
	glm = "two_third"
	operation = "2÷3"
	def compute(): return fdiv(2, 3)

constants.append(_div2_3)

types = []

class _float:
	name = "float"
	slug = "_f"
	suffix = "f"
	has_std = True
	std_suffix = "f"
	precision = 9

if write_float:
	types.append(_float)

class _double:
	name = "double"
	slug = "_d"
	suffix = ""
	has_std = True
	std_suffix = ""
	precision = 20

if write_double:
	types.append(_double)

class _long_double:
	name = "long double"
	slug = "_l"
	suffix = "l"
	has_std = False
	std_suffix = ""
	precision = 37

if write_long_double:
	types.append(_long_double)

def beautify(v):
	value = str(v);
	e = 0

	if value.startswith("0."):
		value = value[1:]

	while value.startswith("."):
		value = "{}{}{}".format(value[1], ".", value[2:])
		e -= 1

		if value.startswith("0."):
			value = value[1:]

	if "." in value:
		if len(value) < mp.dps + 1:
			value = "{}{}".format(value, "0")

	sign = ["+", "-"][e < 0]
	exponent = "e{}{}".format(sign, abs(e))

	return "{}{}".format(value, exponent)

def output(string=""):
	print(string)
	print(string, file=file_handle)

keyword = "constexpr"
value = "0"

keyword_pad = len(keyword)
value_pad = len(value)

type_head = "Type"
name_head = "Name"
value_head = "Value"
operation_head = "Operation"
gnu_head = "GNU"
std_head = "std::numbers"
glm_head = "GLM"

output(header)
output()
output("#ifndef {}".format(header_id))
output("#define {}".format(header_id))
output()
output("namespace Math {")

first = True

for t in types:
	if first:
		first = False
	else:
		output()

	mp.dps = t.precision

	name_pad = len(name_head)
	operation_pad = len(operation_head)
	gnu_pad = len(gnu_head)
	std_pad = [0, len(std_head)][t.has_std]
	type_pad = len(t.name)

	for c in constants:
		name = c.name + t.slug

		l = len(name)
		if l > name_pad:
			name_pad = l

		l = len(c.operation)
		if l > operation_pad:
			operation_pad = l

		if t.has_std:
			l = len(c.std)
			if l > std_pad:
				std_pad = l

		gnu = c.gnu

		if gnu:
			gnu = gnu + t.suffix

		l = len(gnu)
		if l > gnu_pad:
			gnu_pad = l

		value = beautify(c.compute()) + t.suffix

		l = len(value)
		if l > value_pad:
			value_pad = l

	operation_pad += 2
	std_pad += [0, 2][t.has_std]
	gnu_pad += 2

	std = ["", std_head][t.has_std]

	sizes = keyword_pad, type_pad, name_pad, value_pad, operation_pad, std_pad, gnu_pad
	string = "\t{:<%s} {:<%s} {:<%s} {:<1} {:>%s}{:<1} {:<2} {:<%s}{:<%s}{:<%s}{}" % sizes

	fields = "//", type_head, name_head, "", value_head, "", "", operation_head, std, gnu_head, glm_head
	output(string.format(*fields).rstrip())

	for c in constants:
		name = c.name + t.slug

		gnu = c.gnu
		if gnu:
			gnu = gnu + t.suffix

		if t.has_std:
			std = c.std
			if std:
				std = std + t.std_suffix
		else:
			std = ""

		glm = c.glm

		value = beautify(c.compute()) + t.suffix

		fields = keyword, t.name, name, "=", value, ";", "//", c.operation, std, gnu, glm
		output(string.format(*fields).rstrip())

output("}")
output()
output("#endif // {}".format(header_id))

file_handle.close()
