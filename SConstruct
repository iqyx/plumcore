#
# Main plumCore build file
#
# Copyright (c) 2017, Marek Koza (qyx@krtko.org)
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


import os
import time
import yaml


## @todo Move somewhere else
class term_format:
	default = "\x1b[0m"
	bold = "\x1b[1m"
	black = "\x1b[30m"
	red = "\x1b[31m"
	green = "\x1b[32m"
	yellow = "\x1b[33m"
	blue = "\x1b[34m"
	magenta = "\x1b[35m"
	cyan = "\x1b[36m"
	white = "\x1b[37m"

def cformat(s):
	return s.format(c = term_format)

def make_ver(target, source, env):
	for t in target:
		with open(str(t), "w") as f:
			f.write("#define UMESH_VERSION \"%s, %s, %s\"\n" % (env["GIT_BRANCH"], env["GIT_REV"], env["GIT_DATE"]))
			f.write("#define UMESH_BUILD_DATE \"%s\"\n" % env["BUILD_DATE"])

	return None

## @todo Check for the nanopb installation. Request download and build of the library
##       if it is not properly initialized.
def build_proto(target, source, env):
	for s in source:
		print "Building proto %s" % s
		os.system("protoc --plugin=lib/other/nanopb/generator/protoc-gen-nanopb --proto_path=umesh/proto --nanopb_out=umesh/proto --proto_path=lib/other/nanopb/generator/proto %s" % s)


# Create default environment and export it. It will be modified later
# by port-specific and other build scripts.
env = Environment()
Export("env")

# Load build configuration
env["CONFIG"] = yaml.load(file("config/default.yaml", "r"))
# print env["CONFIG"]
env["PORT"] = env["CONFIG"]["build"]["port"]

# Get PATH directories from os environment
## @todo remove
env.Append(ENV = {"PATH": os.environ["PATH"] + ":/opt/gcc-arm-none-eabi-4.9/bin/"})

# Get some statistics from git repo about currently built sources
## @todo provide some defaults if this is not a git repository
env["GIT_BRANCH"] = os.popen("git rev-parse --abbrev-ref HEAD").read().rstrip()
env["GIT_REV"] = os.popen("git rev-parse --short HEAD").read().rstrip()
env["GIT_DATE"] = os.popen("git log -1 --format=%ci").read().rstrip()

# create build version information
#~ env["BUILD_DATE"] = time.ctime(time.time())
env["BUILD_DATE"] = time.strftime("%F %T %Z", time.localtime())

if ARGUMENTS.get('VERBOSE') != "1":
	env['CCCOMSTR'] = cformat("Compiling {c.green}$TARGET{c.default}")
	env['LINKCOMSTR'] = cformat("Linking {c.bold}{c.green}$TARGET{c.default}")
	env['ARCOMSTR'] = cformat("{c.green}{c.bold}Creating library $TARGET{c.default}")

objs = []

# Run port-specific SConscript file
objs.append(SConscript("ports/%s/SConscript" % env["PORT"]))

env["CC"] = "%s-gcc" % env["TOOLCHAIN"]
env["CXX"] = "%s-g++" % env["TOOLCHAIN"]
env["AR"] = "%s-ar" % env["TOOLCHAIN"]
env["AS"] = "%s-as" % env["TOOLCHAIN"]
env["LD"] = "%s-gcc" % env["TOOLCHAIN"]
env["OBJCOPY"] = "%s-objcopy" % env["TOOLCHAIN"]
env["OBJDUMP"] = "%s-objdump" % env["TOOLCHAIN"]
env["SIZE"] = "%s-size" % env["TOOLCHAIN"]
env["OOCD"] = "openocd"
env["CREATEFW"] = "tools/createfw.py"

# Add platform specific things
env.Append(CPPPATH = [Dir("ports/%s" % env["PORT"])])

# Add uMeshFw HAL
env.Append(CPPPATH = [
	Dir("hal/common"),
	Dir("hal/modules"),
	Dir("hal/interfaces"),
])
objs.append(env.Object(source = [
	File(Glob("hal/common/*.c")),
	File(Glob("hal/modules/*.c")),
	File(Glob("hal/interfaces/*.c")),
]))

