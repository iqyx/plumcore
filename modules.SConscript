Import("env")

env["BUILD_MODULES"] = []

def DefineModule(self, module_name, module_objs):
	self.StaticLibrary(module_name, module_objs)
	self.AppendUnique(LIBPATH = [Dir(".")])
env.AddMethod(DefineModule)

def DependsOnModule(self, module_name):
	self.AppendUnique(BUILD_MODULES = module_name)
env.AddMethod(DependsOnModule)

def RecommendsModule(self, module_name):
	pass
env.AddMethod(RecommendsModule)

# Enclose all libraries in a group
env.Replace(LINKCOM = '$LINK -o $TARGET $LINKFLAGS $__RPATH $SOURCES $_LIBDIRFLAGS -Wl,--start-group $_LIBFLAGS -Wl,--end-group')

