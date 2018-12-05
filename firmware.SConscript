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
