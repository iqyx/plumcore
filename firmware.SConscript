import subprocess
import os

Import("env")
Import("conf")
Import("objs")


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

image_elf_size = env.Command(
	source = image_elf,
	target = None,
	action = Action("$SIZE $SOURCE", env["IMAGEELFSIZECOMSTR"])
)

if conf["FW_IMAGE_ELF"] == "y":
	Alias("firmware", image_elf_size)


image_bin = env.Command(
	target = env["PORTFILE"] + ".bin",
	source = env["PORTFILE"] + ".elf",
	action = Action("$OBJCOPY -O binary $SOURCE $TARGET", env["CREATEBINCOMSTR"])
)

if conf["ELF_IMAGE_TO_BIN"] == "y":
	Alias("firmware", image_bin)



###################################################################
# uBLoad firmware generation
###################################################################

# UBF image generator needs a BIN image first
image_ubf_source = image_bin
image_ubf_cmd = "$CREATEFW --base 0x08010000 --input $SOURCE --output $TARGET"
image_ubf_cmd += " --version \"$VERSION\""
image_ubf_cmd += " --compatibility \"meh 0.x.x\""

if conf["FW_IMAGE_HASH"] == "y":
	image_ubf_cmd += " --check"


# If a firmware signing is requested, prepare a signing key (generate if requested),
# append the required parameters and the signing key dependency
if conf["FW_IMAGE_SIGN"] == "y":
	signing_key = conf["FW_IMAGE_SIGN_KEY"]

	if conf["FW_IMAGE_SIGN_KEY_GENERATE"] == "y":
		env.Command(
			target = signing_key,
			source = None,
			action = Action("dd if=/dev/urandom bs=32 count=1 of=$TARGET", env["CREATEKEYCOMSTR"])
		)

	image_ubf_source.append(signing_key);
	image_ubf_cmd += " --sign " + signing_key

image_ubf = env.Command(
	target = env["PORTFILE"] + ".ubf",
	source = image_ubf_source,
	action = Action(image_ubf_cmd, env["CREATEUBFCOMSTR"])
)

if conf["FW_IMAGE_UBLOAD"] == "y":
	Alias("firmware", image_ubf)


###################################################################
# openocd programming
###################################################################

program_source = None
if conf["FW_IMAGE_ELF"] == "y":
	program_source = image_bin
if conf["FW_IMAGE_UBLOAD"] == "y":
	program_source = image_ubf

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
	""" % (env["OOCD_INTERFACE"], env["OOCD_TARGET"], env["LOAD_ADDRESS"])
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
