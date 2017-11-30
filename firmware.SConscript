Import("env")


# signing_key = "bin/test_key.key"
signing_key = env.get("FW_SIGNING_KEY")
fw_sources = [env["PORTFILE"] + ".bin"]

env.Command(
	target = env["PORTFILE"] + ".bin",
	source = env["PORTFILE"] + ".elf",
	action = Action("$OBJCOPY -O binary $SOURCE $TARGET", env["CREATEBINCOMSTR"])
)


createfw_cmd = "$CREATEFW --base 0x08010000 --input $SOURCE --output $TARGET --check"
createfw_cmd += " --version \"$VERSION\""
createfw_cmd += " --compatibility \"meh 0.x.x\""

if signing_key:
	if env.get("FW_CREATE_KEY"):
		env.Command(
			target = signing_key,
			source = None,
			action = Action("dd if=/dev/urandom bs=32 count=1 of=$TARGET", env["CREATEKEYCOMSTR"])
		)
	fw_sources.append(signing_key);
	createfw_cmd += " --sign " + signing_key

env.Command(
	target = env["PORTFILE"] + ".fw",
	source = fw_sources,
	action = Action(createfw_cmd, env["FWCREATECOMSTR"])
)