objs.append(SConscript("uhal/SConscript"))
objs.append(SConscript("system/SConscript"))
objs.append(SConscript("protocols/SConscript"))
objs.append(SConscript("lib/SConscript"))
objs.append(SConscript("services/SConscript"))


env.Append(CPPPATH = [Dir(".")])


env.Append(LINKFLAGS = [
	env["CFLAGS"],
	"--static",
	"-nostartfiles",
	"--specs=nano.specs",
	"-T", env["LDSCRIPT"],
	"-Wl,-Map=bin/umeshfw_%s.map" % env["PORT"],
	"-Wl,--gc-sections",
])

env.Append(CFLAGS = [
	"-Os",
	"-g",
	#~ "-flto",
	"-fno-common",
	"-fdiagnostics-color=always",
	"-ffunction-sections",
	"-fdata-sections",
	"--std=c99",
	"-Wall",
	"-Wextra",
	"-pedantic",
	#~ "-Werror",
	"-Winit-self",
	"-Wreturn-local-addr",
	"-Wswitch-default",
	"-Wuninitialized",
	"-Wundef",
	#~ "-Wstack-usage=256",
	"-Wshadow",
	"-Wimplicit-function-declaration",
	"-Wcast-qual",
	#~ "-Wwrite-strings",
	#~ "-Wconversion",
	"-Wlogical-op",
	"-Wmissing-declarations",
	"-Wno-missing-field-initializers",
	"-Wstack-protector",
	"-Wredundant-decls",
	"-Wmissing-prototypes",
	"-Wstrict-prototypes",
])



# show banner and build configuration
print cformat("{c.bold}{c.blue}uMeshFw branch %s, revision %s, date %s{c.default}\n" % (env["GIT_BRANCH"], env["GIT_REV"], env["GIT_DATE"]))
print cformat("{c.bold}Build configuration:{c.default}")
print cformat("\tbuild date = %s" % env["BUILD_DATE"])
print cformat("\tport = %s" % env["PORT"])
print cformat("\ttoolchain = %s" % env["TOOLCHAIN"])
print ""

# link the whole thing
elf = env.Program(source = objs, target = "bin/umeshfw_%s.elf" % env["PORT"], LIBS = [env["LIBOCM3"], "c", "gcc", "nosys"])

env.Append(BUILDERS = {"MakeVer": env.Builder(action = make_ver)})
version = env.MakeVer(target = "ports/%s/version.h" % env["PORT"], source = None)
Depends(elf, version)

# convert to raw binary and create fw image
rawbin = env.Command("bin/umeshfw_%s.bin" % env["PORT"], elf, "$OBJCOPY -O binary $SOURCE $TARGET")
fwimage = env.Command("bin/umeshfw_%s.fw" % env["PORT"], rawbin, """
	$CREATEFW \
	--base 0x08010000 \
	--input $SOURCE \
	--output $TARGET \
	--check \
	--sign bin/test_key.key \
	--version 0.1.0 \
	--compatibility "qnode4 0.x.x"
""")

proto = env.Command(
	source = [File(Glob("umesh/proto/*.proto"))],
	target = "protoc",
	action = build_proto
)

elfsize = env.Command(source = elf, target = "elfsize", action = "$SIZE $SOURCE")

program = env.Command(source = fwimage, target = "program", action = """
	$OOCD \
	-s /usr/share/openocd/scripts/ \
	-f interface/%s.cfg \
	-f target/%s.cfg \
	-c "init" \
	-c "reset init" \
	-c "flash write_image erase %s 0x08010000 bin" \
	-c "reset" \
	-c "shutdown"
""" % (env["OOCD_INTERFACE"], env["OOCD_TARGET"], str(fwimage[0])))

# And do something by default.
env.Alias("umeshfw", fwimage)
env.Alias("proto", proto);
Default(elf, elfsize, rawbin, fwimage)

