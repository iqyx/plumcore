import kconfiglib

Import("env")
Import("conf")
Import("objs")

def AddSourceIf(self, kconfig, src, inc, cflags = []):
	if conf[kconfig] == "y":
		objs.append(env.Object(src))
		env.Append(CPPPATH = inc)
		env.Append(CFLAGS = cflags)


def LoadKconfig(self, kconfig_file, config_file):
	kconf = kconfiglib.Kconfig(kconfig_file)
	try:
		kconf.load_config(config_file)
	except kconfiglib._KconfigIOError:
		print("Error: no configuration found in the .config file. Run menuconfig or alldefconfig to create one.")
		exit(1)

	for n in kconf.node_iter():
		if isinstance(n.item, kconfiglib.Symbol):
			conf[n.item.name] = n.item.str_value;


env.AddMethod(LoadKconfig)
env.AddMethod(AddSourceIf)

