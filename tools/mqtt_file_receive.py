import paho.mqtt.client as mqtt
import time
import os
import struct
import sys
import threading
import hashlib
import binascii

target = sys.argv[1]
fhash = hashlib.sha256()
f = open("test.fw", "wb");

def on_connect(client, userdata, flags, rc):
	print("MQTT connected")
	client.subscribe(target + "/in")


def on_message(client, userdata, msg):
	if msg.payload[:4] == "open".encode():
		print("open")

	if msg.payload[:4] == "data".encode():
		fhash.update(msg.payload[6:])
		f.write(msg.payload[6:])
		d = b"c" + msg.payload[4:6]
		client.publish(topic = target + "/out", payload = d, qos = 0)

	if msg.payload[:5] == "close".encode():
		print("close");
		f.close()
		print(fhash.hexdigest())


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect("mqtt.krtko.org", 1883, 60)

confirmed = threading.Event()

client.loop_start();
while True:
	time.sleep(1)


