Import("env")
Import("conf")

from git import Repo
import time


repo = Repo(".")
v = "0.0.0"
if str(repo.head.ref) == "develop":
	# We are currently in the develop branch, find all dev tags (eg. 1.0.0-dev) and grab the last one
	dev_tag = [str(tag) for tag in repo.tags if str(tag)[-3:] == "dev"][-1]

	# In the develop branch the version is identical to the dev tag name and a count of
	# commits from the dev tag to the HEAD in the "dev.x" format
	commits_from_dev_tag = len(list(repo.iter_commits(dev_tag + "...develop")))
	v = dev_tag + ".%d" % commits_from_dev_tag;


if str(repo.head.ref)[:8] == "feature/":
	dev_branch = str(repo.head.ref)
	dev_feature = dev_branch[8:]
	dev_tag = [str(tag)[:-4] for tag in repo.tags if str(tag)[-3:] == "dev"][-1]
	commits_from_develop = len(list(repo.iter_commits("develop..." + dev_branch)))
	v = dev_tag + "-" + dev_feature + ".%s" % commits_from_develop


if repo.head.ref == repo.heads.master:
	v = str(next((tag for tag in repo.tags if tag.commit == repo.head.commit), "0.0.0"))


v += "+" + repo.head.commit.hexsha[:7]
v += "." + time.strftime("%Y%m%d")

env["VERSION"] = v

env["BUILD_DATE"] = time.strftime("%F %T %Z", time.localtime())

def make_ver(target, source, env):
	for t in target:
		with open(str(t), "w") as f:
			f.write("#define UMESH_VERSION \"%s\"\n" % env["VERSION"])
			f.write("#define UMESH_BUILD_DATE \"%s\"\n" % env["BUILD_DATE"])

	return None

env.Append(BUILDERS = {"MakeVer": env.Builder(action = make_ver)})
version = env.MakeVer(target = "ports/%s/version.h" % conf["PORT_NAME"], source = None)
