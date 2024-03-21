#!/usr/bin/env python3

import argparse
from textwrap import dedent
import os
import tempfile
from subprocess import run
from base64 import b64decode, b64encode
import ed25519
from hashlib import blake2s


def load_file(fname):
	with open(fname, 'rb') as f:
		contents = f.read()
	return contents

def save_file(fname, contents):
	with open(fname, 'wb') as f:
		f.write(contents)


parser = argparse.ArgumentParser(
	description=dedent("""
		ELF firmware signing tool
	"""),
	epilog="(c) 2024 Marek Koza <qyx@krtko.org>",
	formatter_class=argparse.RawDescriptionHelpFormatter
)

parser.add_argument('--sk', type=str, help='Signing private key (base64)')
parser.add_argument('--pk', type=str, help='Public key for verification (base64)')
parser.add_argument('-s', '--sign', type=str, default=None, help='ELF firmware image to sign')
parser.add_argument('-v', '--verify', type=str, default=None, help='ELF firmware image to verify')
parser.add_argument('-p', '--save-pubkey', type=str, default=None, help='Save public key to a file')
args = parser.parse_args()

if args.sk:
	sk = b64decode(load_file(args.sk))
	pk = ed25519.publickey_unsafe(sk)
	
if args.pk:
	pk = b64decode(load_file(args.pk))


if args.sign:

	# Generate an empty file with 64 zeros and add it as a sign.ed25519 section
	f = tempfile.NamedTemporaryFile(delete=False)
	f.write(b'\x00' * 64)
	f.close()
	run(f'arm-none-eabi-objcopy --remove-section .sign.ed25519 --add-section .sign.ed25519="{f.name}" {args.sign}', shell=True)
	# Do not delete the file yet

	# Sign the empty elf now
	elf = load_file(args.sign)
	hm = blake2s(elf).digest()
	sm = ed25519.signature_unsafe(hm, sk, pk)
	save_file(f.name, sm);
	print(f'hm = {hm.hex()}')
	print(f'sm = {sm.hex()}')

	# Attach the signature
	run(f'arm-none-eabi-objcopy --update-section .sign.ed25519="{f.name}" {args.sign}', shell=True)
	os.remove(f.name)


elif args.verify:

	# Dump the signature to a temporary file and load it
	f = tempfile.NamedTemporaryFile(delete=False)
	f.close()
	run(f'arm-none-eabi-objcopy --dump-section .sign.ed25519="{f.name}" {args.verify}', shell=True)
	sm = load_file(f.name)
	os.remove(f.name)

	# Replace the section with nothing and save the result in the temporary file
	f = tempfile.NamedTemporaryFile(delete=False)
	f.write(b'\x00' * 64)
	f.close()
	run(f'arm-none-eabi-objcopy --update-section .sign.ed25519="{f.name}" {args.verify} {f.name}', shell=True)

	# Read back ELF file with cleared signature and delete the temporary file
	elf = load_file(f.name)
	os.remove(f.name)

	hm = blake2s(elf).digest()

	try:
		ed25519.checkvalid(sm, hm, pk)
		print('ok')
	except:
		print('signature verification failed')
		

elif args.save_pubkey:
	save_file(args.save_pubkey, b64encode(pk))
	

else:
	print("no action selected")
	exit(1)
