Import("env")

objs = env.Object(source = [
	File(Glob("common/*.c")),
	File(Glob("interfaces/*.c")),
	File(Glob("modules/*.c")),
])

# Do not append uHal subdirectories to the CPPPATH variable.
# Header files should be referenced with #include <uhal/interfaces/...>
env.Append(CPPPATH = [
	Dir("."),
	Dir("common"),
])

Return("objs")
