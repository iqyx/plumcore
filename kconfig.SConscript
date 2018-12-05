Import("env")
Import("conf")
Import("objs")

def AddSourceIf(self, kconfig, src, inc):
	if conf[kconfig] == "y":
		objs.append(env.Object(src))
		env.Append(CPPPATH = inc)

env.AddMethod(AddSourceIf)

