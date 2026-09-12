Import("env")

import os
import git
import subprocess
from colorama import Fore, Style

# External-library fetching machinery, shared by every library (and the FreeRTOS) SConscript.
# Sourced early from the top-level SConstruct so env.Git/env.Patch/env.Make are available before
# lib/ and services/ are evaluated. SBOM concerns live separately in sbom.SConscript, which is
# sourced right after this one and wraps env.Git.

# Directory (relative to each library SConscript) into which upstream sources are cloned.
env.Replace(LIB_DEFAULT_REPO_DIR = "src/")


def _stamp(fn):
	with open(fn, 'w') as f:
		pass


def Git(self, url, dir=env['LIB_DEFAULT_REPO_DIR'], branch=None):
	if os.path.exists('.downloaded.stamp'):
		return

	print(f'{Fore.BLUE}{Style.BRIGHT}Cloning git repo{Style.RESET_ALL} {url} into {dir}...')
	git.Repo.clone_from(url, dir, branch=branch, depth=1)
	_stamp('.downloaded.stamp')

env.AddMethod(Git)


def Patch(self, ppatch, dir=env['LIB_DEFAULT_REPO_DIR']):
	if os.path.exists('.patched.stamp'):
		return

	if type(ppatch) is not list:
		ppatch = [ppatch]

	r = git.Repo(dir)
	for p in ppatch:
		# The patch file name is library SConscript file relative, but Git needs it as
		# repository-relative.
		abspatch = os.path.abspath(p)
		print(f'{Fore.BLUE}{Style.BRIGHT}Patching{Style.RESET_ALL} with {p}...')
		r.git.apply([str(abspatch)])

	_stamp('.patched.stamp')

env.AddMethod(Patch)


def Make(self, target, cwd='.'):
	if os.path.exists('.compiled.stamp'):
		return

	print(f'{Fore.BLUE}{Style.BRIGHT}Compiling{Style.RESET_ALL} library target {target} (in {str(cwd)})...')

	r = subprocess.run(['make', target], cwd=str(cwd), stdout=subprocess.PIPE)
	print(r.stdout.decode())

	_stamp('.compiled.stamp')

env.AddMethod(Make)
