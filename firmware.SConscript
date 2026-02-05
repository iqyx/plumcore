import subprocess
import os
from tempfile import mkstemp

Import("env")
Import("conf")
Import("objs")


image_elf_size = Action("$SIZE $TARGET", env["IMAGEELFSIZECOMSTR"])
image_elf_headers = Action("$READELF --program-headers $TARGET", env["IMAGEELFHDRCOMSTR"])
image_elf_sections = Action("$READELF --sections $TARGET", env["IMAGEELFHDRCOMSTR"])

###################################################################
# Linking / ELF image generation
###################################################################

image_elf = env.Program(
	source = objs,
	target = [
		File(env["PORTFILE"] + ".elf"),
		File(env["PORTFILE"] + ".map"),
	],
)

# Always relink the resulting ELF and print ELF statistics. The map file is also rebuilt.
if conf["FW_IMAGE_ELF"] == "y":
	Alias("firmware", image_elf)
	AlwaysBuild(image_elf)
	AddPostAction(image_elf, image_elf_size)


###################################################################
# Flatten the ELF to a binary if requested to do so
###################################################################

image_bin = env.Command(
	target = env["PORTFILE"] + ".bin",
	source = env["PORTFILE"] + ".elf",
	action = Action("$OBJCOPY -O binary $SOURCE $TARGET", env["CREATEBINCOMSTR"])
)

if conf["ELF_IMAGE_TO_BIN"] == "y":
	Alias("firmware", image_bin)
	AlwaysBuild(image_bin)


###################################################################
# Generate XIP ELF
###################################################################

sk = conf["ELF_SIGNING_KEY"]
image_xip = env.Command(
	target = env["PORTFILE"] + ".elf.xip",
	source = env["PORTFILE"] + ".elf",
	action = [
		Action('$STRIP $SOURCE -o ${SOURCE}.strip', env["CREATEXIPCOMSTR"]),
		Action('cp ${SOURCE}.strip ${SOURCE}.xip'),
		Action(f'scripts/elfsign.py --sk {conf["ELF_SIGNING_KEY"]} --sign ${{SOURCE}}.xip'),
		Action(f'scripts/elfxz.py --xz ${{TARGET}}'),
	]
)

if conf["ELF_IMAGE_XIP"] == "y":
	Alias("firmware", image_xip)
	AlwaysBuild(image_xip)
	AddPostAction(image_xip, image_elf_size)
	AddPostAction(image_xip, image_elf_headers)


###################################################################
# openocd programming
###################################################################

program_source = None
if conf["FW_IMAGE_ELF"] == "y":
	program_source = image_bin
if conf["ELF_IMAGE_XIP"] == "y":
	program_source = image_xip


oocd = env.Command(
	source = program_source,
	target = "oocd",
	action = """
	$OOCD \
	-s /usr/share/openocd/scripts/ \
	-f interface/%s.cfg \
	-f target/%s.cfg \
	-c "init" \
	-c "reset init" \
	-c "flash write_image erase $SOURCE %s bin" \
	-c "reset" \
	-c "shutdown"
	""" % (env["OOCD_INTERFACE"], env["OOCD_TARGET"], conf["ELF_IMAGE_LOAD_ADDRESS"])
)

env.Alias("program", oocd);


###################################################################
# GDB programming
###################################################################

gdb_load = env.Command(
	source = image_elf,
	target = "gdb",
	action = """
	$GDB \
	-ex "target extended-remote %s" \
	-ex "load" \
	-ex "monitor reset" \
	-ex "set confirm off" \
	-ex "quit" \
	$SOURCE
	""" % (conf["GDB_REMOTE_TARGET"])
)

env.Alias("gdb-program", gdb_load);


###################################################################
# GDB debugging
###################################################################

gdb_debug = env.Command(
	source = image_elf,
	target = "debug",
	action = """
	$GDB \
	-ex "target extended-remote %s" \
	$SOURCE
	""" % (conf["GDB_REMOTE_TARGET"])
)

env.Alias("debug", gdb_debug);

###################################################################
# Size statistics
###################################################################

def stats_builder_func(target, source, env):
	o = subprocess.run([env['NM'], '--size-sort', '-l', str(source[0])], capture_output=True, text=True).stdout
	files = {}
	for l in o.split('\n'):
		cols = l.split()
		if len(cols) == 3:
			cols.append('other:0')
		if len(cols) < 4:
			continue
		(code_len, section, name, filename,) = cols
		(filename, line, ) = filename.split(':')
		if filename:
			filename = os.path.relpath(filename)

		if files.get(filename) == None:
			files[filename] = {}
		files[filename][name] = code_len;

	sizes = {}
	for k, v in files.items():
		total_size = 0;
		for function_size in v.values():
			total_size += int(function_size, base=16)
		sizes[k] = total_size

	for k in sorted(sizes, key=sizes.get):
		print(k, sizes[k])


stats_builder = Builder(action=Action(stats_builder_func, env["STATSCOMSTR"]))
env.Append(BUILDERS={'Stats': stats_builder})

size_stats = env.Stats(
	source = image_elf,
	target = 'stats',
)

env.Alias("stats", size_stats);
