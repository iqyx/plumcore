Import("env")
Import("conf")
Import("objs")

doc = env.Command(
	source = File(Glob("*.rst")),
	target = File("doc/build/index.html"),
	action = Action("sphinx-build -b html -c doc/ . doc/build/")
)

env.Alias("doc", doc);
