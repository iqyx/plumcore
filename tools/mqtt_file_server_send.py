import paho.mqtt.client as mqtt
import time
import os
import struct
import sys
import queue
import hashlib
import binascii

target = sys.argv[1]
fname = sys.argv[2]
bc = 0

def on_connect(client, userdata, flags, rc):
	print("MQTT connected")
	client.subscribe(target + "/out")


def on_message(client, userdata, msg):
	if msg.payload[0] == ord("c"):
		confirmed.put(struct.unpack("!H", msg.payload[1:3])[0])

	if msg.payload[0] == ord("h"):
		print("remote hash %s" % str(binascii.hexlify(msg.payload[1:])))
		print("local hash %s" % fhash.hexdigest()[:8])
		confirmed.put(1)

	if msg.payload[0] == ord("o"):
		confirmed.put(1)


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect("mqtt.krtko.org", 1883, 60)
client.loop_start()

confirmed = queue.Queue()
fhash = hashlib.sha256()

def send_block(bn, d):
	while True:
		client.publish(topic = target + "/in", payload = d, qos = 0);
		try:
			bc = confirmed.get(timeout = 2.0)
			if bn == bc:
				return
		except queue.Empty:
			pass


def send_close():
	while True:
		client.publish(topic = target + "/in", payload = "close", qos = 0);
		try:
			print("waiting for file close")
			confirmed.get(timeout = 5.0)
			print("OK")
			return
		except queue.Empty:
			pass


def send_open(fn):
	while True:
		client.publish(topic = target + "/in", payload = "open" + os.path.basename(fn), qos = 0);
		try:
			print("Waiting for file open")
			confirmed.get(timeout = 10.0)
			print("OK")
			return
		except queue.Empty:
			pass

# wait for connection
time.sleep(2)
with open(fname, "rb") as f:
	fsize = os.path.getsize(fname)
	print("Local file '%s' opened, size = %dB" % (fname, fsize))
	send_open(fname);

	bn = 0
	sent = 0
	while True:
		data = f.read(64)
		if not data:
			break
		fhash.update(data)
		d = "data".encode()  + struct.pack("!H", bn) + data

		send_block(bn, d)

		sent += len(data)
		bn += 1
		sys.stdout.write("\rBytes sent: %d" % sent)

	sys.stdout.write("\n");
	send_close()
time.sleep(2)
