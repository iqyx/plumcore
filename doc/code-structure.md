# Code structure, contributing

To make the development easy and available to masses, plumCore is adopting some common standards to manage code and releases:

## Git version control system

## Gitflow workflow

## Contributing

All contributions are commited to *feature* branches. Developers from within the main development team can contribute to the mainline hosted at `GitHub`.

All other contributors must fork the mainline repository on `GitHub`, push their changes there and create a pull request.

After all required tests are completed successfuly and all the required reviewers approve the PR, it can be merged into the *develop* branch.

## Semantic versioning

The plumCore project uses Semantic Versioning 2.0.0 for all its build releases, that is for:

- stable releases from the release branches (`release/<version>`)
- development releases from the develop branch (`<version>-dev.<num>`), where `version` is MAJOR.MINOR.PATCH, `num` is the number of commits since the last tagged development release
- alpha, beta, RC releases from the release branches (`<version>-a.<num>`, `<version>-b.<num>`, `<version>-rc.<num>`), where `version` is MAJOR.MINOR.PATCH and `num` is the number of the release version
- feature/testing releases form the release branches (`<version>-<name>.<num>`), where where `version` is MAJOR.MINOR.PATCH of the last tagged development branch, `name` is the feature name and `num` is the number of commits since the feature was branched off the development branch

## Conventional Commits

As the page at <https://www.conventionalcommits.org/en/v1.0.0/> says:

*The Conventional Commits specification is a lightweight convention on top of commit messages. It provides an easy set of rules for creating an explicit commit history; which makes it easier to write automated tools on top of. This convention dovetails with SemVer, by describing the features, fixes, and breaking changes made in commit messages.*

We are using commint messages in the format:

```text
type(scope): description

[optional body]

[optional footer(s)]
```

Where `type` is one of the following:

- `fix`: a commit of the type `fix` patches a bug in your codebase (this correlates with `PATCH` in Semantic Versioning).
- `feat`: a commit of the type `feat` introduces a new feature to the codebase (this correlates with `MINOR` in Semantic Versioning).
- breaking change: a commit that appends a `!` after the `type/scope`, introduces a breaking API change (correlating with `MAJOR` in Semantic Versioning). A breaking change can be part of commits of any type.

There are additional types available when the commit doesn't fall into one of the previous categories:

- `docs` for documentation only changes which do not modify the resulting build
- `style` changes not modifying the code meaning (whitespaces, formatting, etc.)
- `refactor` changes in code not introducing any new features nor bug fixes
- `chore` for changes in the build system, tools, repository configuration, etc.

Scope is the changed part of the plumCore framework:

- services are shortened to `srv`
- `kernel` for all changes in the microkernel and implementation of the basic IPC
- `app/name` - changes in the application named `name`
- `port/name` - changes in the corresponding port code
- `plat/name` - changes in the corresponding platform code

Footers are written in the format described in <https://git-scm.com/docs/git-interpret-trailers>

## Continuous Integration

## Release artifacts

Artifacts are being made as a result of the build process. There are multiple build processes producing different kinds of artifacts in the plumCore project:

### Firmware builds

Firmware builds are located in the `bin/` directory. There are several:

- ELF images with debug symbols
- MAP files to facilitate debugging
- stripped ELF images (optionally) loadable by the plumCore bootloader/chainloader
- signed ELF images with a signing section added for loading in signature-verification enabled bootloader

### Documentation builds

They are located in the `doc/build/` directory.
