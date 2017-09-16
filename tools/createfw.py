#!/usr/bin/python

import argparse
import struct
import hashlib
import ed25519

class section_magic:
	verification = 0x6ef44bc0
	verified = 0x1eda84bc
	dummy = 0xba50911a
	firmware = 0x40b80c0f
	sha512 = 0xb6eb9721
	ed25519 = 0x9d6b1a99
	fp = 0x5bf0aa39
	version = 0x44ed31be
	compatibility = 0x3a1e112b

section_names = {
	section_magic.verification: "verification",
	section_magic.verified: "verified",
	section_magic.dummy: "dummy",
	section_magic.firmware: "firmware",
	section_magic.sha512: "sha512 hash",
	section_magic.ed25519: "ed25519 signature",
	section_magic.fp: "pubkey fingerprint",
	section_magic.version: "firmware version",
	section_magic.compatibility: "hardware compatibility"
}

def build_section(section_magic, data):
	return struct.pack("!LL", section_magic, len(data)) + data

def pad_metadata(data, req_len):
	if len(data) > req_len:
		return None
	if len(data) == req_len:
		return data
	if len(data) > (req_len - 8):
		return None

	padding_size = req_len - len(data) - 8
	return data + build_section(section_magic.dummy, " " * padding_size)


def print_section(data, offset, indent):
	if indent > 1:
		return "", 0

	magic, length = struct.unpack("!LL", data[:8])
	section_name = section_names[magic];

	# Strip header
	data = data[8:]

	# Get this section data only
	section_data = data[:length]
	section_data_offset = offset + 8
	print "%s%s section at 0x%08x, data 0x%08x, len %u bytes" % ("\t" * indent, section_name, offset, section_data_offset, length)

	while len(section_data) > 0:
		section_data, section_data_offset = print_section(section_data, section_data_offset, indent + 1)

	# Return the rest.
	return data[length:], offset + 8 + length


def print_sections(data, offset):
	while len(data) > 0:
		data, offset = print_section(data, offset, 0)


parser = argparse.ArgumentParser(description = "uBLoad firmware creator")
parser.add_argument(
	"--input", "-i",
	metavar = "binfile",
	dest = "binfile",
	required = True,
	type = str,
	help = "Input binary file"
)
parser.add_argument(
	"--output", "-o",
	metavar = "fwfile",
	dest = "fwfile",
	required = True,
	type = str,
	help = "Output uBLoad firmware image file"
)
parser.add_argument(
	"--sign", "-s",
	metavar = "KEY",
	dest = "signfile",
	help = "Sign the firmware image using specified private key."
)
parser.add_argument(
	"--check", "-c",
	action = "store_true",
	help = "Add firmware hash for integrity checking."
)
parser.add_argument(
	"--hash-type",
	dest = "hash_type",
	metavar = "HASH",
	default = "sha512",
	help = "Specify hash type for signing/integrity checking."
)
parser.add_argument(
	"--base", "-b",
	dest = "fw_base",
	metavar = "hex",
	default = "0x08008000",
	help = "Firmware base address for loading."
)
parser.add_argument(
	"--offset",
	dest = "fw_offset",
	metavar = "hex",
	default = "0x400",
	help = "Offset of the vector table inside the image."
)
parser.add_argument(
	"--version", "-v",
	metavar = "V",
	dest = "version",
	type = str,
	help = "Version of the firmware image"
)
parser.add_argument(
	"--compatibility",
	dest = "compatibility",
	metavar = "C",
	action = "append",
	type = str,
	help = "Hardware compatiblity string"
)
args = parser.parse_args()


# Initialize two required sections
fw_verified = ""
fw_verification = ""

# TODO: populate verified section metadata here
if args.version:
	fw_verified += build_section(section_magic.version, args.version)

if args.compatibility:
	for c in args.compatibility:
		fw_verified += build_section(section_magic.compatibility, c)

# Verified section header is exactly 8 bytes long, firmware image header has the
# same length. Therefore, verified data must be fw_offset - 16 bytes long. Print
# error if it overflows or pad to required size. Length if padding header must
# also be considered (another 8 bytes).
if len(fw_verified) > (int(args.fw_offset, 16) - 24):
	print "Firmware metadata too big (doesn't fit in the specified firmware offset)";
	exit(1)

# Fill verified section to match required firmware offset.
fw_verified = pad_metadata(fw_verified, int(args.fw_offset, 16) - 16)

# And append input binary file
try:
	with open(args.binfile, "r") as f:
		fw_verified += build_section(section_magic.firmware, f.read())
except:
	print "Cannot read input file '%s'" % args.binfile
	exit(1)


# Now the verified section is complete. Compute required hashes and other
# authentication data.
fw_hash = ""
if args.check:
	if args.hash_type == "sha512":
		fw_hash = hashlib.sha512(fw_verified).digest()
		fw_verification += build_section(section_magic.sha512, fw_hash)
	else:
		print "Unknown hash specified."
		exit(1)


fw_signature = ""
if args.signfile:

	if not args.check:
		print "Firmware check hash required for signing, use --check"
		exit(1)

	priv_key = ""
	try:
		with open(args.signfile, "r") as f:
			priv_key = f.read()
	except:
		print "Cannot read key file '%s'" % args.signfile
		exit(1)

	pub_key = ed25519.publickey(priv_key)

	fw_signature = ed25519.signature(fw_hash, priv_key, pub_key)
	fw_verification += build_section(section_magic.ed25519, fw_signature)

	# Append pubkey fingerprint.
	pub_key_fp = hashlib.sha512(pub_key).digest()
	pub_key_fp = pub_key_fp[:4]
	fw_verification += build_section(section_magic.fp, pub_key_fp)


# This variable contains full firmware image with all required parts
# Verified and verification sections are always present. Verification
# section can be empty.
fw_image = build_section(section_magic.verified, fw_verified) + build_section(section_magic.verification, fw_verification)

# Print resulting firmware image structure
print_sections(fw_image, int(args.fw_base, 16))

# Save the image to selected file.
try:
	with open(args.fwfile, "w") as f:
		f.write(fw_image)
except:
	print "Cannot write output firmware file '%s'" % args.fwfile
	exit(1)


